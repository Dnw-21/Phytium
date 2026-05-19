#ifndef __MASTER_H
#define __MASTER_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "data_frame.h"

#define CMD_REQUEST_WAVEFORM          0x10
#define CMD_REQUEST_FAULT_LIST        0x11
#define CMD_CLEAR_FLASH               0x12
#define CMD_START_WAVE_COLLECT        0x13

#define MASTER_MAX_NODES            10
#define MASTER_WAVE_MAX_MS          500
#define MASTER_WAVE_RATE_6000       6000
#define MASTER_WAVE_RATE_15000      15000
#define MASTER_WAVE_MAX_SAMPLES     (MASTER_WAVE_RATE_15000 * MASTER_WAVE_MAX_MS / 1000)

#define MASTER_NODE_TIMEOUT_MS      15000
#define MASTER_JUDGE_INTERVAL_MS    1000
#define MASTER_CMD_RETRY_MAX        3

#define MASTER_FLASH_PER_NODE       0x1900

typedef struct {
    uint8_t      node_id;
    uint8_t      is_online;
    uint8_t      has_status_data;
    uint8_t      has_wave_data;
    uint8_t      severity;
    FaultType_t  fault_type;
    uint32_t     last_recv_time;
    uint32_t     fault_count;

    uint8_t      last_status_type;
    uint16_t     last_total_points;
    uint16_t     last_sample_rate;
    float        last_health_score;
    uint32_t     last_status_timestamp;
    FaultType_t  last_status_fault;

    uint8_t      has_last_wave_hdr;
    uint32_t     last_wave_rate;
    uint32_t     last_wave_samples;
    SeverityLevel_t last_wave_severity;

    uint8_t      wave_pending;
    uint8_t      cmd_retry;
} MasterNodeInfo_t;

typedef struct {
    uint8_t  active;
    uint8_t  node_id;
    uint8_t  data_type;
    uint16_t expected_points;
    uint16_t received_points;
    uint32_t sample_rate;
    uint8_t  severity;
    NodeSample_t node_buffer[FAULT_UPLOAD_POINTS];
} MasterDownloadBuf_t;

typedef enum {
    MASTER_CMD_NONE = 0,
    MASTER_CMD_REQ_WAVE,
    MASTER_CMD_REQ_FAULT_LIST,
    MASTER_CMD_CLEAR_FLASH,
    MASTER_CMD_WAVE_COLLECT
} MasterCmdType_t;

typedef enum {
    COLLECT_RATE_6000  = 6000,
    COLLECT_RATE_15000 = 15000
} CollectRate_t;

typedef struct {
    MasterCmdType_t cmd_type;
    uint8_t  node_id;
    uint8_t  fault_idx;
    uint16_t sample_rate;
    uint16_t duration_ms;
} MasterInternalCmd_t;

#define MASTER_RECV_TASK_PRIO       4
#define MASTER_JUDGE_TASK_PRIO      5
#define MASTER_CMD_TASK_PRIO        3

#define MASTER_RECV_STK_SIZE        512
#define MASTER_JUDGE_STK_SIZE       256
#define MASTER_CMD_STK_SIZE         256

#define MASTER_CMD_QUEUE_LEN        5

extern QueueHandle_t g_master_cmd_queue;

void master_init(void);

void master_recv_task(void *pvParameters);
void master_judge_task(void *pvParameters);
void master_cmd_task(void *pvParameters);

void master_lora_rx_ctrl(int enable);
int  master_lora_rx_is_enabled(void);

MasterNodeInfo_t *master_get_node_info(uint8_t node_id);
void master_recv_wave_data(uint8_t node_id, uint16_t count);

void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count);
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count);
void master_flash_erase_node(uint8_t node_id);

void master_flash_save_wave_data(uint8_t node_id, const uint8_t *data, uint16_t len,
                                  uint32_t offset);
uint16_t master_flash_load_wave_data(uint8_t node_id, uint8_t *buf, uint16_t len);
void master_flash_erase_wave(uint8_t node_id);

MasterDownloadBuf_t *master_get_download_buf(void);

int rpmsg_send_lora_recv_log(const uint8_t *raw_data, uint16_t raw_len);

#endif /* __MASTER_H */