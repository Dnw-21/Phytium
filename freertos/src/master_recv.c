#include "master.h"
#include "lora_uart.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include "shm_print.h"
#include <string.h>

/* ===================================================================
 *  process_node_header — 对齐 Master_v3(2) master_recv.c L11-49
 *  关键: node_index 非法时什么都不设，直接 return
 * =================================================================== */
static void process_node_header(const uint8_t *payload, uint16_t len,
                                 MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    if (len < sizeof(NodeUploadHeader_t)) return;

    NodeUploadHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = hdr.node_index;

    if (node_id >= MASTER_MAX_NODES) return;

    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_NODE_HEAD;
    dl->expected_points = hdr.total_points;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    dl->recv_started = 1;
    dl->recv_expected_points = hdr.total_points;

    node->last_total_points = hdr.total_points;
    node->last_sample_rate = hdr.sample_rate;
    node->last_health_score = hdr.health_score;
    node->last_status_timestamp = hdr.timestamp;
    node->severity = hdr.severity;
    node->fault_type = (FaultType_t)hdr.fault_type;
    node->fault_pending = hdr.fault_pending;
    node->last_status_fault = (FaultType_t)hdr.fault_type;

    if (hdr.fault_type != FAULT_NONE) {
        node->fault_count++;
        shm_spf("Fault hdr: node%d t=%d type=%d sev=%d pts=%d\r\n",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else {
        shm_spf("Status hdr: node%d sev=%d health=%d.%02d pts=%d\r\n",
                  node_id, hdr.severity,
                  (int)hdr.health_score, (int)((hdr.health_score - (int)hdr.health_score) * 100),
                  hdr.total_points);
    }
}

static void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
    (void)node;

    if (!dl->active || dl->data_type != DATA_TYPE_NODE_HEAD) return;

    uint16_t samples_in_pkt = len / sizeof(NodeSample_t);
    if (samples_in_pkt == 0)
        return;
    if (dl->received_points + samples_in_pkt > FAULT_UPLOAD_POINTS) {
        samples_in_pkt = FAULT_UPLOAD_POINTS - dl->received_points;
    }

    memcpy(&dl->node_buffer[dl->received_points], payload,
           samples_in_pkt * sizeof(NodeSample_t));

    if (dl->received_points == 0) {
        NodeSample_t *s = (NodeSample_t *)payload;
        shm_spf("SAMPLE: pg[%d %d %d] qg[%d %d %d]\r\n",
               s->pg1, s->pg2, s->pg3,
               s->qg1, s->qg2, s->qg3);
        shm_spf("        vmag[%d %d %d %d %d %d %d %d %d]\r\n",
               s->vmag1, s->vmag2, s->vmag3,
               s->vmag4, s->vmag5, s->vmag6,
               s->vmag7, s->vmag8, s->vmag9);
        shm_spf("        ang[%d %d %d %d %d %d %d %d %d]\r\n",
               s->vangle1, s->vangle2, s->vangle3,
               s->vangle4, s->vangle5, s->vangle6,
               s->vangle7, s->vangle8, s->vangle9);
    }

    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        dl->active = 0;
        dl->flash_save_pending = 1;
    }
}

/* ===================================================================
 *  master_recv_task — 对齐 Master_v3(2) master_recv.c L89-157
 *  关键: NODE_RAW 帧时增加 recv_raw_points 计数
 * =================================================================== */
void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256];
    uint16_t recv_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    (void)pvParameters;

    shm_puts("Recv task started\r\n");

    while (1) {
        static uint32_t loop_count = 0;
        loop_count++;

        if (lora_uart_get_rx_count() == 0) {
            if ((loop_count % 1000) == 0) {
                shm_spf("[RECV-hb] loop=%u isr=%u bytes=%u ring=%u\r\n",
                        (unsigned)loop_count,
                        (unsigned)lora_uart_get_isr_count(),
                        (unsigned)lora_uart_get_byte_total(),
                        (unsigned)lora_uart_get_rx_count());
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* Soft timeout: 等待 UART 数据稳定 */
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

        lora_uart_mark_frame();
        recv_len = lora_uart_read_frame(lora_buf, sizeof(lora_buf));

        if (recv_len < 13) {
            shm_spf("[RECV] short frame %uB:", recv_len);
            for (uint16_t i = 0; i < recv_len; i++)
                shm_spf(" %02X", lora_buf[i]);
            shm_puts("\r\n");
            continue;
        }

        /* 循环解析: 一次 lora_uart_read_frame 可能包含多个 LoRa 帧 */
        uint8_t *p = lora_buf;
        uint16_t remaining = recv_len;

        while (remaining >= 9) {
            FrameParseResult_t frame_result;
            frame_parse(p, remaining, &frame_result);

            if (frame_result.consumed == 0 || frame_result.consumed > remaining)
                break;

            /* Master_v3(2) L136-138: NODE_RAW 帧时累计 raw_points */
            if (frame_result.rx_type == DATA_TYPE_NODE_RAW) {
                dl->recv_raw_points += frame_result.enc_len / sizeof(NodeSample_t);
            }

            static RecvPacket_t pkt;
            memcpy(pkt.sync_code, frame_result.sync_code, CHAOS_SYNC_SIZE);
            pkt.rx_type   = frame_result.rx_type;
            pkt.enc_len   = frame_result.enc_len;

            if (pkt.enc_len > 220) pkt.enc_len = 220;
            if (pkt.enc_len > 0) {
                memcpy(pkt.enc_data, frame_result.enc_start, pkt.enc_len);
            }

            if (xQueueSend(g_recv_queue, &pkt, 0) != pdPASS) {
                shm_spf("[RECV] queue full, drop type=0x%02X\r\n", pkt.rx_type);
            }

            /* 前进到下一个帧 */
            p += frame_result.consumed;
            remaining -= frame_result.consumed;
        }
    }
}

/* ===================================================================
 *  master_process_task — 对齐 Master_v3(2) master_recv.c L159-237
 *  关键: recv_started 由 process_node_header 设置，不在这里设
 * =================================================================== */
void master_process_task(void *pvParameters)
{
    uint8_t  data[220];
    uint8_t *payload;
    uint16_t payload_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;
    (void)pvParameters;

    shm_puts("Process task started\r\n");

    static const uint8_t sync_zero[CHAOS_SYNC_SIZE] = {0};
    while (1) {
        RecvPacket_t pkt;
        if (xQueueReceive(g_recv_queue, &pkt, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (memcmp(pkt.sync_code, sync_zero, CHAOS_SYNC_SIZE) != 0
            && pkt.enc_len > 0 && pkt.enc_len <= 220) {
            payload_len = chaos_decrypt_packet(pkt.enc_data, pkt.enc_len, data, pkt.sync_code);
            payload = data;
        } else {
            payload_len = pkt.enc_len;
            payload = pkt.enc_data;
        }

        shm_spf("[DEC] type=0x%02X len=%d:", pkt.rx_type, payload_len);
        for (uint16_t i = 0; i < payload_len && i < 64; i++)
            shm_spf(" %02X", payload[i]);
        shm_puts("\r\n");

        /* node_id 由头帧中提取，raw帧沿用当前dl->node_id */
        uint8_t node_id;
        if (dl->active && pkt.rx_type == DATA_TYPE_NODE_RAW) {
            node_id = dl->node_id;
        } else {
            node_id = 0;
            if (pkt.rx_type == DATA_TYPE_NODE_HEAD && payload_len >= sizeof(NodeUploadHeader_t)) {
                NodeUploadHeader_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            } else if (pkt.rx_type == DATA_TYPE_FAULT_HEAD && payload_len >= sizeof(NodeUploadHeader_t)) {
                NodeUploadHeader_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            }
            if (node_id >= MASTER_MAX_NODES) node_id = 0;
        }
        node = master_get_node_info(node_id);
        if (!node) continue;
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (pkt.rx_type) {
        case DATA_TYPE_NODE_HEAD:
        case DATA_TYPE_FAULT_HEAD:
            dl->active = 0;
            process_node_header(payload, payload_len, dl, node);
            break;

        case DATA_TYPE_NODE_RAW:
            process_node_raw(payload, payload_len, dl, node);
            break;

        default:
            break;
        }

        send_ack(0, node_id);

        if (dl->flash_save_pending) {
            master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
            shm_spf("Node%d: status saved (%d pts)\r\n", dl->node_id, dl->received_points);
            dl->flash_save_pending = 0;
        }
    }
}