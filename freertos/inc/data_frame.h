#ifndef __LORA_FRAME_H
#define __LORA_FRAME_H

#include <stdint.h>
#include "chaos_encrypt.h"

#define FRAME_START_0           0xAA
#define FRAME_START_1           0x55
#define FRAME_END_0             0x55
#define FRAME_END_1             0xAA
#define FRAME_OVERHEAD          7

typedef struct {
    uint16_t addr;
    uint8_t  channel;
} LoRaSrc_t;

typedef enum {
    DATA_TYPE_NODE_HEAD  = 0x01,   // 正常节点状态头 (NodeUploadHeader_t)
    DATA_TYPE_NODE_RAW   = 0x02,   // 节点原始数据包 (NodeSample_t)
    DATA_TYPE_FAULT_HEAD = 0x03,   // 故障快照头 (NodeUploadHeader_t)
} DataType_t;

typedef struct {
    uint8_t  rx_type;
    uint8_t  sync_code[CHAOS_SYNC_SIZE];
    uint16_t enc_len;
    uint8_t *enc_start;
    uint16_t consumed;     /* 这个帧消耗了多少字节 (用于多帧连续解析) */
} FrameParseResult_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_OVER_VOLTAGE,
    FAULT_UNDER_VOLTAGE,
    FAULT_VOLTAGE_SAG,
    FAULT_VOLTAGE_SWELL,
    FAULT_TRANSIENT
} FaultType_t;

typedef enum {
    SEVERITY_NORMAL = 0,
    SEVERITY_WARNING,
    SEVERITY_DANGER
} SeverityLevel_t;

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

typedef struct {
    uint8_t  data_type;
    uint8_t  severity;
    uint8_t  fault_type;
    uint8_t  fault_pending;
    uint8_t  node_index;
    uint32_t timestamp;
    uint16_t sample_rate;
    uint16_t total_points;
    float    health_score;
} NodeUploadHeader_t;

#define NODE_SAMPLE_RATE        1000
#define SAMPLES_PER_CYCLE       20
#define NORMAL_UPLOAD_POINTS    (SAMPLES_PER_CYCLE * 1)
#define FAULT_UPLOAD_POINTS     (SAMPLES_PER_CYCLE * 2)
#define MASTER_NODE_UPLOAD_POINTS FAULT_UPLOAD_POINTS

#define TIER1_TIMEOUT_MS            3000
#define TIER2_TIMEOUT_MS            6000

uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);
void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result);
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                             LoRaSrc_t *dest, uint8_t retries, uint32_t timestamp);
uint8_t send_ack(uint8_t status, uint8_t node_id);

#endif