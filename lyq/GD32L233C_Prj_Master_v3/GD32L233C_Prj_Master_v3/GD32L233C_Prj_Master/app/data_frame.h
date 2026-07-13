#ifndef __LORA_FRAME_H
#define __LORA_FRAME_H

#include <stdint.h>
#include "chaos_encrypt.h"

/*====================================================================
 *  帧格式定义 (LoRa空中帧)
 *  [0xAA 0x55][len 2B][data nB][CRC8 1B][0x55 0xAA]
 *  data段内部结构:
 *    [timestamp 4B][data_type 1B][sync_code 16B][payload mB]
 *
 *  帧开销: 7 字节 (2头 + 2长度 + 1CRC + 2尾)
 *====================================================================*/
#define FRAME_START_0           0xAA
#define FRAME_START_1           0x55
#define FRAME_END_0             0x55
#define FRAME_END_1             0xAA
#define FRAME_OVERHEAD          7

/*====================================================================
 * 定向传输目标地址结构
 *====================================================================*/
typedef struct {
    uint16_t addr;        // 目标地址
    uint8_t channel;          // 目标信道 (0-83)
} LoRaSrc_t;

/*====================================================================
 * 数据类型定义 (与终端 GD32L233C_Prj 保持一致)
 *====================================================================*/
typedef enum {
    DATA_TYPE_NODE_HEAD  = 0x01,   // 正常节点状态头 (NodeUploadHeader_t)
    DATA_TYPE_NODE_RAW   = 0x02,   // 节点原始数据包 (NodeSample_t)
    DATA_TYPE_FAULT_HEAD = 0x03,   // 故障快照头 (NodeUploadHeader_t)
} DataType_t;

/*====================================================================
 * 帧解析结果结构
 *====================================================================*/
typedef struct {
    uint8_t  rx_type;
    uint8_t  sync_code[CHAOS_SYNC_SIZE];
    uint16_t enc_len;
    uint8_t *enc_start;
} FrameParseResult_t;


/*====================================================================
 * 故障类型枚举 (与终端 data_monitor.h 一致)
 *====================================================================*/
typedef enum {
    FAULT_NONE = 0,
    FAULT_OVER_VOLTAGE,
    FAULT_UNDER_VOLTAGE,
    FAULT_VOLTAGE_SAG,
    FAULT_VOLTAGE_SWELL,
    FAULT_TRANSIENT
} FaultType_t;

/*====================================================================
 * 故障级别 (与终端一致)
 *====================================================================*/
typedef enum {
    SEVERITY_NORMAL = 0,
    SEVERITY_WARNING,
    SEVERITY_DANGER
} SeverityLevel_t;

/*====================================================================
 * NodeSample_t: 节点采样数据 (与终端 data_monitor.h 一致)
 * 每个样本 52 字节
 *====================================================================*/
typedef struct {
    int16_t  pg1;           int16_t  pg2;           int16_t  pg3;
    int16_t  qg1;           int16_t  qg2;           int16_t  qg3;
    int16_t  vmag1;         int16_t  vmag2;         int16_t  vmag3;
    int16_t  vmag4;         int16_t  vmag5;         int16_t  vmag6;
    int16_t  vmag7;         int16_t  vmag8;         int16_t  vmag9;
    int16_t  vangle1;       int16_t  vangle2;       int16_t  vangle3;
    int16_t  vangle4;       int16_t  vangle5;       int16_t  vangle6;
    int16_t  vangle7;       int16_t  vangle8;       int16_t  vangle9;
    uint32_t timestamp;
} NodeSample_t;

/*====================================================================
 * NodeUploadHeader_t: 节点状态头 (主控轮询 / 故障触发 统一使用)
 * 正常轮询: 后续紧跟 NORMAL_UPLOAD_POINTS 个 NodeSample_t 原始数据
 * 故障快照: 后续紧跟 FAULT_UPLOAD_POINTS 个 NodeSample_t 原始数据
 *====================================================================*/
typedef struct {
    uint8_t  data_type;         /* DATA_TYPE_NODE_HEAD */
    uint8_t  severity;          /* 故障级别 */
    uint8_t  fault_type;        /* 故障类型 (FAULT_NONE=正常) */
    uint8_t  fault_pending;     /* 剩余待上传故障条数 */
    uint8_t  node_index;        /* 节点号 (0~9) */
    uint32_t timestamp;         /* 时间戳 (ms) */
    uint16_t sample_rate;       /* 采样率 (1000Hz) */
    uint16_t total_points;      /* 后续raw数据总点数 */
    uint8_t  battery_pct;       /* 电池电量百分比 (0~100, 255=未就绪) */
    float    health_score;      /* 健康度 */
} NodeUploadHeader_t;

/*====================================================================
 * 常量
 *====================================================================*/
#define NODE_SAMPLE_RATE        1000
#define SAMPLES_PER_CYCLE       20
#define NORMAL_UPLOAD_POINTS (SAMPLES_PER_CYCLE * 1)   /* 20: 正常1周期 */
#define FAULT_UPLOAD_POINTS  (SAMPLES_PER_CYCLE * 2)   /* 40: 故障2周期 */
#define MASTER_NODE_UPLOAD_POINTS FAULT_UPLOAD_POINTS  /* 缓冲区大小 = 最大值 */

#define TIER1_TIMEOUT_MS            3000  /* 2秒超时 */
#define TIER2_TIMEOUT_MS            6000  /* 6秒超时 */

uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);
int  frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result);
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                                   LoRaSrc_t *dest,
                                   uint8_t retries,
                                   uint32_t timestamp);
uint8_t send_ack(uint8_t status, uint8_t node_id);

#endif /* __LORA_FRAME_H */