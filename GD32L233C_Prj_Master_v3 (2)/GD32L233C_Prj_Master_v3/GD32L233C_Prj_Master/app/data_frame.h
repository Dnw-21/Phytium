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
    uint16_t my_addr;        // 目标地址
    uint8_t channel;          // 目标信道 (0-83)
} LoRaSrc_t;

/*====================================================================
 * 数据类型定义 (与终端 GD32L233C_Prj 保持一致)
 *====================================================================*/
typedef enum {
    DATA_TYPE_STATUS     = 0x01,   // 正常节点状态头 (NodeUploadData_t)
    DATA_TYPE_WAVE       = 0x02,   // 波形数据头 (WaveChunkHeader_t)
    DATA_TYPE_POWER      = 0x03,   // 电源电压数据 (预留)
    DATA_TYPE_NODE_RAW   = 0x04,   // 节点原始数据包 (int32×4, 来自环缓冲)
    DATA_TYPE_FLASH_WAVE = 0x05,   // 波形原始数据包 (int16, 来自Flash存储)
    DATA_TYPE_FAULT_LIST = 0x06,   // 故障记录列表 (故障有效性标志数组)
    DATA_TYPE_FAULT_HEAD = 0x07    // 故障节点状态头 (FaultUploadHeader_t)
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
 * NodeSample_t: 节点采样数据 (与终端 data_monitor.h 一致, int32 x10000)
 * 每个样本 16 字节, 终端以 DATA_TYPE_NODE_RAW 分包发送 (每包最多8样本=128B)
 *====================================================================*/
typedef struct {
    int32_t active_power;
    int32_t reactive_power;
    int32_t voltage_angle;
    int32_t voltage_mag;
} NodeSample_t;

/*====================================================================
 * 周期上传节点头 — 终端在正常/预警/紧急模式下发送
 *====================================================================*/
typedef struct {
    uint8_t  data_type;
    uint8_t  severity;
    uint8_t  node_index;
    uint16_t sample_rate;
    float    health_score;
    uint16_t total_points;
} NodeUploadData_t;

/*====================================================================
 * 故障上传头 — 终端检测到故障时发送
 *====================================================================*/
typedef struct {
    uint8_t      data_type;
    uint8_t      severity;
    uint32_t     timestamp;
    FaultType_t  fault_type;
    uint8_t      node_index;
    uint16_t     total_points;
    uint16_t     sample_rate;
} FaultUploadHeader_t;

/*====================================================================
 * 波形数据头 — 终端按主控指令上传已录制的 Flash 波形
 *====================================================================*/
typedef struct {
    uint8_t  data_type;
    uint8_t  node_index;
    uint8_t  severity;
    uint32_t fault_timestamp;
    uint32_t sample_rate;
    uint16_t sample_count;
} WaveChunkHeader_t;

/*====================================================================
 * 常量
 *====================================================================*/
#define NODE_SAMPLE_RATE        2000
#define SAMPLES_PER_CYCLE       40
#define FAULT_UPLOAD_CYCLES     2   //每次上传的节点状态周期数 
#define FAULT_UPLOAD_POINTS     (SAMPLES_PER_CYCLE * FAULT_UPLOAD_CYCLES)  /* 80 */

uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);
void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result);
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                                   LoRaSrc_t *dest,
                                   uint8_t retries,
                                   uint32_t timestamp);
uint8_t send_ack(uint8_t status);

#endif /* __LORA_FRAME_H */