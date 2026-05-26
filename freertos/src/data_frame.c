#include "data_frame.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* UART2 PL011 基址 (与 main.c 一致, LoRa 模块连接到此串口) */
#define U2_BASE     0x2800E000UL
#define U2_FR       (*(volatile uint32_t *)(U2_BASE + 0x18))
#define U2_DR       (*(volatile uint32_t *)(U2_BASE + 0x00))
#define U2_FR_TXFF  (1U << 5)   /* TX FIFO full */
#define U2_FR_BUSY  (1U << 3)   /* UART busy */

/* UART2 TX 单字节 (阻塞, 与 main.c AT_SEND 宏逻辑一致) */
static void uart2_tx_byte(uint8_t b)
{
    while (U2_FR & U2_FR_TXFF);   /* 等 TX FIFO 不满 */
    U2_DR = (uint32_t)b;
}

static void uart2_tx_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        uart2_tx_byte(data[i]);
    while (U2_FR & U2_FR_BUSY);   /* 等发送完成 */
}

/*
 *  解析 LoRa 帧: 检查同步码、数据类型、加密长度、加密起始位置
 */
void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result)
{
    int incomplete = 0;
    /* 记录最大的合法帧 (real frames are larger than false AA55 patterns) */
    int      best_i      = -1;
    uint16_t best_len    = 0;  /* frame_data_len */
    int      best_tail   = 0;

    for (int i = 0; i < (int)raw_len - 1; i++) {
        if (raw_pkt[i] == 0xAA && raw_pkt[i + 1] == 0x55) {
            if (i + 4 > raw_len) break;
            uint16_t frame_data_len = ((uint16_t)raw_pkt[i + 2] << 8) | raw_pkt[i + 3];

            if (frame_data_len > 250) continue;

            int tail_pos = i + 5 + frame_data_len;

            /* 帧尾超出缓冲区 → 可能是不完整的真实大帧, 停止搜索 */
            if (tail_pos + 2 > (int)raw_len) {
                incomplete = 1;
                break;
            }

            if (raw_pkt[tail_pos] == 0x55 && raw_pkt[tail_pos + 1] == 0xAA) {
                /* 倾向更大的帧 (真帧200B >> 假帧11B) */
                if (frame_data_len > best_len) {
                    best_i    = i;
                    best_len  = frame_data_len;
                    best_tail = tail_pos;
                }
                /* 继续搜索, 可能还有更大的帧 */
            }
        }
    }

    if (best_i >= 0) {
        /* 返回找到的最大帧 */
        const uint8_t *frame = &raw_pkt[best_i];
        result->rx_type   = frame[8];
        result->sync_code = ((uint32_t)frame[9]  << 24) | ((uint32_t)frame[10] << 16)
                          | ((uint32_t)frame[11] << 8)  |  (uint32_t)frame[12];
        result->enc_len   = best_len - 9;
        result->enc_start = (uint8_t *)&frame[13];
        result->consumed  = (uint16_t)(best_tail + 2 - best_i);
    } else if (incomplete) {
        result->consumed = 0;
        result->enc_len  = 0;
        result->rx_type  = 0;
    } else {
        result->sync_code = ((uint32_t)raw_pkt[0] << 24) | ((uint32_t)raw_pkt[1] << 16)
                          | ((uint32_t)raw_pkt[2] << 8)  |  (uint32_t)raw_pkt[3];
        result->rx_type   = raw_pkt[4];
        result->enc_len   = raw_len - 5;
        result->enc_start = (uint8_t *)&raw_pkt[5];
        result->consumed  = raw_len;
    }
}

uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

/*
 *  发送带帧标记的 LoRa 数据包 (Phytium 版: 直接写 UART2)
 *  帧格式: [AA55][len(2B)][ts(4B)][type(1B)][inner_data(NB)][CRC8(1B)][55AA]
 */
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                             LoRaSrc_t *dest, uint8_t retries, uint32_t timestamp)
{
    (void)retries;

    /* 构建 inner_data: [timestamp(4B)][data_type(1B)][data(NB)] */
    uint8_t  inner_data[260];
    uint16_t di = 0;
    inner_data[di++] = (timestamp >> 24) & 0xFF;
    inner_data[di++] = (timestamp >> 16) & 0xFF;
    inner_data[di++] = (timestamp >> 8) & 0xFF;
    inner_data[di++] =  timestamp & 0xFF;
    inner_data[di++] =  data_type;
    memcpy(&inner_data[di], data, len);
    uint16_t data_len = di + len;

    /* 构建完整的 LoRa 帧: [AA55][len(2B)][inner_data(NB)][CRC8(1B)][55AA] */
    uint8_t  frame_buf[280];  /* 前面多3字节放目标地址+信道 */
    uint16_t fi = 0;

    /* MWCC68D 透传模式: 前3字节 = [destAddr_H][destAddr_L][channel] */
    frame_buf[fi++] = (dest->addr >> 8) & 0xFF;
    frame_buf[fi++] =  dest->addr & 0xFF;
    frame_buf[fi++] =  dest->channel;

    frame_buf[fi++] = FRAME_START_0;
    frame_buf[fi++] = FRAME_START_1;
    frame_buf[fi++] = (data_len >> 8) & 0xFF;
    frame_buf[fi++] =  data_len & 0xFF;
    memcpy(&frame_buf[fi], inner_data, data_len);
    fi += data_len;
    frame_buf[fi++] = calc_frame_crc8(inner_data, data_len);
    frame_buf[fi++] = FRAME_END_0;
    frame_buf[fi++] = FRAME_END_1;

    /* 直接写 UART2 TX → LoRa 模块透传发送 */
    uart2_tx_bytes(frame_buf, fi);
}

/*
 *  发送 ACK 确认帧
 */
uint8_t send_ack(uint8_t status)
{
    uint8_t ack_byte = (status == 0) ? 0x00 : 0x01;
    uart2_tx_byte(ack_byte);
    return 0;
}
