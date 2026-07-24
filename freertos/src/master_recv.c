#include "master.h"
#include "lora_uart.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include "shm_print.h"
#include <string.h>

/* RPMsg 发送函数 (main.c) */
extern int rpmsg_send_lora_raw(const uint8_t *frame, uint32_t frame_len);
extern int rpmsg_send_node_info(const void *data, uint16_t len);

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
        shm_spf("FaultStatus hdr: node%d t=%d type=%d sev=%d pts=%d\r\n",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else {
        shm_spf("NormalStatus hdr: node%d sev=%d health=%d.%02d pts=%d\r\n",
                  node_id, hdr.severity,
                  (int)hdr.health_score, (int)((hdr.health_score - (int)hdr.health_score) * 100),
                  hdr.total_points);
    }

    /* 通过 RPMsg 发送节点头信息到 Linux (NodeUploadHeader_t, 18B packed) */
    shm_spf("RPMsg tx: NodeStatus hdr node%d %uB\r\n", node_id, sizeof(hdr));
    rpmsg_send_node_info(&hdr, sizeof(hdr));
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

    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        dl->active = 0;
        dl->flash_save_pending = 1;

        /* 通过 RPMsg 发送完整原始数据到 Linux (NodeSample_t * points, 40B/sample) */
        shm_spf("RPMsg tx: LoraRaw node%d %uB (%u samples)\r\n",
                dl->node_id, dl->received_points * sizeof(NodeSample_t), dl->received_points);
        rpmsg_send_lora_raw((const uint8_t *)dl->node_buffer,
                            dl->received_points * sizeof(NodeSample_t));
    }
}

/* ===================================================================
 *  master_recv_task — 对齐 Master_v3(2) master_recv.c L89-157
 * =================================================================== */
void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256];
    uint16_t recv_len;

    (void)pvParameters;

    shm_puts("Recv task started\r\n");

    while (1) {
        static uint32_t loop_count = 0;
        loop_count++;

        if (lora_uart_get_rx_count() == 0) {
            if ((loop_count % 1000) == 0) {
                // shm_spf("[RECV-hb] loop=%u isr=%u bytes=%u ring=%u\r\n",
                //         (unsigned)loop_count,
                //         (unsigned)lora_uart_get_isr_count(),
                //         (unsigned)lora_uart_get_byte_total(),
                //         (unsigned)lora_uart_get_rx_count());
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

            /* NODE_RAW 帧提前计数: 对所有节点的 dl 累加，send_lora_cmd 会在 poll 前清零 */
            if (frame_result.rx_type == DATA_TYPE_NODE_RAW) {
                uint16_t n_points = frame_result.enc_len / sizeof(NodeSample_t);
                for (uint8_t n = 0; n < MASTER_MAX_NODES; n++) {
                    master_get_download_buf(n)->recv_raw_points += n_points;
                }
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
 *  master_process_task — 每个节点独立维护下载状态
 * =================================================================== */
void master_process_task(void *pvParameters)
{
    uint8_t  data[220];
    uint8_t *payload;
    uint16_t payload_len;

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

        /* 确定 node_id: HEADER 帧从 payload 提取, RAW 帧查找活跃的 dl */
        uint8_t node_id = 0;

        if (pkt.rx_type == DATA_TYPE_NODE_HEAD || pkt.rx_type == DATA_TYPE_FAULT_HEAD) {
            if (payload_len >= sizeof(NodeUploadHeader_t)) {
                NodeUploadHeader_t hdr;
                memcpy(&hdr, payload, sizeof(hdr));
                node_id = hdr.node_index;
            }
        } else if (pkt.rx_type == DATA_TYPE_NODE_RAW) {
            /* 查找当前活跃的节点下载缓冲区 */
            for (uint8_t n = 0; n < MASTER_MAX_NODES; n++) {
                MasterDownloadBuf_t *candidate = master_get_download_buf(n);
                if (candidate->active) {
                    node_id = n;
                    break;
                }
            }
        }
        if (node_id >= MASTER_MAX_NODES) node_id = 0;

        MasterDownloadBuf_t *dl   = master_get_download_buf(node_id);
        MasterNodeInfo_t    *node = master_get_node_info(node_id);
        if (!node) continue;
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (pkt.rx_type) {
        case DATA_TYPE_NODE_HEAD:
        case DATA_TYPE_FAULT_HEAD:
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
            // master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
            shm_spf("Node%d: status saved (%d pts)\r\n", dl->node_id, dl->received_points);
            dl->flash_save_pending = 0;
        }
    }
}