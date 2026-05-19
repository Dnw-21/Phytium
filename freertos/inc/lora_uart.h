#ifndef LORA_UART_H
#define LORA_UART_H

#include <stdint.h>

int      lora_uart_init(void);
void     lora_uart_poll(void);
uint16_t lora_uart_recv_frame(uint8_t *buf, uint16_t max_len);
void     lora_uart_send(const uint8_t *data, uint16_t len);

#endif
