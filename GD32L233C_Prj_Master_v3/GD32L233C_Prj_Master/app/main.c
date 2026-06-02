#include "main.h"
#include "gd32l23x_it.h"
#include "systick.h"
#include "gd32l233r_eval.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "data_frame.h"
#include "master.h"
#include "log.h"
#include "chaos_encrypt.h"
#include "tasks.h"
#include "rtc_time.h"
#include <stdio.h>

QueueHandle_t g_recv_queue = NULL;

void task_create(void)
{
    BaseType_t ret;

    g_recv_queue = xQueueCreate(RECV_QUEUE_LENGTH, sizeof(RecvPacket_t));
    if (g_recv_queue == NULL) {
        log_error("Failed to create recv queue");
        return;
    }

    ret = xTaskCreate(master_recv_task,
                      "MasterRecv",
                      MASTER_RECV_STK_SIZE,
                      NULL,
                      MASTER_RECV_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_recv_task");

    ret = xTaskCreate(master_process_task,
                      "MasterProc",
                      MASTER_PROCESS_STK_SIZE,
                      NULL,
                      MASTER_PROCESS_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_process_task");

    ret = xTaskCreate(master_judge_task,
                      "MasterJudge",
                      MASTER_JUDGE_STK_SIZE,
                      NULL,
                      MASTER_JUDGE_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_judge_task");

    ret = xTaskCreate(master_poll_task,
                      "MasterPoll",
                      MASTER_POLL_STK_SIZE,
                      NULL,
                      MASTER_POLL_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_poll_task");
}

int main(void)
{
    gd_eval_led_init(LED1);
    gd_eval_led_init(LED2);

    usart0_init();

    printf("=== GD32L233C Master Controller ===");

    log_info("Build: %s %s", __DATE__, __TIME__);

    LoRa_Init();     // LoRa模块初始化
    log_info("LoRa module initialized");

    /* 初始化混沌加密模块 */
    chaos_init(0x12345678);
    log_info("Chaos encryption initialized");

    master_init();

    // rtc_time_init();
    log_info("RTC time service initialized");

    task_create();

    log_info("FreeRTOS scheduler starting...");
    vTaskStartScheduler();

    while (1);
}