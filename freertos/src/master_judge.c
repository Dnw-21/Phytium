#include "master.h"
#include "log.h"

void master_judge_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t now_ms;

    (void)pvParameters;

    shm_puts("Judge task started\r\n");

    /* 检查每个节点状态, 判断是否超时 */
    while (1) {
        now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (uint8_t i = 0; i < MASTER_MAX_NODES; i++) {
            MasterNodeInfo_t *n = master_get_node_info(i);
            if (!n) continue;

            uint32_t elapsed = now_ms - n->last_recv_time;

            if (elapsed > MASTER_NODE_TIMEOUT_MS && n->is_online) {
                n->is_online = 0;
                shm_spf("Node%d offline (%dms)\r\n", i, elapsed);
                continue;
            }

            if (!n->is_online) continue;
        }

        vTaskDelayUntil(&last_wake, MASTER_JUDGE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}
