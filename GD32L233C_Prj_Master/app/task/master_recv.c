#include "master.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

#define SLAVE_ADDR_BASE     0x0001
#define FRAME_OVERHEAD      7
#define FRAME_START_0       0xAA
#define FRAME_START_1       0x55
#define FRAME_END_0         0x55
#define FRAME_END_1         0xAA
#define MAX_FRAME_BUF       270

/*============================================================================
 *  解析 DATA_TYPE_STATUS: 区分 NodeUploadData_t 和 FaultUploadHeader_t
 *
 *  NodeUploadData_t (周期上传):   11B, severity 0/1/2, total_points=400
 *  FaultUploadHeader_t (故障上传): 15B, severity=1, 包含 timestamp+fault_type
 *
 *  区分方法: 检查 payload 长度
 *   - 11B → NodeUploadData_t (正常10周期)
 *   - 15B → FaultUploadHeader_t (故障触发10周期)
 *============================================================================*/
static void process_status_header(const uint8_t *payload, uint16_t len, uint16_t src_addr,
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

        log_info("Fault hdr: node%d t=%d type=%d sev=%d pts=%d",
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

        log_debug("Status hdr: node%d sev=%d health=%.1f pts=%d",
                  node_id, hdr.severity, hdr.health_score, hdr.total_points);
    }
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
        master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
        dl->active = 0;
        log_info("Node%d: 10-cycle saved (%d pts)", dl->node_id, dl->received_points);
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

    /* 擦除旧波形 Flash 区 */
    master_flash_erase_wave(node_id);   //TODO: 检查是否需要擦除

    log_info("Wave hdr: node%d rate=%d sev=%d samp=%d",
             node_id, hdr.sample_rate, hdr.severity, hdr.sample_count);
}

/*============================================================================
 *  解析 DATA_TYPE_FLASH_WAVE: 累积 int16 波形原始数据包
 *  终端每包64个int16 (64×2=128B, 大端序)
 *============================================================================*/
static void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
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
 *  calc_frame_crc8: CRC-8 校验 (多项式 0x07)
 *  从终端 data_frame.c 移植
 *============================================================================*/
static uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

/*============================================================================
 *  parse_frame: 从 LoRa 原始数据中解析一帧 (从终端 data_frame.c 移植)
 *
 *  帧格式: [0xAA 0x55][len 2B,BE][data段 lenB][CRC8][0x55 0xAA]
 *  data段:  [timestamp 4B][data_type 1B][sync_code 4B][data_type 1B][payload nB]
 *
 *  返回: out_data = data段中 timestamp 之后的部分
 *              = [sync_code 4B][data_type 1B][encrypted nB]
 *============================================================================*/
static int parse_frame(const uint8_t *raw_data, uint16_t raw_len,
                        uint8_t *out_data, uint16_t *out_data_len,
                        uint8_t *out_type, uint32_t *out_timestamp)
{
    if (!raw_data || raw_len < (FRAME_OVERHEAD + 6)) return 0;

    uint16_t pos = 0;
    while (pos + 1 < raw_len) {
        if (raw_data[pos] == FRAME_START_0 && raw_data[pos + 1] == FRAME_START_1) break;
        pos++;
    }
    if (pos + 1 >= raw_len) return 0;

    uint16_t frame_start = pos;
    if (frame_start + 2 >= raw_len) return 0;

    uint16_t data_len = ((uint16_t)raw_data[frame_start + 2] << 8) | raw_data[frame_start + 3];
    uint16_t frame_total = FRAME_OVERHEAD + data_len;
    if (frame_start + frame_total > raw_len) return 0;

    const uint8_t *data_ptr = &raw_data[frame_start + 4];
    uint8_t crc_received = raw_data[frame_start + 4 + data_len];
    if (calc_frame_crc8(data_ptr, data_len) != crc_received) return 0;

    if (raw_data[frame_start + 4 + data_len + 1] != FRAME_END_0 ||
        raw_data[frame_start + 4 + data_len + 2] != FRAME_END_1) return 0;

    *out_timestamp  = ((uint32_t)data_ptr[0] << 24) | ((uint32_t)data_ptr[1] << 16)
                    | ((uint32_t)data_ptr[2] << 8)  |  data_ptr[3];
    *out_type       = data_ptr[4];
    *out_data_len   = data_len - 5;
    memcpy(out_data, &data_ptr[5], *out_data_len);
    return 1;
}

/*============================================================================
 *  master_recv_task: 主入口 — LoRa_ReceiveData 接收 → parse_frame 解帧 → 混沌解密
 *============================================================================*/
void master_recv_task(void *pvParameters)
{
    uint8_t  raw_buf[MAX_FRAME_BUF];
    uint16_t raw_len;

    uint8_t  inner_data[MAX_FRAME_BUF];
    uint16_t inner_len;
    uint8_t  inner_type;
    uint32_t timestamp;

    uint8_t  dec_buf[128];
    uint16_t dec_len;
    uint16_t src_addr;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;
    (void)pvParameters;

    log_info("Recv task started");

    while (1) {
        uint16_t avail = usart1_data_available();
        if (avail == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        raw_len = usart1_read_data(raw_buf, sizeof(raw_buf));

        /* 打印原始帧 hex */
        printf("[RX %d] ", raw_len);
        for (uint16_t i = 0; i < raw_len && i < 64; i++)
            printf("%02X ", raw_buf[i]);
        if (raw_len > 64) printf("...");
        printf("\r\n");

        /* 终端验证过的 parse_frame: inner_data = [sync(4B)][type(1B)][enc(nB)] */
        if (!parse_frame(raw_buf, raw_len, inner_data, &inner_len, &inner_type, &timestamp)) {
            printf("  [no valid frame]\r\n");
            continue;
        }
        if (inner_len < 5) continue;

        /* 混沌解密 */
        uint32_t sync_code = ((uint32_t)inner_data[0] << 24) | ((uint32_t)inner_data[1] << 16)
                           | ((uint32_t)inner_data[2] << 8)  |  (uint32_t)inner_data[3];
        uint8_t  rx_type   = inner_data[4];
        uint16_t enc_len   = inner_len - 5;

        if (sync_code != 0 && enc_len > 0 && enc_len <= MAX_ENCRYPT_DATA_LEN) {
            dec_len = chaos_decrypt_packet(&inner_data[5], enc_len, dec_buf, sync_code);
        } else {
            memcpy(dec_buf, &inner_data[5], enc_len);
            dec_len = enc_len;
        }

        /* 打印解密后数据 */
        printf("  [Dec %d bytes type=0x%02X] ", dec_len, rx_type);
        for (uint16_t i = 0; i < dec_len && i < 64; i++)
            printf("%02X ", dec_buf[i]);
        if (dec_len > 64) printf("...");
        printf("\r\n");

        /* 识别节点ID */
        uint8_t node_id = 0;
        if (dl->active) {
            node_id = dl->node_id;
        } else {
            if (rx_type == DATA_TYPE_STATUS && dec_len >= sizeof(FaultUploadHeader_t)) {
                FaultUploadHeader_t hdr;
                memcpy(&hdr, dec_buf, sizeof(hdr));
                node_id = hdr.node_index;
            } else if (rx_type == DATA_TYPE_STATUS && dec_len >= sizeof(NodeUploadData_t)) {
                NodeUploadData_t hdr;
                memcpy(&hdr, dec_buf, sizeof(hdr));
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
        case DATA_TYPE_STATUS:
            dl->active = 0;
            process_status_header(dec_buf, dec_len, src_addr, dl, node);
            break;
        case DATA_TYPE_WAVE:
            dl->active = 0;
            process_wave_header(dec_buf, dec_len, dl, node, src_addr);
            break;
        case DATA_TYPE_POWER:
            log_debug("Power: len=%d", dec_len);
            break;
        case DATA_TYPE_NODE_RAW:
            process_node_raw(dec_buf, dec_len, dl, node);
            break;
        case DATA_TYPE_FLASH_WAVE:
            process_flash_wave(dec_buf, dec_len, dl, node);
            break;
        case DATA_TYPE_FAULT_LIST:
            dl->active = 0;
            process_fault_list(dec_buf, dec_len, src_addr, node);
            break;
        default:
            break;
        }
    }
}