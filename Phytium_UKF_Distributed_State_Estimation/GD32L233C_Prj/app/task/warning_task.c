#include "tasks.h"
#include "gd32l233r_eval.h"
#include "data_monitor.h"
#include "health_assessment.h"
#include "log.h"
#include "wave_monitor.h"

/*============================================================================
 * warning_task: 预警模式管理 - 恢复检测 / 升级为紧急
 *============================================================================*/
void warning_task(void *pvParameters)
{
    EventBits_t event_bits;
    uint32_t start_time;
    uint32_t current_time;

    log_info("Warning task started");

    while (1) {
        event_bits = xEventGroupWaitBits(g_event_group, EVENT_WARNING_BIT,
                                          pdTRUE, pdFALSE, portMAX_DELAY);

        if (event_bits & EVENT_WARNING_BIT) {
            log_warn("Warning mode activated");
            gd_eval_led_on(LED1);
            start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

            while (get_system_mode() == MODE_WARNING) {
                current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

                event_bits = xEventGroupWaitBits(g_event_group,
                    EVENT_SWITCH_NORMAL | EVENT_DANGER_BIT,
                    pdTRUE, pdFALSE, 0);

                if (event_bits & EVENT_SWITCH_NORMAL) {           /* 恢复正常 */
                    log_info("Recovering to normal mode");
                    break;
                }

                if (event_bits & EVENT_DANGER_BIT) {              /* 升级为紧急 */
                    log_warn("Escalating to DANGER mode");
                    break;
                }

                float score = health_get_score();      /* 健康恢复检测 */
                if (score >= HEALTH_SCORE_NORMAL) {
                    uint32_t elapsed = current_time - start_time;
                    if (elapsed > WARNING_RECOVER_TIME * 1000) {  /* 持续健康则恢复 */
                        switch_to_normal_mode();
                        log_info("Health recovered, switching to normal");
                    }
                }

                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            gd_eval_led_off(LED1);
            log_info("Warning mode deactivated");
        }
    }
}