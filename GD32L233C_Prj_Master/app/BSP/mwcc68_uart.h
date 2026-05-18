#ifndef _MWCC68_UART_H
#define _MWCC68_UART_H

#include "gd32l23x.h"
#include <stdio.h>

void usart0_init(void);

uint16_t usart1_data_available(void);   // 获取USART1接收缓冲区数据长度
uint16_t usart1_peek_data(uint8_t* buf, uint16_t maxLen); // 读取USART1接收缓冲区数据，不移除
uint16_t usart1_read_data(uint8_t* buf, uint16_t maxLen); // 读取USART1接收缓冲区数据，移除
void usart1_clear_buffer(void); // 清空USART1接收缓冲区

uint16_t usart0_data_available(void);   // 获取USART0接收缓冲区数据长度
uint16_t usart0_peek_data(uint8_t* buf, uint16_t maxLen); // 读取USART0接收缓冲区数据，不移除
uint16_t usart0_read_data(uint8_t* buf, uint16_t maxLen); // 读取USART0接收缓冲区数据，移除
void usart0_clear_buffer(void); // 清空USART0接收缓冲区

#endif