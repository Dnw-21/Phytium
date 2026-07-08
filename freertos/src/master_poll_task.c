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
 * @brief 加密并发送 LoRa 命令 — 对齐 Master_v3
 */
static void lora_send_encrypted(const uint8_t *data, uint16_t len, uint8_t data_type,
                                 uint16_t addr, uint8_t channel)
{
    uint8_t  sync[CHAOS_SYNC_SIZE];
    uint16_t enc_len = chaos_encrypt_packet(data, len, g_enc_buf, sync);

    memcpy(g_lora_pkt, sync, CHAOS_SYNC_SIZE);
    memcpy(&g_lora_pkt[CHAOS_SYNC_SIZE], g_enc_buf, enc_len);

    LoRaSrc_t dest = { .addr = addr, .channel = channel };
    send_node_data_with_ack(g_lora_pkt, enc_len + CHAOS_SYNC_SIZE, data_type, &dest, 3,
                            xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool wait_download_done(MasterDownloadBuf_t *dl, uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
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

/**
 * @brief 主控轮询任务
 *
 * Tier1: 逐节点下发 CMD_POLL_STATUS, 接收1周期状态数据(20点), 写入Flash
 * Tier2: 对 fault_pending=1 的节点下发 CMD_REQUEST_FAULT_DATA, 接收2周期故障数据(40点)
 */
void master_poll_task(void *pvParameters)
{
    MasterDownloadBuf_t *dl = master_get_download_buf();

    (void)pvParameters;

    shm_puts("Poll task started\r\n");

    vTaskDelay(pdMS_TO_TICKS(3000));

    while (1) {
        TickType_t cycle_start = xTaskGetTickCount();

        for (uint8_t i = 0; i < MASTER_POLL_MAX_NODES; i++) {
            MasterNodeInfo_t *n = master_get_node_info(i);

            if (n->fault_pending) {
                /* 存在故障快照: 上传故障数据，不上传正常节点数据 */
                uint8_t params[1] = { i };
                send_lora_cmd(i, CMD_REQUEST_FAULT_DATA, params, 1);
                if (!wait_download_done(dl, TIER2_TIMEOUT_MS)) {
                    shm_spf("Poll: node%d fault timeout\r\n", i);
                    continue;
                }

                while (dl->active || dl->flash_save_pending)
                    vTaskDelay(5);

                n->fault_pending = 0;
                shm_spf("Poll: node%d fault upload done\r\n", i);
            } else {
                /* 无故障快照: 上传正常节点数据 */
                send_lora_cmd(i, CMD_POLL_STATUS, NULL, 0);
                if (!wait_download_done(dl, TIER1_TIMEOUT_MS)) {
                    shm_spf("Poll: node%d timeout\r\n", i);
                    continue;
                }

                shm_spf("Poll: node%d status ok sev=%d\r\n", i, n->severity);
            }
        }

        /* 保证轮询周期时间稳定 */
        TickType_t elapsed = xTaskGetTickCount() - cycle_start;
        TickType_t cycle_ticks = pdMS_TO_TICKS(MASTER_POLL_CYCLE_MS);
        if (elapsed < cycle_ticks) {
            vTaskDelay(cycle_ticks - elapsed);
        }
    }
}
