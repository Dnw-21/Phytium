#include "tasks.h"
#include "health_assessment.h"
#include "data_monitor.h"
#include "log.h"

/*============================================================================
 * data_judge_task: 接收数据 → 1kHz节点采样(环缓冲+滑动检测) → 健康评估
 *============================================================================*/
void data_judge_task(void *pvParameters)
{
    SensorData_t sensor;

    log_info("Judge task started");

    while (1) {
        if (xQueueReceive(g_sampledata_queue, &sensor, portMAX_DELAY) == pdPASS) {
            node_sample_process(&sensor.zdata, sensor.timestamp,
                                 &sensor.gps_utc, sensor.gps_ms);
        }
    }
}