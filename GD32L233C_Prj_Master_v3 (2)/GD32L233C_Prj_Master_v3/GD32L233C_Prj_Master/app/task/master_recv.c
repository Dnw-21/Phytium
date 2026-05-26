#include "master.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 *  master_recv_task: 接收 → 解密 → 按类型存储
 *
 *  状态机:
 *    IDLE            → 等待 DATA_TYPE_STATUS / DATA_TYPE_WAVE / DATA_TYPE_FAULT_LIST
 *    RECV_NODE_RAW   → 累积 DATA_TYPE_NODE_RAW, 满后保存10周期到 Flash
 *    RECV_FLASH_WAVE → 累积 DATA_TYPE_FLASH_WAVE, 满后保存波形到 Flash
 *============================================================================*/
typedef enum {
    RSTATE_IDLE = 0,
    RSTATE_RECV_NODE_RAW,
    RSTATE_RECV_FLASH_WAVE
} RecvState_t;

/*============================================================================
 *  解析 DATA_TYPE_STATUS: 区分 NodeUploadData_t 和 FaultUploadHeader_t
 *
 *  NodeUploadData_t (周期上传):   11B, severity 0/1/2, total_points=400
 *  FaultUploadHeader_t (故障上传): 15B, severity=1, 包含 timestamp+fault_type
 *============================================================================*/
static void process_status_header(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                   MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    if (len < sizeof(NodeUploadData_t)) return;

    NodeUploadData_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = hdr.node_index;
    if (node_id >= MASTER_MAX_NODES) return;

    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_STATUS;
    dl->expected_points = hdr.total_points;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    node->last_status_type = 0;
    node->last_total_points = hdr.total_points;
    node->last_sample_rate = hdr.sample_rate;
    node->last_health_score = hdr.health_score;
    node->severity = hdr.severity;

    log_debug("Status hdr: node%d sev=%d health=%.1f pts=%d",
              node_id, hdr.severity, hdr.health_score, hdr.total_points);
}

/*============================================================================
 *  解析 DATA_TYPE_FAULT_HEAD: FaultUploadHeader_t (故障触发上传)
 *============================================================================*/
static void process_fault_header(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                  MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    if (len < sizeof(FaultUploadHeader_t)) return;

    FaultUploadHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = hdr.node_index;
    if (node_id >= MASTER_MAX_NODES) return;

    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_STATUS;
    dl->expected_points = hdr.total_points;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    node->last_status_type = 1;
    node->last_total_points = hdr.total_points;
    node->last_sample_rate = hdr.sample_rate;
    node->last_status_timestamp = hdr.timestamp;
    node->severity = hdr.severity;
    node->fault_type = hdr.fault_type;
    node->last_status_fault = hdr.fault_type;
    if (hdr.fault_type != FAULT_NONE) node->fault_count++;

    log_info("Fault hdr: node%d t=%d type=%d sev=%d pts=%d",
             node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
}

/*============================================================================
 *  解析 DATA_TYPE_NODE_RAW: 累积 int32×4 原始样本到下载缓冲区
 *  终端每包8个样本 (8×4×4=128B)
 *============================================================================*/
static void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
    if (!dl->active || dl->data_type != DATA_TYPE_STATUS) return;

    uint16_t samples_in_pkt = len / sizeof(NodeSample_t);   //每包样本数
    if (samples_in_pkt == 0) 
        return;
    if (dl->received_points + samples_in_pkt > FAULT_UPLOAD_POINTS) {   // 检查是否超出故障上传点
        samples_in_pkt = FAULT_UPLOAD_POINTS - dl->received_points;
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
 *  解析 DATA_TYPE_WAVE: 波形数据头 → 准备接收 int16 Flash波形
 *============================================================================*/
static void process_wave_header(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                 MasterNodeInfo_t *node, uint16_t src_addr)
{
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

    dl->flash_erase_pending = 1;

    log_info("Wave hdr: node%d rate=%d sev=%d samp=%d ts=%d",
             node_id, hdr.sample_rate, hdr.severity, hdr.sample_count, hdr.fault_timestamp);
}

/*============================================================================
 *  解析 DATA_TYPE_FLASH_WAVE: 累积 int16 波形原始数据包
 *  终端每包64个int16 (64×2=128B, 大端序)
 *============================================================================*/
static void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                MasterNodeInfo_t *node)
{
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
 *  解析 DATA_TYPE_FAULT_LIST: 终端列为8个uint8有效性标志
 *============================================================================*/
static void process_fault_list(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                MasterNodeInfo_t *node)
{
    if (len > 8) len = 8;
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (payload[i]) valid_count++;
    }
    log_info("Fault list: node%d=%d valid/%d", src_addr, valid_count, len);
}

/*============================================================================
 *  master_recv_task: 主入口
 *============================================================================*/
void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256]; // 接收缓冲区
    uint8_t  data[128]; 
    uint16_t src_addr;  // 源节点地址
    uint16_t recv_len;
    RecvState_t state = RSTATE_IDLE;

    MasterDownloadBuf_t *dl = master_get_download_buf();     // 下载缓冲区
    MasterNodeInfo_t *node; // 1个节点快照

    log_info("Recv task started");

    while (1) {
        /* 等待数据到达 */
        if (usart1_get_rx_count() == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 软超时检测: rx_count 连续10ms不变 → 一帧数据已完整到达 */
        {
            uint16_t prev = usart1_get_rx_count();
            int stable = 0;
            while (stable < 5) {
                vTaskDelay(pdMS_TO_TICKS(1));
                uint16_t cur = usart1_get_rx_count();
                if (cur == prev) {
                    stable++;
                } else {
                    prev = cur;
                    stable = 0;
                }
            }
        }

        /* 标记帧边界, 读取一帧 */
        usart1_mark_frame();
        recv_len = usart1_read_frame(lora_buf, sizeof(lora_buf));
        if (recv_len < 13) continue;  /* 至少: [0xAA 0x55][len2B][ts4B][type][CRC2B][0x55 0xAA] */

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
        
        //打印接收的源数据
        printf("[DEC] type=0x%02X len=%d: ", rx_type, enc_len);
        for (uint16_t i = 0; i < enc_len && i < 64; i++)
            printf(" 0x%02X", enc_start[i]);
        printf("\r\n");

        if (sync_code != 0 && enc_len > 0 && enc_len <= 128) {
            // payload_len = chaos_decrypt_packet(enc_start, enc_len, data, sync_code);
            // payload = data;
            payload_len = enc_len;
            payload = enc_start;
        } else {
            payload_len = enc_len;
            payload = enc_start;
        }

        uint8_t node_id;
        if (dl->active && (rx_type == DATA_TYPE_NODE_RAW || rx_type == DATA_TYPE_FLASH_WAVE)) {
            node_id = dl->node_id;
        } else {
            node_id = 0;
            if (rx_type == DATA_TYPE_STATUS && payload_len >= sizeof(NodeUploadData_t)) {
                NodeUploadData_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            } else if (rx_type == DATA_TYPE_FAULT_HEAD && payload_len >= sizeof(FaultUploadHeader_t)) {
                FaultUploadHeader_t hdr;
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
        case DATA_TYPE_STATUS:    /* 0x01: 节点状态头 */
            dl->active = 0;
            process_status_header(payload, payload_len, src_addr, dl, node);
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

        case DATA_TYPE_FLASH_WAVE:/* 0x05: 波形原始数据 */
            process_flash_wave(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_FAULT_LIST:/* 0x06: 故障列表 */
            dl->active = 0;
            process_fault_list(payload, payload_len, src_addr, node);
            break;

        case DATA_TYPE_FAULT_HEAD:/* 0x07: 故障节点状态头 */
            dl->active = 0;
            process_fault_header(payload, payload_len, src_addr, dl, node);
            break;

        default:
            //log_warn("Unknown data type: 0x%02X", rx_type);
            break;
        }
        //发送回复ACK
        send_ack(0);

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
            log_info("Node%d: 10-cycle saved (%d pts)", dl->node_id, dl->received_points);
            dl->flash_save_pending = 0;
        }

    }
}