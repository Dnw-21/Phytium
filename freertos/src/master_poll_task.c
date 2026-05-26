#include "master.h"
#include "data_frame.h"
#include "log.h"
#include "tasks.h"
#include "chaos_encrypt.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define LORA_CHN  23  /* LoRa 信道, 与 MWCC68D 模块配置一致 */

static uint8_t g_enc_buf[220];
static uint8_t g_lora_pkt[256];

static bool dl_is_idle(MasterDownloadBuf_t *dl)
{
    return dl->active == 0
        && dl->flash_save_pending == 0
        && dl->flash_erase_pending == 0
        && dl->flash_wave_pending == 0
        && dl->flash_wave_done == 0;
}

static bool wait_download_done(MasterDownloadBuf_t *dl, uint32_t timeout_ms)
{
    bool started = false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        if (dl->active)
            started = true;
        if (started && dl_is_idle(dl))
            return true;
        vTaskDelay(5);
    }
    return false;
}

/**
 * @brief 主控下发 Lora 命令 (与 GD32 新版一致)
 *   构建: [sync(4B)][cmd_code(1B)][params(NB)]
 *   通过 send_node_data_with_ack 添加 AA55/CRC8 帧包装后经 UART2 → LoRa 模块发出
 */
void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len)
{
    uint8_t payload[32];

    payload[0] = cmd_code;
    if (params && param_len > 0 && param_len < 31)
        memcpy(&payload[1], params, param_len);
    uint8_t total = 1 + param_len;

    /* 加密已注释掉, 直接使用明文 (匹配新版 GD32) */
    uint32_t sync = 0;
    /* uint16_t enc_len = chaos_encrypt_packet(payload, total, g_enc_buf, &sync); */
    uint16_t enc_len = total;
    memcpy(g_enc_buf, payload, total);

    /* 构建发送包: [sync(4B)][enc_data(NB)] */
    g_lora_pkt[0] = (sync >> 24) & 0xFF;
    g_lora_pkt[1] = (sync >> 16) & 0xFF;
    g_lora_pkt[2] = (sync >> 8) & 0xFF;
    g_lora_pkt[3] = sync & 0xFF;
    memcpy(&g_lora_pkt[4], g_enc_buf, enc_len);
    uint16_t send_len = enc_len + 4;

    /* 目标节点地址 */
    LoRaSrc_t dest = {
        .addr    = SLAVE_ADDR_BASE + node_id,
        .channel = LORA_CHN,
    };
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    /* 调试: hex dump g_lora_pkt */
    {
        char dbg[128];
        int dp = 0;
        dp += snprintf(dbg + dp, sizeof(dbg) - dp, "SEND: node%d code=0x%02X pkt[%d]=", node_id, cmd_code, send_len);
        for (int i = 0; i < (int)send_len && dp < 100; i++)
            dp += snprintf(dbg + dp, sizeof(dbg) - dp, " %02X", g_lora_pkt[i]);
        log_info("%s", dbg);
    }

    /* 通过 send_node_data_with_ack 添加 AA55/CRC8 帧包装, 经 UART2 发送 */
    send_node_data_with_ack(g_lora_pkt, send_len, cmd_code, &dest, 3, now);
}

/**
 * @brief 主控两层轮询任务
 *
 * Tier1: 逐节点下发 poll_ts, 接收状态数据
 * Tier2: 有故障节点 -> CMD_REQUEST_WAVEFORM 按序下载波形
 */
void master_poll_task(void *pvParameters)
{
    MasterDownloadBuf_t *dl = master_get_download_buf();
    uint8_t fault_nodes[MASTER_POLL_MAX_NODES];
    uint8_t fault_count;

    (void)pvParameters;

    log_info("Poll task started");

    while (1) {
        TickType_t cycle_start = xTaskGetTickCount();
        fault_count = 0;

        uint32_t poll_ts = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint8_t  poll_params[4];
        poll_params[0] = (poll_ts >> 24) & 0xFF;
        poll_params[1] = (poll_ts >> 16) & 0xFF;
        poll_params[2] = (poll_ts >> 8) & 0xFF;
        poll_params[3] = poll_ts & 0xFF;

        /*=== Tier 1: 状态轮 (Status Round) ===*/
        for (uint8_t i = 0; i < MASTER_POLL_MAX_NODES; i++) {
            MasterNodeInfo_t *n = master_get_node_info(i);

            send_lora_cmd(i, CMD_POLL_STATUS, poll_params, 4);

            if (!wait_download_done(dl, MASTER_POLL_NODE_TIMEOUT_MS)) {
                log_warn("Poll: node%d status timeout", i);
                continue;
            }

            log_info("Poll: node%d status ok sev=%d", i, n->severity);

            if (n->severity >= SEVERITY_WARNING
                && fault_count < MASTER_POLL_MAX_NODES
                && (n->last_wave_fault_idx == 0xFF
                    || (uint32_t)(n->last_wave_fault_idx + 1) < (uint32_t)n->fault_count)) {
                fault_nodes[fault_count++] = i;
            }
        }

        /*=== Tier 2: 波形轮 (Waveform Round) ===*/
        if (fault_count > 0) {
            TickType_t tier2_deadline = xTaskGetTickCount()
                                        + pdMS_TO_TICKS(MASTER_POLL_TIER2_BUDGET_MS);

            for (uint8_t j = 0; j < fault_count; j++) {
                if (xTaskGetTickCount() >= tier2_deadline)
                    break;

                MasterNodeInfo_t *wn = master_get_node_info(fault_nodes[j]);
                if (!wn || !wn->is_online)
                    continue;

                uint8_t next_idx = wn->last_wave_fault_idx + 1;
                uint8_t params[2] = { fault_nodes[j], next_idx };
                send_lora_cmd(fault_nodes[j], CMD_REQUEST_WAVEFORM, params, 2);

                if (!wait_download_done(dl, MASTER_POLL_WAVE_TIMEOUT_MS)) {
                    log_warn("Poll: node%d wave timeout", fault_nodes[j]);
                    continue;
                }

                log_info("Poll: node%d wave idx=%d ok", fault_nodes[j], wn->last_wave_fault_idx);
            }
        }

        /* 保证轮询周期时间稳定 */
        TickType_t elapsed = xTaskGetTickCount() - cycle_start;
        uint32_t cycle_ms = fault_count > 0 ? MASTER_POLL_CYCLE_FAST_MS : MASTER_POLL_CYCLE_MS;
        TickType_t cycle_ticks = pdMS_TO_TICKS(cycle_ms);
        if (elapsed < cycle_ticks) {
            vTaskDelay(cycle_ticks - elapsed);
        }
    }
}
