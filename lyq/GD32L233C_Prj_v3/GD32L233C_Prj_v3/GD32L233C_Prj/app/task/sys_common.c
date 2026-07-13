#include "tasks.h"
#include "mwcc68_cfg.h"

/* 全局变量定义 */
static SystemMode_t g_system_mode = MODE_NORMAL;
QueueHandle_t g_sampledata_queue = NULL;
QueueHandle_t g_lora_send_queue = NULL;
QueueHandle_t g_lora_recv_queue = NULL;
EventGroupHandle_t g_event_group = NULL;
SemaphoreHandle_t g_mode_mutex = NULL;

/* 设置系统模式 */
void set_system_mode(SystemMode_t new_mode)
{
    if(xSemaphoreTake(g_mode_mutex, portMAX_DELAY) == pdTRUE) {
        g_system_mode = new_mode;
        xSemaphoreGive(g_mode_mutex);
    }
}

/* 获取系统模式 */
SystemMode_t get_system_mode(void)
{
    SystemMode_t mode = MODE_NORMAL;
    if(xSemaphoreTake(g_mode_mutex, portMAX_DELAY) == pdTRUE) {
        mode = g_system_mode;
        xSemaphoreGive(g_mode_mutex);
    }
    return mode;
}

