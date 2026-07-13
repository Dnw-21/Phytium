#include "gd32l23x.h"
#include "systick.h"
#include <stdio.h>
#include "main.h"
#include "tasks.h"

#include "gd32l233r_eval.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "log.h"
#include "data_monitor.h"
#include "health_assessment.h"
#include "chaos_encrypt.h"
#include "battery_monitor.h"
#include "gps.h"

/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    gd_eval_led_init(LED1);
    gd_eval_led_init(LED2);

    usart0_init();

    log_init(LOG_LEVEL_DEBUG);
    printf("System initialized");
	
    LoRa_Init();     // LoRa模块初始化
    log_info("LoRa module configured");

    /* GPS 模块初始化 */
    gps_init();
    log_info("GPS module initialized");

    /* 电池电压模块初始化 */
    battery_monitor_init();
    battery_baseline_init();
    log_info("Battery monitor initialized");

    gd_eval_led_on(LED1);
    gd_eval_led_on(LED2);

    /* 创建队列 */
    g_sampledata_queue = xQueueCreate(SAMPLE_DATA_QUEUE_LEN, sizeof(SensorData_t));
    g_lora_send_queue = xQueueCreate(LORA_SEND_QUEUE_LEN, sizeof(LoRaSendPacket_t));
    g_lora_recv_queue = xQueueCreate(LORA_RECV_QUEUE_LEN, sizeof(LoRaRecvPacket_t));
    
    /* 初始化数据监测模块 */
    data_monitor_init();
    log_info("Data monitor initialized");
    
    /* 初始化健康度评价模块 */
    health_assessment_init();
    log_info("Health assessment initialized");
    
    /* 初始化混沌加密模块 */
    chaos_init(0x12345678);
    log_info("Chaos encryption initialized");

    /* 创建事件组 */
    g_event_group = xEventGroupCreate();    //事件组 

    /* 创建互斥锁 */
    g_mode_mutex = xSemaphoreCreateMutex();    //互斥锁

    /* 创建任务 */
    xTaskCreate(data_collect_task, "data_collect", DATA_COLLECT_STK_SIZE, NULL, DATA_COLLECT_TASK_PRIO, NULL);
    xTaskCreate(data_judge_task, "data_judge", DATA_JUDGE_STK_SIZE, NULL, DATA_JUDGE_TASK_PRIO, NULL);
    xTaskCreate(lora_send_task, "lora_send", LORA_SEND_STK_SIZE, NULL, LORA_SEND_TASK_PRIO, NULL);
    xTaskCreate(lora_recv_task, "lora_recv", LORA_RECV_STK_SIZE, NULL, LORA_RECV_TASK_PRIO, NULL);
    xTaskCreate(warning_task, "warning", WARNING_STK_SIZE, NULL, WARNING_TASK_PRIO, NULL);
    xTaskCreate(danger_task, "danger", DANGER_STK_SIZE, NULL, DANGER_TASK_PRIO, NULL);
    
    vTaskStartScheduler();

    while(1) {
    }
}

/* Stack overflow hook. */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    (void)xTask;
    log_error("STACK OVERFLOW: %s", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;);
}