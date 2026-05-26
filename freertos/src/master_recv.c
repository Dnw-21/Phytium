#include "master.h"
#include "lora_uart.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include "shm_print.h"
#include <string.h>

/*============================================================================
 *  master_recv_task: 接收 -> 按类型存储
 *
 *  状态机:
 *    IDLE            -> 等待 DATA_TYPE_NODE_HEAD / DATA_TYPE_WAVE
 *    RECV_NODE_RAW   -> 累积 DATA_TYPE_NODE_RAW, 满后保存
 *    RECV_FLASH_WAVE -> 累积 DATA_TYPE_FLASH_WAVE (差分编码), 满后保存波形
 *============================================================================*/
typedef enum {
    RSTATE_IDLE = 0,
    RSTATE_RECV_NODE_RAW,
    RSTATE_RECV_FLASH_WAVE
} RecvState_t;

/*============================================================================
 *  解析 DATA_TYPE_NODE_HEAD: NodeUploadHeader_t (轮询/故障 统一)
 *  fault_type==FAULT_NONE -> 正常   fault_type!=FAULT_NONE -> 故障触发
 *============================================================================*/
void process_node_header(const uint8_t *payload, uint16_t len,
                                 MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    if (len < sizeof(NodeUploadHeader_t)) return;

    NodeUploadHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = hdr.node_index;
    if (node_id >= MASTER_MAX_NODES) return;

    /* 初始化下载缓冲区 */
    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_NODE_HEAD;
    dl->expected_points = hdr.total_points;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    /* 初始化节点信息 */
    node->last_total_points = hdr.total_points;
    node->last_sample_rate = hdr.sample_rate;
    node->last_health_score = hdr.health_score;
    node->last_status_timestamp = hdr.timestamp;
    node->severity = hdr.severity;
    node->fault_type = (FaultType_t)hdr.fault_type;
    node->last_status_fault = (FaultType_t)hdr.fault_type;

    if (hdr.fault_type != FAULT_NONE) {
        node->fault_count++;
        log_info("Fault hdr: node%d t=%d type=%d sev=%d pts=%d",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else {
        log_debug("Status hdr: node%d sev=%d health=%.1f pts=%d",
                  node_id, hdr.severity, hdr.health_score, hdr.total_points);
    }
}

/*============================================================================
 *  解析 DATA_TYPE_NODE_RAW: 累积 NodeSample_t 到下载缓冲区
 *  终端每包10个样本 (10×20=200B)
 *============================================================================*/
void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
    (void)node;

    if (!dl->active || dl->data_type != DATA_TYPE_NODE_HEAD) return;

    uint16_t samples_in_pkt = len / sizeof(NodeSample_t);
    if (samples_in_pkt == 0)
        return;
    if (dl->received_points + samples_in_pkt > MASTER_NODE_UPLOAD_POINTS) {
        samples_in_pkt = MASTER_NODE_UPLOAD_POINTS - dl->received_points;
    }

    memcpy(&dl->node_buffer[dl->received_points], payload,
           samples_in_pkt * sizeof(NodeSample_t));
    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        dl->active = 0;
        dl->flash_save_pending = 1;
    }
}

/*============================================================================
 *  解析 DATA_TYPE_WAVE: 波形数据头 -> 准备接收 int16 Flash波形
 *============================================================================*/
void process_wave_header(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                 MasterNodeInfo_t *node, uint16_t src_addr)
{
    (void)src_addr;

    if (len < sizeof(WaveChunkHeader_t)) return;

    WaveChunkHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = hdr.node_index;
    if (node_id >= MASTER_MAX_NODES) node_id = 0;

    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_WAVE;
    dl->expected_points = hdr.sample_count;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    node->has_last_wave_hdr = 1;
    node->last_wave_rate = hdr.sample_rate;
    node->last_wave_samples = hdr.sample_count;
    node->last_wave_severity = (SeverityLevel_t)hdr.severity;
    node->last_wave_fault_idx = hdr.fault_idx;

    dl->flash_erase_pending = 1;

    log_info("Wave hdr: node%d rate=%d sev=%d samp=%d ts=%d idx=%d",
             node_id, hdr.sample_rate, hdr.severity, hdr.sample_count,
             hdr.fault_timestamp, hdr.fault_idx);
}

/*============================================================================
 *  解析 DATA_TYPE_FLASH_WAVE: 累积 int16 波形原始数据包 (差分编码)
 *============================================================================*/
void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                MasterNodeInfo_t *node)
{
    (void)node;

    if (!dl->active || dl->data_type != DATA_TYPE_WAVE) return;

    dl->wave_byte_offset = dl->received_points * sizeof(int16_t);
    dl->wave_chunk_len = len;
    memcpy(dl->wave_chunk, payload, len);
    dl->received_points += (len / sizeof(int16_t));
    dl->flash_wave_pending = 1;

    if (dl->received_points >= dl->expected_points) {
        dl->active = 0;
        dl->flash_wave_done = 1;
    }
}

/*============================================================================
 *  master_recv_task: 主入口
 *============================================================================*/
void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256];
    uint8_t  data[128];
    uint16_t src_addr;
    uint16_t recv_len;
    RecvState_t state = RSTATE_IDLE;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;

    (void)pvParameters;
    (void)state;

    log_info("Recv task started");

    while (1) {
        /* 等待数据到达 */
        if (lora_uart_get_rx_count() == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 软超时检测: rx_count 连续5ms不变 -> 一帧数据已完整到达 */
        {
            uint16_t prev = lora_uart_get_rx_count();
            int stable = 0;
            while (stable < 5) {
                vTaskDelay(pdMS_TO_TICKS(1));
                uint16_t cur = lora_uart_get_rx_count();
                if (cur == prev) {
                    stable++;
                } else {
                    prev = cur;
                    stable = 0;
                }
            }
        }

        /* 标记帧边界, 读取一帧 */
        lora_uart_mark_frame();
        recv_len = lora_uart_read_frame(lora_buf, sizeof(lora_buf));

        if (recv_len < 13) {
            shm_spf("[RECV] short frame %uB:", recv_len);
            for (uint16_t i = 0; i < recv_len; i++)
                shm_spf(" %02X", lora_buf[i]);
            shm_puts("\r\n");
            continue;
        }

        uint8_t *raw_pkt = lora_buf;
        uint16_t raw_len = recv_len;

        uint8_t  rx_type;
        uint32_t sync_code;
        uint16_t enc_len;
        uint8_t *enc_start;
        uint8_t *payload;
        uint16_t payload_len;

        FrameParseResult_t frame_result;
        frame_parse(raw_pkt, raw_len, &frame_result);
        rx_type   = frame_result.rx_type;
        sync_code = frame_result.sync_code;
        enc_len   = frame_result.enc_len;
        enc_start = frame_result.enc_start;

        /* 打印接收的源数据 */
        shm_spf("[DEC] type=0x%02X len=%d:", rx_type, enc_len);
        for (uint16_t i = 0; i < enc_len && i < 64; i++)
            shm_spf(" %02X", enc_start[i]);
        shm_puts("\r\n");

        /* 加密已被注释掉, 直接使用明文 (匹配新版GD32) */
        if (sync_code != 0 && enc_len > 0 && enc_len <= 128) {
            /* 原加密路径保留但注释:
             * payload_len = chaos_decrypt_packet(enc_start, enc_len, data, sync_code);
             * payload = data;
             */
            payload_len = enc_len;
            payload = enc_start;
        } else {
            payload_len = enc_len;
            payload = enc_start;
        }

        /* 确定节点ID */
        uint8_t node_id;
        if (dl->active && (rx_type == DATA_TYPE_NODE_RAW || rx_type == DATA_TYPE_FLASH_WAVE)) {
            node_id = dl->node_id;
        } else {
            node_id = 0;
            if (rx_type == DATA_TYPE_NODE_HEAD && payload_len >= sizeof(NodeUploadHeader_t)) {
                NodeUploadHeader_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            } else if (rx_type == DATA_TYPE_WAVE && payload_len >= sizeof(WaveChunkHeader_t)) {
                WaveChunkHeader_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            }
            if (node_id >= MASTER_MAX_NODES) node_id = 0;
        }
        src_addr = SLAVE_ADDR_BASE + node_id;
        node = master_get_node_info(node_id);
        if (!node) continue;
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (rx_type) {
        case DATA_TYPE_NODE_HEAD: /* 0x01: 节点状态头 (轮询/故障统一) */
            dl->active = 0;
            process_node_header(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_WAVE:      /* 0x02: 波形数据头 */
            dl->active = 0;
            process_wave_header(payload, payload_len, dl, node, src_addr);
            break;

        case DATA_TYPE_POWER:     /* 0x03: 电源电压 (预留) */
            log_debug("Power data: len=%d (reserved)", payload_len);
            break;

        case DATA_TYPE_NODE_RAW:  /* 0x04: 节点原始数据 */
            process_node_raw(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_FLASH_WAVE:/* 0x05: 波形原始数据 (差分编码) */
            process_flash_wave(payload, payload_len, dl, node);
            break;

        default:
            break;
        }

        /* 延迟 Flash/共享内存 操作 (在任务上下文中执行, 不阻塞ISR) */
        if (dl->flash_erase_pending) {
            master_flash_erase_wave(dl->node_id);
            dl->flash_erase_pending = 0;
        }
        if (dl->flash_wave_pending) {
            master_flash_save_wave_data(dl->node_id, dl->wave_chunk,
                                        dl->wave_chunk_len, dl->wave_byte_offset);
            dl->flash_wave_pending = 0;
        }
        if (dl->flash_wave_done) {
            master_recv_wave_data(dl->node_id, dl->received_points);
            log_info("Wave saved: node%d %d samp@%dHz",
                     dl->node_id, dl->received_points, dl->sample_rate);
            dl->flash_wave_done = 0;
        }
        if (dl->flash_save_pending) {
            master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
            log_info("Node%d: status saved (%d pts)", dl->node_id, dl->received_points);
            dl->flash_save_pending = 0;
        }
    }
}
