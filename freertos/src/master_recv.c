#include "master.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>

#define SLAVE_ADDR_BASE     0x0001

extern uint32_t lora_rx_avail(void);
extern int lora_rx_read_byte(uint8_t *byte);


/*============================================================================
 *  process_status_header: 解析 DATA_TYPE_STATUS, 区分两种头部
 *
 *  NodeUploadData_t (周期上传):   11B, severity 0/1/2, total_points=400
 *  FaultUploadHeader_t (故障上传): 15B, severity=1, timestamp+fault_type
 *============================================================================*/
void process_status_header(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                   MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    uint8_t node_id;

    if (len >= sizeof(FaultUploadHeader_t)) {
        FaultUploadHeader_t hdr;
        memcpy(&hdr, payload, sizeof(hdr));
        node_id = hdr.node_index;
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

        log_info("Fault hdr: node%d t=%u type=%d sev=%d pts=%u",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else if (len >= sizeof(NodeUploadData_t)) {
        NodeUploadData_t hdr;
        memcpy(&hdr, payload, sizeof(hdr));
        node_id = hdr.node_index;
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

        log_debug("Status hdr: node%d sev=%d health=%.1f pts=%u",
                  node_id, hdr.severity, (double)hdr.health_score, hdr.total_points);
    }
}


/*============================================================================
 *  process_node_raw: 累积 int32x4 原始样本到下载缓冲区
 *============================================================================*/
void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
    if (!dl->active || dl->data_type != DATA_TYPE_STATUS) return;

    uint16_t samples_in_pkt = len / sizeof(NodeSample_t);
    if (samples_in_pkt == 0)
        return;
    if (dl->received_points + samples_in_pkt > FAULT_UPLOAD_POINTS) {
        samples_in_pkt = FAULT_UPLOAD_POINTS - dl->received_points;
    }

    memcpy(&dl->node_buffer[dl->received_points], payload,
           samples_in_pkt * sizeof(NodeSample_t));
    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
        dl->active = 0;
        log_info("Node%d: status data saved (%d pts)", dl->node_id, dl->received_points);
    }
}


/*============================================================================
 *  process_wave_header: 解析 DATA_TYPE_WAVE 波形头
 *============================================================================*/
void process_wave_header(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                 MasterNodeInfo_t *node, uint16_t src_addr)
{
    if (len < sizeof(WaveChunkHeader_t)) return;

    WaveChunkHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = (uint8_t)(src_addr - 0x0001);
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

    master_flash_erase_wave(node_id);

    log_info("Wave hdr: node%d rate=%u sev=%d samp=%u",
             node_id, hdr.sample_rate, hdr.severity, hdr.sample_count);
}


/*============================================================================
 *  process_flash_wave: 累积 int16 波形数据到共享内存 Flash 区
 *============================================================================*/
void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                MasterNodeInfo_t *node)
{
    if (!dl->active || dl->data_type != DATA_TYPE_WAVE) return;

    uint16_t byte_offset = dl->received_points * sizeof(int16_t);
    master_flash_save_wave_data(dl->node_id, payload, len, byte_offset);
    dl->received_points += (len / sizeof(int16_t));

    if (dl->received_points >= dl->expected_points) {
        master_recv_wave_data(dl->node_id, dl->received_points);
        dl->active = 0;
        log_info("Wave saved: node%d %d samp@%dHz",
                 dl->node_id, dl->received_points, dl->sample_rate);
    }
}


/*============================================================================
 *  process_fault_list: 解析 DATA_TYPE_FAULT_LIST 终端故障列表
 *============================================================================*/
void process_fault_list(const uint8_t *payload, uint16_t len, uint16_t src_addr,
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
 *  master_recv_task: GD32移植 — 软超时帧接收 + 状态机
 *
 *  数据流:
 *    UART2 ISR (main.c) → ring buffer → 本任务读取 → 搜索帧边界 →
 *    提取 type/sync → 混沌解密 → 按类型处理
 *
 *  帧格式 (GD32 mwcc68): [AA55][LEN:2B][TS:4B][TYPE:1B][SYNC:4B][ENC:N][CRC?][55AA]
 *  帧尾计算: tail_pos = pos + 5 + data_len (GD32 原始代码)
 *============================================================================*/
void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256];
    uint8_t  data[128];
    uint16_t src_addr;
    uint16_t recv_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;

    (void)pvParameters;

    log_info("Recv task started (GD32 port)");

    while (1) {
        if (lora_rx_avail() == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 软超时检测: 环缓冲可用字节连续10ms不变 → 一帧已到达 */
        {
            uint32_t prev = lora_rx_avail();
            int stable = 0;
            while (stable < 20) {
                vTaskDelay(pdMS_TO_TICKS(1));
                uint32_t cur = lora_rx_avail();
                if (cur == prev) {
                    stable++;
                } else {
                    prev = cur;
                    stable = 0;
                }
            }
        }

        /* 读取全部已累积字节到 lora_buf */
        recv_len = 0;
        {
            uint32_t avail = lora_rx_avail();
            while (recv_len < sizeof(lora_buf) && avail > 0) {
                uint8_t b;
                if (lora_rx_read_byte(&b) == 0) {
                    lora_buf[recv_len++] = b;
                }
                avail--;
            }
        }

        if (recv_len < 13) continue;

        uint8_t *raw_pkt = lora_buf;
        uint16_t raw_len = recv_len;

        uint8_t  rx_type;
        uint32_t sync_code;
        uint16_t enc_len;
        uint8_t *enc_start;
        uint8_t *payload;
        uint16_t payload_len;

        /* 搜索帧边界: AA55...+55AA (匹配 GD32 帧尾计算逻辑) */
        {
            int frame_found = 0;
            for (int i = 0; i < (int)raw_len - 1; i++) {
                if (raw_pkt[i] == 0xAA && raw_pkt[i + 1] == 0x55) {
                    if (i + 4 > (int)raw_len) break;
                    uint16_t frame_data_len = ((uint16_t)raw_pkt[i + 2] << 8) | raw_pkt[i + 3];
                    int tail_pos = i + 5 + frame_data_len;
                    if (tail_pos + 2 <= (int)raw_len &&
                        raw_pkt[tail_pos] == 0x55 && raw_pkt[tail_pos + 1] == 0xAA) {
                        uint8_t *frame = &raw_pkt[i];
                        rx_type   = frame[8];
                        sync_code = ((uint32_t)frame[9]  << 24) | ((uint32_t)frame[10] << 16)
                                  | ((uint32_t)frame[11] << 8)  |  (uint32_t)frame[12];
                        enc_len   = frame_data_len - 9;
                        enc_start = &frame[13];
                        frame_found = 1;
                        break;
                    }
                }
            }

            if (!frame_found) {
                sync_code = ((uint32_t)raw_pkt[0] << 24) | ((uint32_t)raw_pkt[1] << 16)
                          | ((uint32_t)raw_pkt[2] << 8)  |  (uint32_t)raw_pkt[3];
                rx_type   = raw_pkt[4];
                enc_len   = raw_len - 5;
                enc_start = &raw_pkt[5];
            }
        }

        /* 混沌解密 */
        if (sync_code != 0 && enc_len > 0 && enc_len <= 128) {
            payload_len = chaos_decrypt_packet(enc_start, enc_len, data, sync_code);
            payload = data;
        } else {
            payload_len = enc_len;
            payload = enc_start;
        }

        /* 确定节点ID */
        uint8_t node_id;
        if (dl->active) {
            node_id = dl->node_id;
        } else {
            node_id = 0;
            if (rx_type == DATA_TYPE_STATUS && payload_len >= 3) {
                uint8_t idx = (payload_len == 15) ? 10 : 2;
                node_id = payload[idx];
            }
            if (node_id >= MASTER_MAX_NODES) node_id = 0;
        }
        src_addr = SLAVE_ADDR_BASE + node_id;
        node = master_get_node_info(node_id);
        if (!node) continue;
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (rx_type) {
        case DATA_TYPE_STATUS:
            dl->active = 0;
            process_status_header(payload, payload_len, src_addr, dl, node);
            break;

        case DATA_TYPE_WAVE:
            dl->active = 0;
            process_wave_header(payload, payload_len, dl, node, src_addr);
            break;

        case DATA_TYPE_POWER:
            log_debug("Power data: len=%d (reserved)", payload_len);
            break;

        case DATA_TYPE_NODE_RAW:
            process_node_raw(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_FLASH_WAVE:
            process_flash_wave(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_FAULT_LIST:
            dl->active = 0;
            process_fault_list(payload, payload_len, src_addr, node);
            break;

        default:
            break;
        }
    }
}