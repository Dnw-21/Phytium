#ifndef LORA_UART_H
#define LORA_UART_H

#include <stdint.h>

int      lora_uart_init(void);
void     lora_uart_poll(void);
uint16_t lora_uart_recv_frame(uint8_t *buf, uint16_t max_len);
void     lora_uart_send(const uint8_t *data, uint16_t len);
void     lora_uart_send_str(const char *str);

int      lora_aux_is_busy(void);

/* GD32 兼容帧边界 API */
uint16_t lora_uart_get_rx_count(void);
void     lora_uart_clear_buffer(void);
void     lora_uart_mark_frame(void);
uint16_t lora_uart_read_frame(uint8_t *buf, uint16_t max_len);

/* 调试统计 */
unsigned int lora_uart_get_isr_count(void);
unsigned int lora_uart_get_byte_total(void);

#endif