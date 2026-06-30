#ifndef __LORA_FRAME_H
#define __LORA_FRAME_H

#include <stdint.h>

/*====================================================================
 *  帧格式定义 (LoRa空中帧)
 *  [0xAA 0x55][len 2B][data nB][CRC8 1B][0x55 0xAA]
 *  data段内部结构:
 *    [timestamp 4B][data_type 1B][sync_code 8B][payload mB]
 *
 *  帧开销: 7 字节 (2头 + 2长度 + 1CRC + 2尾)
 *====================================================================*/
#define FRAME_START_0           0xAA
#define FRAME_START_1           0x55
#define FRAME_END_0             0x55
#define FRAME_END_1             0xAA
#define FRAME_OVERHEAD          7

/*====================================================================
 * 数据类型定义
 *====================================================================*/
typedef enum {
    DATA_TYPE_NODE_HEAD  = 0x01,   // 正常节点状态头 (NodeUploadHeader_t)
    DATA_TYPE_WAVE       = 0x02,   // 波形数据头 (WaveChunkHeader_t)
    DATA_TYPE_POWER      = 0x03,   // 电源电压数据 (预留)
    DATA_TYPE_NODE_RAW   = 0x04,   // 节点原始数据包 (NodeSample_t)
    DATA_TYPE_FLASH_WAVE = 0x05,   // 波形原始数据包 (差分编码)
    DATA_TYPE_FLASH_WAVE_END = 0x06, // 波形传输结束标志 (含总点数)
    DATA_TYPE_FAULT_HEAD = 0x07,   // 故障快照头 (NodeUploadHeader_t)
} DataType_t;

/*====================================================================
 * 定向传输目标地址结构
 *====================================================================*/
typedef struct {
    uint16_t my_addr;      
    uint8_t channel;          
} LoRaSrc_t;

/*====================================================================
 * 帧解析结果结构
 *====================================================================*/
typedef struct {
    uint8_t  rx_type;
    uint64_t sync_code;
    uint16_t enc_len;
    uint8_t *enc_start;
} FrameParseResult_t;

/*====================================================================
 * 发送状态枚举
 *====================================================================*/
typedef enum {
    TX_SUCCESS = 0,    // 发送成功且收到ACK
    TX_TIMEOUT,        // 发送超时
    TX_NO_ACK,         // 未收到ACK
    TX_ERROR           // 发送错误
} TxStatus_t;

/*====================================================================
 * 函数声明
 *====================================================================*/
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type, LoRaSrc_t *dest, uint8_t retries, uint32_t timestamp);
uint8_t send_ack(uint8_t status);
void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result);
uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);
int parse_frame(const uint8_t *raw_data, uint16_t raw_len,
                uint8_t *out_data, uint16_t *out_data_len,
                uint8_t *out_type, uint32_t *out_timestamp);

#endif /* __LORA_FRAME_H */