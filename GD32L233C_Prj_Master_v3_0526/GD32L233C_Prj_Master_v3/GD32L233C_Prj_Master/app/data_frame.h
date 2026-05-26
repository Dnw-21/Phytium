#ifndef __LORA_FRAME_H
#define __LORA_FRAME_H

#include <stdint.h>

/*====================================================================
 *  帧格式定义 (LoRa空中帧)
 *  [0xAA 0x55][len 2B][data nB][CRC8 1B][0x55 0xAA]
 *  data段内部结构:
 *    [timestamp 4B][data_type 1B][sync_code 4B][data_type 1B][payload mB]
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
    DATA_TYPE_NODE_HEAD  = 0x01,   // 节点状态头 (NodeUploadHeader_t)
    DATA_TYPE_WAVE       = 0x02,   // 波形数据头 (WaveChunkHeader_t)
    DATA_TYPE_POWER      = 0x03,   // 电源电压数据 (预留)
    DATA_TYPE_NODE_RAW   = 0x04,   // 节点原始数据包 (NodeSample_t)
    DATA_TYPE_FLASH_WAVE = 0x05,   // 波形原始数据包 (差分编码)
} DataType_t;

/*====================================================================
 * 帧解析结果结构
 *====================================================================*/
typedef struct {
    uint8_t  rx_type;
    uint32_t sync_code;
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
 * 每个样本 20 字节, 终端以 DATA_TYPE_NODE_RAW 分包发送 (每包10样本=200B)
 *====================================================================*/
typedef struct {
    int16_t  active_power;        /* ×10000 */
    int16_t  reactive_power;      /* ×10000 */
    int16_t  voltage_mag_1;       /* ×10000 */
    int16_t  voltage_mag_4;       /* ×10000 */
    int16_t  voltage_mag_5;       /* ×10000 */
    int16_t  voltage_angle_1;     /* ×10000 */
    int16_t  voltage_angle_4;     /* ×10000 */
    int16_t  voltage_angle_5;     /* ×10000 */
    uint32_t timestamp;
} NodeSample_t;

/*====================================================================
 * NodeUploadHeader_t: 节点状态头 (主控轮询 / 故障触发 统一使用)
 * 后续紧跟 MASTER_NODE_UPLOAD_POINTS 个 NodeSample_t raw数据
 *====================================================================*/
typedef struct {
    uint8_t  data_type;         /* DATA_TYPE_NODE_HEAD */
    uint8_t  severity;          /* 故障级别 */
    uint8_t  fault_type;        /* 故障类型 (FAULT_NONE=正常) */
    uint8_t  node_index;        /* 节点号 (0~9) */
    uint32_t timestamp;         /* 时间戳 (ms) */
    uint16_t sample_rate;       /* 采样率 (1000Hz) */
    uint16_t total_points;      /* 后续raw数据总点数 */
    float    health_score;      /* 健康度 */
} NodeUploadHeader_t;

/*====================================================================
 * 波形数据头 — 终端按主控指令上传已录制的 Flash 波形
 *====================================================================*/
typedef struct {
    uint8_t  data_type;
    uint8_t  node_index;
    uint8_t  severity;
    uint8_t  fault_idx;           /* 故障序号 (0~7), 对应终端的故障记录索引 */
    uint32_t fault_timestamp;
    uint32_t sample_rate;
    uint16_t sample_count;
} WaveChunkHeader_t;

/*====================================================================
 * 常量
 *====================================================================*/
#define NODE_SAMPLE_RATE        1000
#define SAMPLES_PER_CYCLE       20
#define MASTER_NODE_UPLOAD_CYCLES 2   /*终端上传2周期 */
#define MASTER_NODE_UPLOAD_POINTS (SAMPLES_PER_CYCLE * MASTER_NODE_UPLOAD_CYCLES)  /* 40 */

uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);
void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result);
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                                   LoRaSrc_t *dest,
                                   uint8_t retries,
                                   uint32_t timestamp);
uint8_t send_ack(uint8_t status);

#endif /* __LORA_FRAME_H */