#ifndef __MASTER_H
#define __MASTER_H

#include "FreeRTOS.h"
#include "task.h"
#include "data_frame.h"

/*============================================================================
 *  命令码定义 (与终端 tasks.h 公用)
 *============================================================================*/
#define CMD_REQUEST_WAVEFORM          0x10    /* 请求上传已录波形 (Tier2) */
#define CMD_POLL_STATUS               0x14    /* 主控轮询: 带时间戳下发 (Tier1) */

/*============================================================================
 *  主控节点管理常量
 *============================================================================*/
#define MASTER_MAX_NODES            3       /* 最大监控终端数 */
#define MASTER_WAVE_MAX_MS          500     /* 波形采集最大时长(ms) */
#define MASTER_WAVE_RATE_6000       12000   /* 采集采样率: 12kHz */
#define MASTER_WAVE_RATE_15000      15000   /* 采集采样率: 15kHz */
#define MASTER_WAVE_MAX_SAMPLES     (MASTER_WAVE_RATE_15000 * MASTER_WAVE_MAX_MS / 1000)

#define MASTER_NODE_TIMEOUT_MS      15000   /* 节点超时: 15秒无数据视为离线 */
#define MASTER_JUDGE_INTERVAL_MS    1000    /* 健康判断周期: 1秒 */

/*============================================================================
 *  轮询常量 (终端2周期=40点, 62.5Kbps, 差分编码220B/包)
 *  Tier1: 状态轮   Tier2: 波形轮 (按需)
 *  DANGER 6750点: 31包≈900ms | WARNING 3000点: 14包≈400ms
 *============================================================================*/
#define MASTER_POLL_CYCLE_MS        15000   /* 轮询周期: 15秒 */
#define MASTER_POLL_CYCLE_FAST_MS   12000   /* 轮询周期: 12秒 (有故障时加速) */
#define MASTER_POLL_NODE_TIMEOUT_MS 1000    /* 单节点状态上传超时 */
#define MASTER_POLL_WAVE_TIMEOUT_MS 1000    /* 单节点波形上传超时 (含突变余量) */
#define MASTER_POLL_MAX_NODES       3       /* 实际轮询节点数 (0/1/2) */
#define MASTER_POLL_TIER2_BUDGET_MS 3000    /* Tier2 波形轮时间预算 (3×1s) */

/*============================================================================
 *  每节点2周期状态数据: 40点 × 20B = 800B/节点
 *  SRAM放不下 -> 共享内存分区存储, 状态数据与波形数据完全分离:
 *  RAM 中仅保留下载缓冲区(40点=800B), 接收完毕后写入共享内存
 *============================================================================*/
#define MASTER_FLASH_PER_NODE       0x5000      /* 20KB/节点 (页对齐) */

/*============================================================================
 *  节点状态快照 (RAM中常驻, 每节点 ~42B)
 *============================================================================*/
typedef struct {
    uint8_t      node_id;
    uint8_t      is_online;
    uint8_t      has_status_data;      /* 共享内存中有有效状态数据 */
    uint8_t      has_wave_data;        /* 共享内存中有有效波形数据 */
    uint8_t      severity;
    FaultType_t  fault_type;
    uint32_t     last_recv_time;
    uint32_t     fault_count;

    /* 最近一次状态数据头 (2周期) */
    uint16_t     last_total_points;
    uint16_t     last_sample_rate;
    float        last_health_score;
    uint32_t     last_status_timestamp;
    FaultType_t  last_status_fault;

    /* 最近一次波形数据头 */
    uint8_t      has_last_wave_hdr;
    uint32_t     last_wave_rate;
    uint32_t     last_wave_samples;
    SeverityLevel_t last_wave_severity;
    uint8_t      last_wave_fault_idx;  /* 已下载到第几个故障波形 */

    /* 指令状态 */
    uint8_t      wave_pending;
    uint8_t      cmd_retry;
} MasterNodeInfo_t;

/*============================================================================
 *  当前正在接收的节点, 接收完毕后写入共享内存
 *============================================================================*/
typedef struct {
    uint8_t  active;              /* 是否有正在进行的下载 0=无 1=有 */
    uint8_t  node_id;             /* 当前接收的节点 */
    uint8_t  data_type;           /* 当前DT: DATA_TYPE_NODE_HEAD或DATA_TYPE_WAVE */
    uint16_t expected_points;     /* 期望的总点数 */
    uint16_t received_points;     /* 已接收点数 */
    uint32_t sample_rate;
    uint8_t  severity;
    uint8_t  flash_save_pending;  /* 1 = 延迟: 保存节点数据到共享内存 */
    uint8_t  flash_erase_pending; /* 1 = 延迟: 擦除波形共享内存区 */
    uint8_t  flash_wave_pending;  /* 1 = 延迟: 写入波形数据块 */
    uint8_t  flash_wave_done;     /* 1 = 延迟: 波形接收完毕 */
    uint16_t wave_byte_offset;    /* 延迟波形写入的偏移 */
    uint16_t wave_chunk_len;      /* wave_chunk 中有效字节数 */
    uint8_t  wave_chunk[220];     /* 延迟波形写入临时缓冲区 */
    NodeSample_t node_buffer[MASTER_NODE_UPLOAD_POINTS];  /* 40点样本缓冲区 */
} MasterDownloadBuf_t;

/*============================================================================
 *  任务优先级与堆栈
 *============================================================================*/
#define MASTER_RECV_TASK_PRIO       4       /* 接收优先于指令发送, 避免饥饿 */
#define MASTER_JUDGE_TASK_PRIO      5       /* 最高: 遍历极快 */
#define MASTER_POLL_TASK_PRIO       3       /* 轮询调度 */

#define MASTER_RECV_STK_SIZE        512
#define MASTER_JUDGE_STK_SIZE       256
#define MASTER_POLL_STK_SIZE        512

/*============================================================================
 *  初始化函数
 *============================================================================*/
void master_init(void);

/*============================================================================
 *  任务函数
 *============================================================================*/
void master_recv_task(void *pvParameters);
void master_judge_task(void *pvParameters);
void master_poll_task(void *pvParameters);

/*============================================================================
 *  LoRa 命令发送 (供轮询任务直接调用)
 *============================================================================*/
void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len);

/*============================================================================
 *  公共接口
 *============================================================================*/
MasterNodeInfo_t *master_get_node_info(uint8_t node_id);
void master_recv_wave_data(uint8_t node_id, uint16_t count);
MasterDownloadBuf_t *master_get_download_buf(void);

/*============================================================================
 *  共享内存存储访问接口 (模拟Flash)
 *============================================================================*/
void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count);
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count);
void master_flash_erase_node(uint8_t node_id);

void master_flash_save_wave_data(uint8_t node_id, const uint8_t *data, uint16_t len,
                                  uint32_t offset);
uint16_t master_flash_load_wave_data(uint8_t node_id, uint8_t *buf, uint16_t len);
void master_flash_erase_wave(uint8_t node_id);

/*============================================================================
 *  帧处理函数 (由 master_recv.c 实现, main.c 调用)
 *============================================================================*/
void process_node_header(const uint8_t *payload, uint16_t len,
                         MasterDownloadBuf_t *dl, MasterNodeInfo_t *node);
void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                      MasterNodeInfo_t *node);
void process_wave_header(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                         MasterNodeInfo_t *node, uint16_t src_addr);
void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                        MasterNodeInfo_t *node);

/*============================================================================
 *  Phytium 平台: 接收接口 (由 lora_uart.c 提供, master_recv_task 使用)
 *============================================================================*/
uint16_t lora_uart_get_rx_count(void);
void lora_uart_mark_frame(void);
uint16_t lora_uart_read_frame(uint8_t *buf, uint16_t max_len);

#endif /* __MASTER_H */
