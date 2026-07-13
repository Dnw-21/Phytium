#include "tasks.h"
#include "mwcc68_app.h"
#include "chaos_encrypt.h"
#include "log.h"
#include "mwcc68_cfg.h"

#include <string.h>
#include <stdio.h>

static uint8_t g_encrypted_buffer[MAX_ENCRYPT_DATA_LEN];
static uint8_t g_current_packsize = LORA_PACKSIZE_240;


static void set_lora_packsize(uint8_t new_packsize)
{
    if (g_current_packsize != new_packsize) {
        LoRa_EnterConfigMode();
        vTaskDelay(pdMS_TO_TICKS(100));
        LoRa_SetPacksize(new_packsize);
        vTaskDelay(pdMS_TO_TICKS(100));
        LoRa_ExitConfigMode();
        g_current_packsize = new_packsize;
        log_info("LoRa packsize changed to %d bytes", 
                 (new_packsize == LORA_PACKSIZE_32) ? 32 : 
                 (new_packsize == LORA_PACKSIZE_64) ? 64 :
                 (new_packsize == LORA_PACKSIZE_128) ? 128 : 240);
    }
}

void lora_send_task(void *pvParameters)
{
    LoRaSendPacket_t packet;
    TxStatus_t tx_status;

    log_info("LoRa Send Task started");

    while(1) {
        if(xQueueReceive(g_lora_send_queue, &packet, portMAX_DELAY) == pdPASS) {
            
            uint8_t *send_data;
            uint16_t send_len;
            uint8_t  sync_code[16];
            uint8_t  final_packet[MAX_ENCRYPT_DATA_LEN + 16];

           if (packet.data_len > 0 && packet.data_len <= MAX_ENCRYPT_DATA_LEN) {
                log_info("[ENC]TX Raw: type=0x%02X, len=%d", packet.data_type, packet.data_len);
                for (uint16_t i = 0; i < packet.data_len && i < 64; i++) {
                    printf(" 0x%02X", packet.data[i]);
                }
                printf("\n");

               send_len = chaos_encrypt_packet(
                   packet.data,
                   packet.data_len,
                   g_encrypted_buffer,
                   sync_code
               );

                memcpy(&final_packet[0], sync_code, 16);
                memcpy(&final_packet[16], g_encrypted_buffer, send_len);
                
                send_data = final_packet;
                send_len = 16 + send_len;
                
                uint64_t sync_lo;
                memcpy(&sync_lo, &sync_code[8], 8);
                log_debug("Encrypted: sync=0x%016llX, len=%d", sync_lo, send_len);
            } else {
                memset(&final_packet[0], 0, 16);
                memcpy(&final_packet[16], packet.data, packet.data_len);
                
                send_data = final_packet;
                send_len = 16 + packet.data_len;
            }

            send_node_data_with_ack(
                send_data,
                send_len,
                packet.data_type,
                &packet.dest,
                3,
                packet.timestamp
            );

            switch(tx_status) {
                case TX_SUCCESS:
                    log_debug("LoRa Send: Success");
                    xEventGroupSetBits(g_event_group, EVENT_ACK_RECEIVED);
                    break;
                case TX_TIMEOUT:
                    log_warn("LoRa Send: Timeout");
                    break;
                case TX_NO_ACK:
                    log_warn("LoRa Send: No ACK");
                    break;
                case TX_ERROR:
                    log_error("LoRa Send: Error");
                    break;
            }
        }
    }
}
