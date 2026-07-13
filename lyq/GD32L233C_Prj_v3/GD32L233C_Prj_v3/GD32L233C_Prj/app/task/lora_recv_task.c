#include "tasks.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "mwcc68_cfg.h"
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "data_monitor.h"
#include "wave_monitor.h"
#include "log.h"
#include <string.h>
#include <stdio.h>


void lora_recv_task(void *pvParameters)
{
    uint8_t  lora_buf[LORA_RX_BUF_SIZE]; // 接收缓冲区
    uint8_t  data[128]; 
    uint16_t src_addr;  // 源节点地址
    uint16_t recv_len;

    log_info("LoRa Recv Task started");

    while(1) {
        /* 等待数据到达 */
        if (usart1_get_rx_count() == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 软超时检测: rx_count 连续20ms不变 → 一帧数据已完整到达 */
        {
            uint16_t prev = usart1_get_rx_count();
            int stable = 0;
            while (stable < 20) {
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

        if (recv_len == 1) {
            if (lora_buf[0] == 0x00) {
                log_debug("ACK OK");
            } else {
                log_warn("ACK FAIL (0x%02X), need retransmit", lora_buf[0]);
            }
            continue;
        }
        if (recv_len < 28) continue;  /* 至少28B: [0xAA 0x55][len2B][ts4B][type][sync16B][CRC][0x55 0xAA] */
        
        uint8_t *raw_pkt = lora_buf;
        uint16_t raw_len = recv_len;

        uint8_t  rx_type;
        uint16_t enc_len;
        uint8_t *enc_start;
        uint8_t *payload;
        uint16_t payload_len;

        FrameParseResult_t frame_result;
        int ret = frame_parse(raw_pkt, raw_len, &frame_result);
        if (ret < 0) {
            log_warn("Frame parse failed: %d", ret);
            continue;
        }
        
        rx_type   = frame_result.rx_type;
        enc_len   = frame_result.enc_len;
        enc_start = frame_result.enc_start;
        
        uint32_t sync_sum = 0;
        for (int i = 0; i < 16; i++) sync_sum |= frame_result.sync_code[i];
        if (sync_sum != 0 && enc_len > 0 && enc_len <= 128) {
            payload_len = chaos_decrypt_packet(enc_start, enc_len, data, frame_result.sync_code);
            payload = data;
        } else {
            payload_len = enc_len;
            payload = enc_start;
        }
				
        printf("[DEC] type=0x%02X len=%d: ", rx_type, payload_len);
        // for (uint16_t i = 0; i < payload_len && i < 64; i++)
        //         printf(" 0x%02X", payload[i]);
        printf("\r\n");
//        send_ack(0);	//TODO：设计应答机制
//        log_debug("ACK sent");

        Command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd_id = rx_type;
        memcpy(cmd.cmd_param, payload,
                payload_len > 16 ? 16 : payload_len);

        switch (cmd.cmd_id) {
        case CMD_POLL_STATUS: {
            node_upload_recent_20();
            break;
        }
        case CMD_CLEAR_FLASH: {
            wave_flash_init();
            log_info("Flash cleared");
            break;
        }
        case CMD_REQUEST_FAULT_DATA: {
            uint8_t node_idx = cmd.cmd_param[1];
            set_active_node(node_idx);
            upload_fault_snaps();
            log_info("Fault data sent for node%d", node_idx);
            break;
        }
        default:
            break;
        }
        
    }
}
