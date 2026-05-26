#include "master.h"
#include "mwcc68_app.h"
#include "mwcc68_cfg.h"
#include "tasks.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>

static uint8_t g_enc_buf[128];
static uint8_t g_lora_pkt[256];

static void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len)
{
    uint8_t payload[32];
    uint16_t send_len;

    payload[0] = cmd_code;  //指令码
    if (params && param_len > 0 && param_len < 31)
        memcpy(&payload[1], params, param_len);
    uint8_t total = 1 + param_len;

    uint32_t sync;
    uint16_t enc_len = chaos_encrypt_packet(payload, total, g_enc_buf, &sync);

    g_lora_pkt[0] = (sync >> 24) & 0xFF;
    g_lora_pkt[1] = (sync >> 16) & 0xFF;
    g_lora_pkt[2] = (sync >> 8) & 0xFF;
    g_lora_pkt[3] = sync & 0xFF;
    memcpy(&g_lora_pkt[5], g_enc_buf, enc_len);

    LoRaSrc_t my_addr = {
        .my_addr = SLAVE_ADDR_BASE + node_id,
        .channel = 0x00,
    };
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    send_len = enc_len + 4;

    send_node_data_with_ack(g_lora_pkt, send_len, cmd_code, &my_addr, 3, now);

    log_warn("Cmd node%d code=0x%02X failed", node_id, cmd_code);
}

void master_cmd_task(void *pvParameters)
{
    MasterInternalCmd_t cmd;

    log_info("Cmd task started");

    while (1) {
        //TODO:发送指令
        if (xQueueReceive(g_master_cmd_queue, &cmd, portMAX_DELAY) != pdPASS)
            continue;

        MasterNodeInfo_t *n = master_get_node_info(cmd.node_id);
        if (!n || !n->is_online) continue;

        switch (cmd.cmd_type) {
        case MASTER_CMD_REQ_FAULT_LIST: {
            send_lora_cmd(cmd.node_id, CMD_REQUEST_FAULT_LIST, NULL, 0);
            log_info("CMD: req fault list node%d", cmd.node_id);
            break;
        }
        case MASTER_CMD_CLEAR_FLASH: {  
            send_lora_cmd(cmd.node_id, CMD_CLEAR_FLASH, NULL, 0);
            log_info("CMD: clear flash node%d", cmd.node_id);
            break;
        }
        case MASTER_CMD_WAVE_COLLECT: {
            uint8_t params[2] = { (uint8_t)(cmd.sample_rate & 0xFF),
                                  (uint8_t)(cmd.sample_rate >> 8) };
            send_lora_cmd(cmd.node_id, CMD_START_WAVE_COLLECT, params, 2);
            log_info("CMD: start wave collect node%d %dHz", cmd.node_id, cmd.sample_rate);
            break;
        }
        default:
            break;
        }
    }
}