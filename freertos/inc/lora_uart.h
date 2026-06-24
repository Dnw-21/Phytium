#ifndef LORA_UART_H
#define LORA_UART_H

#include "ftypes.h"

int      lora_uart_init(void);
void     lora_uart_interrupt_enable(int enable);
void     lora_uart_poll(void);
u16      lora_uart_recv_frame(u8 *buf, u16 max_len);
void     lora_uart_send(const u8 *data, u16 len);
void     lora_uart_send_str(const char *str);

int      lora_uart_read_byte(u8 *byte);
void     lora_md0_high(void);
void     lora_md0_low(void);
int      lora_aux_is_busy(void);

u16      lora_uart_get_rx_count(void);
void     lora_uart_clear_buffer(void);
void     lora_uart_mark_frame(void);
u16      lora_uart_read_frame(u8 *buf, u16 max_len);
u16      lora_uart_read_bytes(u8 *buf, u16 max_len);

u32      lora_uart_get_isr_count(void);
u32      lora_uart_get_byte_total(void);

void     debug_putc(char c);
void     debug_print(const char *s);

#endif
