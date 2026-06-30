#include "data_frame.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "systick.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static uint32_t get_timestamp_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
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
 *  解析 LoRa 帧: 检查同步码、数据类型、加密长度、加密起始位置
*/
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
    }
}

/*====================================================================
 *  发送带帧标记的LoRa数据包
 *  帧格式: [0xAA 0x55][len 2B][data nB][CRC8 1B][0x55 0xAA]
 *  data段: [timestamp 4B][data_type 1B][inner_data nB]
 *  帧开销: 7 (帧头尾CRC) + 5 (timestamp+type) + 8 (sync_code) = 20 B
 *  总帧长 fi = 20 + packet.data_len ≤ 240B
 *====================================================================*/
void send_node_data_with_ack(uint8_t *data, uint16_t len, uint8_t data_type,
                                   LoRaSrc_t *dest,
                                   uint8_t retries,
                                   uint32_t timestamp)
{
    uint8_t  inner_data[248];
    uint8_t  frame_buf[256];
    uint8_t  retry_count = 0;
    uint16_t di = 0;

    inner_data[di++] = (timestamp >> 24) & 0xFF;
    inner_data[di++] = (timestamp >> 16) & 0xFF;
    inner_data[di++] = (timestamp >> 8) & 0xFF;
    inner_data[di++] =  timestamp & 0xFF;
    inner_data[di++] =  data_type;
    memcpy(&inner_data[di], data, len);
    uint16_t data_len = di + len;   //添加timestamp和data_type的5字节

    uint16_t fi = 0;
    frame_buf[fi++] = FRAME_START_0;
    frame_buf[fi++] = FRAME_START_1;
    frame_buf[fi++] = (data_len >> 8) & 0xFF;
    frame_buf[fi++] =  data_len & 0xFF;
    memcpy(&frame_buf[fi], inner_data, data_len);
    fi += data_len;
    frame_buf[fi++] = calc_frame_crc8(inner_data, data_len);
    frame_buf[fi++] = FRAME_END_0;
    frame_buf[fi++] = FRAME_END_1;
    
    LoRa_SendData(frame_buf, fi);
    retry_count++;
    vTaskDelay(100);    //之前是100

}

/*====================================================================
 *  发送ACK确认帧
 *====================================================================*/
uint8_t send_ack(uint8_t status)
{
    uint8_t ack_byte = (status == 0) ? 0x00 : 0x01;
    LoRa_SendData(&ack_byte, 1);
    return ATK_MWCC68D_EOK;
}
