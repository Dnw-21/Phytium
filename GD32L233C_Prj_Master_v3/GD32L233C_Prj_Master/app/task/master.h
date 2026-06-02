#ifndef __MASTER_H
#define __MASTER_H

#include "FreeRTOS.h"
#include "task.h"
#include "data_frame.h"

/*============================================================================
 *  命令码定义 (与终端 tasks.h 公用)
 *============================================================================*/
#define CMD_REQUEST_WAVEFORM          0x10    /* 请求上传指定节点/故障的已录波形 */
#define CMD_POLL_STATUS               0x14    /* 主控轮询: 带时间戳下发, 终端上传1周期 */
#define CMD_CLEAR_FLASH               0x12    /* 清除 Flash 波形区 */
#define CMD_REQUEST_FAULT_DATA        0x15    /* 请求上传待处理的故障快照数据 */
/*============================================================================
 *  主控节点管理常量
 *============================================================================*/
#define MASTER_MAX_NODES            3      /* 最大监控终端数 */

#define MASTER_NODE_TIMEOUT_MS      15000   /* 节点超时: 15秒无数据视为离线 */
#define MASTER_JUDGE_INTERVAL_MS    1000    /* 健康判断周期: 1秒 */

/*============================================================================
 *  轮询常量 (正常1周期=20点, 故障2周期=40点)
 *  Tier1: 状态轮   Tier2: 故障Flash上传
 *============================================================================*/
#define MASTER_POLL_CYCLE_MS        5000    /* 轮询周期: 5秒 */
#define MASTER_POLL_CYCLE_FAST_MS   3000    /* 轮询周期: 1秒 (有故障时加速) */
#define MASTER_POLL_MAX_NODES       3       /* 实际轮询节点数 (0/1/2) */

/*============================================================================
 *  状态数据: 故障2周期=40点×52B=2080B/节点 → 内部Flash存储
 *  RAM: 下载缓冲区(40点=2080B), 接收完毕后写入Flash
 *============================================================================*/
#define MASTER_STATUS_FLASH_BASE    0x08030000
#define MASTER_FLASH_PER_NODE       0x5000      /* 20KB/节点 (页对齐) */

/*============================================================================
 *  节点状态快照 (RAM中常驻, 每节点 ~30B, 10节点 = 400B)
 *============================================================================*/
typedef struct {
    uint8_t      node_id;
    uint8_t      is_online;
    uint8_t      has_status_data;      /* Flash中有有效10周期数据 */
    uint8_t      severity;
    FaultType_t  fault_type;
    uint8_t      fault_pending;      /* 是否有待上传的故障快照 (1=有) */
    uint32_t     last_recv_time;
    uint32_t     fault_count;   /* 故障次数 */

    /* 最近一次状态数据头 (1周期/2周期) */
    uint16_t     last_total_points;
    uint16_t     last_sample_rate;
    float        last_health_score;
    uint32_t     last_status_timestamp;
    FaultType_t  last_status_fault;
} MasterNodeInfo_t;

/*============================================================================
 *  当前正在接收的节点, 接收完毕后写入Flash
 *============================================================================*/
typedef struct {
    uint8_t  active;              /* 是否有正在进行的下载 0=无 1=有 */
    uint8_t  node_id;             /* 当前接收的节点 */
    uint8_t  data_type;           /* 当前DT: DATA_TYPE_NODE_HEAD */
    uint16_t expected_points;     /* 期望的总点数 */
    uint16_t received_points;     /* 已接收点数 */
    uint32_t sample_rate;
    uint8_t  severity;
    uint8_t  flash_save_pending;  /* 1 = 延迟: 保存节点数据到Flash */
    NodeSample_t node_buffer[MASTER_NODE_UPLOAD_POINTS];  /* 40点=2080B样本缓冲区 */

    uint8_t  recv_started;            /* recv任务: 已收到当前命令的首个header包 */
    uint16_t recv_raw_points;         /* recv任务: 帧解析后累计的原始数据点数 */
    uint16_t recv_expected_points;    /* recv任务: 预期总点数 (由send_lora_cmd设定; 故障时由process更新) */
} MasterDownloadBuf_t;

/*============================================================================
 *  任务优先级与堆栈
 *============================================================================*/
#define MASTER_RECV_TASK_PRIO       5       /* 仅接收+入队，最高优先级防止丢帧 */
#define MASTER_PROCESS_TASK_PRIO    4       /* 解密+处理，与原recv同级 */
#define MASTER_JUDGE_TASK_PRIO      3       /* 遍历快，略低 */
#define MASTER_POLL_TASK_PRIO       2       /* 轮询调度 */

#define MASTER_RECV_STK_SIZE        256
#define MASTER_PROCESS_STK_SIZE     512
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
void master_process_task(void *pvParameters);
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

/*============================================================================
 *  新: Flash 存储访问接口
 *============================================================================*/
void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count);
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count);
void master_flash_erase_node(uint8_t node_id);

MasterDownloadBuf_t *master_get_download_buf(void);

/*============================================================================
 *  获取指定节点状态专用接口
 *============================================================================*/
#endif /* __MASTER_H */