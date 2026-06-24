#include "data_frame.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define U2_BASE     0x2800E000UL
#define U2_FR       (*(volatile uint32_t *)(U2_BASE + 0x18))
#define U2_DR       (*(volatile uint32_t *)(U2_BASE + 0x00))
#define U2_FR_TXFF  (1U << 5)
#define U2_FR_BUSY  (1U << 3)

static void uart2_tx_byte(uint8_t b)
{
    while (U2_FR & U2_FR_TXFF);
    U2_DR = (uint32_t)b;
}

static void uart2_tx_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        uart2_tx_byte(data[i]);
    while (U2_FR & U2_FR_BUSY);
}

void frame_parse(const uint8_t *raw_pkt, uint16_t raw_len, FrameParseResult_t *result)
{
    int frame_found = 0;
    for (int i = 0; i < (int)raw_len - 1; i++) {
        if (raw_pkt[i] == 0xAA && raw_pkt[i + 1] == 0x55) {
            if (i + 4 > raw_len) break;
            uint16_t frame_data_len = ((uint16_t)raw_pkt[i + 2] << 8) | raw_pkt[i + 3];
            int tail_pos = i + 5 + frame_data_len;
            if (tail_pos + 2 <= raw_len &&
                raw_pkt[tail_pos] == 0x55 && raw_pkt[tail_pos + 1] == 0xAA) {
                const uint8_t *frame = &raw_pkt[i];
                result->rx_type   = frame[8];
                result->sync_code = ((uint64_t)frame[9]  << 56) | ((uint64_t)frame[10] << 48)
                                  | ((uint64_t)frame[11] << 40) | ((uint64_t)frame[12] << 32)
                                  | ((uint64_t)frame[13] << 24) | ((uint64_t)frame[14] << 16)
                                  | ((uint64_t)frame[15] << 8)  |  (uint64_t)frame[16];
                result->enc_len   = frame_data_len - 13;
                result->enc_start = (uint8_t *)&frame[17];
                result->consumed  = (uint16_t)(tail_pos + 2);  /* 帧头 AA 到帧尾 AA */
                frame_found = 1;
                break;
            }
        }
    }

    if (!frame_found) {
        result->sync_code = ((uint64_t)raw_pkt[0] << 56) | ((uint64_t)raw_pkt[1] << 48)
                          | ((uint64_t)raw_pkt[2] << 40) | ((uint64_t)raw_pkt[3] << 32)
                          | ((uint64_t)raw_pkt[4] << 24) | ((uint64_t)raw_pkt[5] << 16)
                          | ((uint64_t)raw_pkt[6] << 8)  |  (uint64_t)raw_pkt[7];
        result->rx_type   = raw_pkt[8];
        result->enc_len   = raw_len - 9;
        result->enc_start = (uint8_t *)&raw_pkt[9];
        result->consumed  = raw_len;  /* 没找到帧，消耗全部 */
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

void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                             LoRaSrc_t *dest, uint8_t retries, uint32_t timestamp)
{
    (void)retries;

    uint8_t  inner_data[260];
    uint16_t di = 0;
    inner_data[di++] = (timestamp >> 24) & 0xFF;
    inner_data[di++] = (timestamp >> 16) & 0xFF;
    inner_data[di++] = (timestamp >> 8) & 0xFF;
    inner_data[di++] =  timestamp & 0xFF;
    inner_data[di++] =  data_type;
    memcpy(&inner_data[di], data, len);
    uint16_t data_len = di + len;

    uint8_t  frame_buf[280];
    uint16_t fi = 0;

    /* FP mode addressing header: destH | destL | channel */
    if (dest) {
        frame_buf[fi++] = (dest->addr >> 8) & 0xFF;
        frame_buf[fi++] = dest->addr & 0xFF;
        frame_buf[fi++] = dest->channel;
    } else {
        frame_buf[fi++] = 0;
        frame_buf[fi++] = 0;
        frame_buf[fi++] = 0;
    }

    frame_buf[fi++] = FRAME_START_0;
    frame_buf[fi++] = FRAME_START_1;
    frame_buf[fi++] = (data_len >> 8) & 0xFF;
    frame_buf[fi++] =  data_len & 0xFF;
    memcpy(&frame_buf[fi], inner_data, data_len);
    fi += data_len;
    frame_buf[fi++] = calc_frame_crc8(inner_data, data_len);
    frame_buf[fi++] = FRAME_END_0;
    frame_buf[fi++] = FRAME_END_1;

    uart2_tx_bytes(frame_buf, fi);
}

uint8_t send_ack(uint8_t status)
{
    uint8_t ack_byte = (status == 0) ? 0x00 : 0x01;

    /* 对齐 Master_v3(2): 必须加 FP 模式头, 否则 E220 模块进入等待状态阻塞接收 */
    uart2_tx_byte(0x00);          /* destH (high byte of target address) */
    uart2_tx_byte(0x0B);          /* destL (low byte  → SLAVE_ADDR_BASE=0x000B) */
    uart2_tx_byte(23);            /* channel */
    uart2_tx_byte(ack_byte);      /* ACK data */
    return 0;
}