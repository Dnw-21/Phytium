#include "data_collect_task.h"
#include "tasks.h"
#include "data_frame.h"
#include "data_monitor.h"
#include "zdata_adaptive.h"
#include "log.h"

#include <string.h>

#define OUTPUT_RATE       1       /* 每2个采样点输出一次 (1kHz输出) */

static uint32_t g_time_index = 3;           /* 时间点索引 (0~59) */
static uint32_t g_sample_counter = 0;       /* 采样计数器 */

#define ANOMALY_NORMAL_DURATION1   30000     
#define ANOMALY_NORMAL_DURATION2   50000    
#define ANOMALY_FAULT_COUNT       ZDATA_FAULT_POINTS

/*============================================================================
 * data_collect_task: 按2kHz采样频率采集数据，按200Hz频率输出
 * 波形采集由 wave_capture (TIM5 ISR) 独立完成
 *============================================================================*/
void data_collect_task(void *pvParameters)
{
    SensorData_t data;
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t sample_interval = NODE_SAMPLE_RATE / 1000; // 1000Hz采样间隔 (ms)

    while (1) {
        uint8_t node = get_active_node();                          /* 当前选中节点 */

        g_time_index = (g_time_index + 1) % ZDATA_NORMAL_POINTS;    /* 轮转时间点 */

        const ZDataPoint_t *dp;

        if (g_sample_counter >= ANOMALY_NORMAL_DURATION1 && g_sample_counter < ANOMALY_NORMAL_DURATION1+ ANOMALY_FAULT_COUNT) {
            uint32_t fault_idx = g_sample_counter - ANOMALY_NORMAL_DURATION1;
            dp = &g_zdata_fault[fault_idx];
        } 
        else if(g_sample_counter >= ANOMALY_NORMAL_DURATION2 && g_sample_counter < ANOMALY_NORMAL_DURATION2+ ANOMALY_FAULT_COUNT) {
            uint32_t fault_idx = g_sample_counter - ANOMALY_NORMAL_DURATION2;
            dp = &g_zdata_fault[fault_idx];
        }
        else {
            dp = &g_zdata_normal[g_time_index];
        }

        data.zdata     = *dp;
        data.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;

        xQueueSend(g_sampledata_queue, &data, portMAX_DELAY);
        g_sample_counter++;

        vTaskDelayUntil(&last_wake_time, sample_interval);
    }
}