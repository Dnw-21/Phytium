#include "tasks.h"
#include "gd32l233r_eval.h"
#include "data_monitor.h"
#include "health_assessment.h"
#include "log.h"
#include "wave_monitor.h"

#define POWER_SWITCH_GPIO    GPIOC
#define POWER_SWITCH_PIN     GPIO_PIN_0

static uint8_t g_power_switched = 0;

static void power_switch_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(POWER_SWITCH_GPIO, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, POWER_SWITCH_PIN);
    gpio_output_options_set(POWER_SWITCH_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, POWER_SWITCH_PIN);
    gpio_bit_reset(POWER_SWITCH_GPIO, POWER_SWITCH_PIN);
}

static void switch_to_backup_power(void)
{
    gpio_bit_set(POWER_SWITCH_GPIO, POWER_SWITCH_PIN);
    g_power_switched = 1;
    log_warn("Switched to backup power");
}

static void restore_main_power(void)
{
    gpio_bit_reset(POWER_SWITCH_GPIO, POWER_SWITCH_PIN);
    g_power_switched = 0;
    log_info("Restored main power");
}

/*============================================================================
 * danger_task: 紧急模式管理 - 电源切换 / 恢复检测 / LED指示
 *============================================================================*/
void danger_task(void *pvParameters)
{
    EventBits_t event_bits;
    uint32_t start_time;
    uint32_t current_time;
    static uint8_t power_init = 0;

    if (!power_init) {
        power_switch_init();
        power_init = 1;
    }

    log_info("Danger task started");

    while (1) {
        event_bits = xEventGroupWaitBits(g_event_group, EVENT_DANGER_BIT,
                                          pdTRUE, pdFALSE, portMAX_DELAY);

        if (event_bits & EVENT_DANGER_BIT) {
            log_error("Danger mode activated!");
            start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

            while (get_system_mode() == MODE_DANGER) {
                current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

                gd_eval_led_on(LED2);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                gd_eval_led_off(LED2);
                vTaskDelay(100 / portTICK_PERIOD_MS);

                event_bits = xEventGroupWaitBits(g_event_group,
                    EVENT_SWITCH_NORMAL, pdTRUE, pdFALSE, 0);

                if (event_bits & EVENT_SWITCH_NORMAL) {           /* 恢复正常 */
                    log_info("Recovering from danger mode");
                    break;
                }

                float score = health_get_score();      /* 健康恢复检测 */
                if (score >= HEALTH_SCORE_NORMAL) {
                    uint32_t elapsed = current_time - start_time;
                    if (elapsed > DANGER_RECOVER_TIME * 1000) {  /* 持续健康则恢复 */
                        switch_to_normal_mode();
                        log_info("Health recovered, switching to normal");
                    }
                } else if (!g_power_switched) {                   /* 健康过低切备用电源 */
                    if (score < HEALTH_SCORE_NORMAL) {
                        switch_to_backup_power();
                    }
                }

                vTaskDelay(50 / portTICK_PERIOD_MS);
            }

            if (g_power_switched) {
                restore_main_power();
            }

            gd_eval_led_off(LED2);
            switch_to_normal_mode();
            log_info("Danger mode deactivated");
        }
    }
}