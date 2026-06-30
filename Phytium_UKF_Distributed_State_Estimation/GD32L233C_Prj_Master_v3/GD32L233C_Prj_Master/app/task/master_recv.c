#include "master.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include <string.h>
#include <stdio.h>

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

    node->last_total_points = hdr.total_points;
    node->last_sample_rate = hdr.sample_rate;
    node->last_health_score = hdr.health_score;
    node->last_status_timestamp = hdr.timestamp;
    node->severity = hdr.severity;
    node->fault_type = (FaultType_t)hdr.fault_type;
    node->fault_pending = hdr.fault_pending;
    node->last_status_fault = (FaultType_t)hdr.fault_type;

    dl->recv_started = 1;
    dl->recv_expected_points = hdr.total_points;

    if (hdr.fault_type != FAULT_NONE) {
        node->fault_count++;
        log_info("Fault hdr: node%d t=%d type=%d sev=%d pts=%d",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else {
        log_debug("Status hdr: node%d sev=%d health=%.1f pts=%d",
                  node_id, hdr.severity, hdr.health_score, hdr.total_points);
    }
}

static void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
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
        printf("SAMPLE: pg[%.4f %.4f %.4f] qg[%.4f %.4f %.4f]\n",
               s->pg1 / 10000.0f, s->pg2 / 10000.0f, s->pg3 / 10000.0f,
               s->qg1 / 10000.0f, s->qg2 / 10000.0f, s->qg3 / 10000.0f);
        printf("        vmag[%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f]\n",
               s->vmag1 / 10000.0f, s->vmag2 / 10000.0f, s->vmag3 / 10000.0f,
               s->vmag4 / 10000.0f, s->vmag5 / 10000.0f, s->vmag6 / 10000.0f,
               s->vmag7 / 10000.0f, s->vmag8 / 10000.0f, s->vmag9 / 10000.0f);
        printf("        ang[%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f]\n",
               s->vangle1 / 10000.0f, s->vangle2 / 10000.0f, s->vangle3 / 10000.0f,
               s->vangle4 / 10000.0f, s->vangle5 / 10000.0f, s->vangle6 / 10000.0f,
               s->vangle7 / 10000.0f, s->vangle8 / 10000.0f, s->vangle9 / 10000.0f);
    }

    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        dl->active = 0;
        dl->flash_save_pending = 1;
    }
}

void master_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[256];
    uint16_t recv_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();

    log_info("Recv task started");

    while (1) {
        if (usart1_get_rx_count() == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

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

        usart1_mark_frame();
        recv_len = usart1_read_frame(lora_buf, sizeof(lora_buf));
        
        if (recv_len < 13) 
        {	
            for(uint16_t i = 0; i < recv_len; i++)
                printf(" 0x%02X", lora_buf[i]);
            printf("\r\n");
            continue;
        }

        uint8_t *raw_pkt = lora_buf;
        uint16_t raw_len = recv_len;

        FrameParseResult_t frame_result;
        frame_parse(raw_pkt, raw_len, &frame_result);

        if (frame_result.rx_type == DATA_TYPE_NODE_RAW) {
            dl->recv_raw_points += frame_result.enc_len / sizeof(NodeSample_t);
        }

        RecvPacket_t pkt;
        pkt.sync_code = frame_result.sync_code;
        pkt.rx_type   = frame_result.rx_type;
        pkt.enc_len   = frame_result.enc_len;
        
        printf("[Recv] sync=%016llX type=0x%02X len=%d: ", pkt.sync_code, pkt.rx_type, pkt.enc_len);
        printf("\r\n");
        
        if (pkt.enc_len > 220) pkt.enc_len = 220;
        if (pkt.enc_len > 0) {
            memcpy(pkt.enc_data, frame_result.enc_start, pkt.enc_len);
        }

        if (xQueueSend(g_recv_queue, &pkt, 0) != pdPASS) {
            log_warn("Recv queue full, drop type=0x%02X", pkt.rx_type);
        }
    }
}

void master_process_task(void *pvParameters)
{
    uint8_t  data[220];
    uint8_t *payload;
    uint16_t payload_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;

    log_info("Process task started");

    while (1) {
        RecvPacket_t pkt;
        if (xQueueReceive(g_recv_queue, &pkt, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (pkt.sync_code != 0 && pkt.enc_len > 0 && pkt.enc_len <= 220) {
            payload_len = chaos_decrypt_packet(pkt.enc_data, pkt.enc_len, data, pkt.sync_code);
            payload = data;
        } else {
            payload_len = pkt.enc_len;
            payload = pkt.enc_data;
        }

        printf("[DEC] sync=%016llX type=0x%02X len=%d: ", pkt.sync_code, pkt.rx_type, payload_len);
        for (uint16_t i = 0; i < payload_len && i < 64; i++)
            printf(" 0x%02X", payload[i]);
        printf("\r\n");

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

        case DATA_TYPE_POWER:
            log_debug("Power data: len=%d (reserved)", payload_len);
            break;

        case DATA_TYPE_NODE_RAW:
            process_node_raw(payload, payload_len, dl, node);
            break;

        default:
            break;
        }

        send_ack(0);

        if (dl->flash_save_pending) {
            master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
            log_info("Node%d: status saved (%d pts)", dl->node_id, dl->received_points);
            dl->flash_save_pending = 0;
        }
    }
}
