#include "master.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include "chaos_encrypt.h"
#include "shm_print.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#define LORA_CHN  23

static uint8_t g_enc_buf[MAX_ENCRYPT_DATA_LEN];
static uint8_t g_lora_pkt[256];

/**
 * @brief 加密并发送 LoRa 命令 — 对齐 Master_v3(2) lora_send_encrypted()
 * @note  使用 chaos_encrypt_packet() 加密 payload, 8字节 sync 大端写入帧头
 */
static void lora_send_encrypted(const uint8_t *data, uint16_t len, uint8_t data_type,
                                 uint16_t addr, uint8_t channel)
{
    uint64_t sync = 0;
    uint16_t enc_len = chaos_encrypt_packet(data, len, g_enc_buf, &sync);

    /* 8字节 sync 大端写入 g_lora_pkt[0..7] */
    g_lora_pkt[0] = (sync >> 56) & 0xFF;
    g_lora_pkt[1] = (sync >> 48) & 0xFF;
    g_lora_pkt[2] = (sync >> 40) & 0xFF;
    g_lora_pkt[3] = (sync >> 32) & 0xFF;
    g_lora_pkt[4] = (sync >> 24) & 0xFF;
    g_lora_pkt[5] = (sync >> 16) & 0xFF;
    g_lora_pkt[6] = (sync >> 8) & 0xFF;
    g_lora_pkt[7] = sync & 0xFF;
    memcpy(&g_lora_pkt[8], g_enc_buf, enc_len);

    LoRaSrc_t dest = { .addr = addr, .channel = channel };
    send_node_data_with_ack(g_lora_pkt, enc_len + 8, data_type, &dest, 3,
                            xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool dl_is_idle(MasterDownloadBuf_t *dl)
{
    return dl->active == 0 && dl->flash_save_pending == 0;
}

static bool wait_download_done(MasterDownloadBuf_t *dl, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        /* Master_v3(2) L38-45: recv_started && recv_raw_points >= recv_expected_points */
        if (dl->recv_started && dl->recv_raw_points >= dl->recv_expected_points)
            return true;
        vTaskDelay(5);
    }
    return false;
}

/**
 * @brief 主控下发 Lora 命令 — 加密后发送
 */
void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len)
{
    uint8_t payload[32];

    MasterDownloadBuf_t *dl = master_get_download_buf();
    dl->recv_started = 0;
    dl->recv_raw_points = 0;

    payload[0] = cmd_code;
    if (params && param_len > 0 && param_len < 31)
        memcpy(&payload[1], params, param_len);
    uint8_t total = 1 + param_len;

    shm_spf("[ENC][CMD] node%d code=0x%02X payload:", node_id, cmd_code);
    for (uint8_t i = 0; i < total; i++)
        shm_spf(" %02X", payload[i]);
    shm_puts("\r\n");

    lora_send_encrypted(payload, total, cmd_code,
                        SLAVE_ADDR_BASE + node_id, LORA_CHN);
}

void master_poll_task(void *pvParameters)
{
    MasterDownloadBuf_t *dl = master_get_download_buf();

    (void)pvParameters;

    shm_puts("Poll task started (fast x4)\r\n");

    vTaskDelay(pdMS_TO_TICKS(3000));

    while (1) {
        TickType_t cycle_start = xTaskGetTickCount();

        uint32_t poll_ts = 0xFFFFFFFF;
        uint8_t  poll_params[4];
        poll_params[0] = (poll_ts >> 24) & 0xFF;
        poll_params[1] = (poll_ts >> 16) & 0xFF;
        poll_params[2] = (poll_ts >> 8) & 0xFF;
        poll_params[3] = poll_ts & 0xFF;

        send_lora_cmd(0, CMD_POLL_STATUS, poll_params, 4);
        if (wait_download_done(dl, TIER1_TIMEOUT_MS)) {
            shm_spf("Poll: ok sev=%d\r\n", master_get_node_info(0)->severity);
        } else {
            shm_spf("Poll: timeout\r\n");
        }
        while (dl->active || dl->flash_save_pending)
            vTaskDelay(5);

        /* 检查是否有待上传故障 */
        {
            MasterNodeInfo_t *n = master_get_node_info(0);
            if (n && n->fault_pending) {
                uint8_t params[1] = { 0 };
                send_lora_cmd(0, CMD_REQUEST_FAULT_DATA, params, 1);
                if (!wait_download_done(dl, TIER2_TIMEOUT_MS)) {
                    shm_spf("Tier2: fault timeout\r\n");
                } else {
                    while (dl->active || dl->flash_save_pending)
                        vTaskDelay(5);
                    n->fault_pending = 0;
                    shm_spf("Tier2: fault upload done\r\n");
                }
            }
        }

        TickType_t elapsed = xTaskGetTickCount() - cycle_start;
        uint32_t cycle_ms = MASTER_POLL_CYCLE_MS;
        TickType_t cycle_ticks = pdMS_TO_TICKS(cycle_ms);
        if (elapsed < cycle_ticks) {
            vTaskDelay(cycle_ticks - elapsed);
        }
    }
}