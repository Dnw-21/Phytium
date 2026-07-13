#ifndef __TASK_H
#define __TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "data_frame.h"
#include "data_monitor.h"

/*============================================================================
 *                              系统模式定义
 *============================================================================*/
typedef enum {
    MODE_NORMAL = 0,
    MODE_WARNING,
    MODE_DANGER
} SystemMode_t;

/*============================================================================
 *                              节点状态数据结构体
 *============================================================================*/
typedef struct {
    ZDataPoint_t zdata;
    uint32_t timestamp;         /* RTOS ms (header用) */
    nmea_utc_time gps_utc;      /* GPS UTC 时间 (采样瞬间捕获) */
    uint16_t gps_ms;            /* 自 GPS UTC 时刻起的 ms 偏移 */
} SensorData_t;

/*============================================================================
 *                              指令结构体
 *============================================================================*/
typedef struct {
    uint8_t cmd_id;
    uint8_t cmd_param[16];
} Command_t;

/*============================================================================
 *                              LoRa数据包结构体
 *============================================================================*/
typedef struct {
    DataType_t data_type;
    uint8_t data_len;
    uint8_t data[240];
    LoRaSrc_t dest;
    uint32_t timestamp;
} LoRaSendPacket_t;

typedef struct {
    DataType_t data_type;
    uint32_t timestamp;
    uint16_t src_addr;
    uint8_t data_len;
    uint8_t data[128];
} LoRaRecvPacket_t;

/*============================================================================
 *                              任务优先级定义
 *============================================================================*/
#define DATA_COLLECT_TASK_PRIO    7
#define LORA_SEND_TASK_PRIO       6
#define DATA_JUDGE_TASK_PRIO      5
#define LORA_RECV_TASK_PRIO       4
#define WARNING_TASK_PRIO         2
#define DANGER_TASK_PRIO          2

/*============================================================================
 *                              任务堆栈大小定义
 *============================================================================*/
#define DATA_COLLECT_STK_SIZE     256         /* 仅采样+压队列, 够用 */
#define DATA_JUDGE_STK_SIZE       256         /* 故障检测+pkt_buf[32]+raw[128] */
#define LORA_SEND_STK_SIZE        512         /* chaos_encrypt + send_with_ack + printf */
#define LORA_RECV_STK_SIZE        512         /* 接收+命令处理 */
#define WARNING_STK_SIZE          128         /* 事件等待, 够用 */
#define DANGER_STK_SIZE           128         /* 事件等待, 够用 */

/*============================================================================
 *                              队列长度定义
 *============================================================================*/
#define SAMPLE_DATA_QUEUE_LEN     20
#define LORA_SEND_QUEUE_LEN       10          
#define LORA_RECV_QUEUE_LEN       3

/*============================================================================
 *                              事件组位定义
 *============================================================================*/
#define EVENT_WARNING_BIT         (1 << 0)
#define EVENT_DANGER_BIT          (1 << 1)
#define EVENT_ACK_RECEIVED        (1 << 2)
#define EVENT_SWITCH_NORMAL       (1 << 3)

/*============================================================================
 *                              恢复时间定义 (秒)
 *============================================================================*/
#define WARNING_RECOVER_TIME          5
#define DANGER_RECOVER_TIME           10

/*============================================================================
 *                              指令码定义
 *============================================================================*/
#define CMD_POLL_STATUS               0x14    /* 主控轮询: 带时间戳下发, 终端上传1周期 */
#define CMD_CLEAR_FLASH               0x12    /* 清除 Flash 波形区 */
#define CMD_REQUEST_FAULT_DATA        0x15    /* 请求上传待处理的故障快照数据 */

/*============================================================================
 *                              外部声明
 *============================================================================*/
extern QueueHandle_t g_sampledata_queue;
extern QueueHandle_t g_lora_send_queue;
extern QueueHandle_t g_lora_recv_queue;
extern EventGroupHandle_t g_event_group;
extern SemaphoreHandle_t g_mode_mutex;

/*============================================================================
 *                              任务函数声明
 *============================================================================*/
void data_collect_task(void *pvParameters);
void data_judge_task(void *pvParameters);
void warning_task(void *pvParameters);
void danger_task(void *pvParameters);
void lora_send_task(void *pvParameters);
void lora_recv_task(void *pvParameters);

/*============================================================================
 *                              公共函数声明
 *============================================================================*/
void set_system_mode(SystemMode_t new_mode);
SystemMode_t get_system_mode(void);

#endif
