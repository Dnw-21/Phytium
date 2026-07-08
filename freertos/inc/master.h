#ifndef __MASTER_H
#define __MASTER_H

#include "FreeRTOS.h"
#include "task.h"
#include "data_frame.h"

#define CMD_REQUEST_WAVEFORM          0x10
#define CMD_CLEAR_FLASH               0x12
#define CMD_POLL_STATUS               0x14
#define CMD_REQUEST_FAULT_DATA        0x15

#define MASTER_MAX_NODES            3

#define MASTER_NODE_TIMEOUT_MS      15000
#define MASTER_JUDGE_INTERVAL_MS    1000

#define MASTER_POLL_CYCLE_MS        5000
#define MASTER_POLL_CYCLE_FAST_MS   3000
#define MASTER_POLL_MAX_NODES       3

#define MASTER_FLASH_PER_NODE       0x5000

typedef struct {
    uint8_t      node_id;
    uint8_t      is_online;
    uint8_t      has_status_data;
    uint8_t      severity;
    FaultType_t  fault_type;
    uint8_t      fault_pending;
    uint32_t     last_recv_time;
    uint32_t     fault_count;

    uint16_t     last_total_points;
    uint16_t     last_sample_rate;
    float        last_health_score;
    uint32_t     last_status_timestamp;
    FaultType_t  last_status_fault;
} MasterNodeInfo_t;

typedef struct {
    uint8_t  active;
    uint8_t  node_id;
    uint8_t  data_type;
    uint16_t expected_points;
    uint16_t received_points;
    uint32_t sample_rate;
    uint8_t  severity;
    uint8_t  flash_save_pending;
    NodeSample_t node_buffer[MASTER_NODE_UPLOAD_POINTS];

    uint8_t  recv_started;
    uint16_t recv_raw_points;
    uint16_t recv_expected_points;
} MasterDownloadBuf_t;

#define MASTER_RECV_TASK_PRIO       5
#define MASTER_PROCESS_TASK_PRIO    4
#define MASTER_JUDGE_TASK_PRIO      3
#define MASTER_POLL_TASK_PRIO       2

#define MASTER_RECV_STK_SIZE        1024
#define MASTER_PROCESS_STK_SIZE     1024
#define MASTER_JUDGE_STK_SIZE       256
#define MASTER_POLL_STK_SIZE        512

void master_init(void);

void master_recv_task(void *pvParameters);
void master_process_task(void *pvParameters);
void master_judge_task(void *pvParameters);
void master_poll_task(void *pvParameters);

void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len);

MasterNodeInfo_t *master_get_node_info(uint8_t node_id);

void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count);
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count);
void master_flash_erase_node(uint8_t node_id);

MasterDownloadBuf_t *master_get_download_buf(void);

uint16_t lora_uart_get_rx_count(void);
void lora_uart_mark_frame(void);
uint16_t lora_uart_read_frame(uint8_t *buf, uint16_t max_len);

#endif