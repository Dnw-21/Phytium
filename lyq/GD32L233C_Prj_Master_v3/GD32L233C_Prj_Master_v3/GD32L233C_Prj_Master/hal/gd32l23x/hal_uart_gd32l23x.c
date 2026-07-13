#include "hal_uart.h"
#include "hal_platform_gd32l23x.h"
#include "gd32l23x.h"
#include <string.h>
#include <stdio.h>

#define USART0_RX_BUF_SIZE   512
#define USART1_RX_BUF_SIZE   1024
#define LORA_FRAME_QUEUE     8

typedef struct {
    uint32_t usart_periph;
    uint32_t usart_clk;
    uint32_t gpio_clk;
    uint32_t gpio_port;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint8_t  af;
    uint8_t  irq_channel;
    uint8_t  rx_buf_size;
    uint8_t  has_frame_queue;
} uart_hw_config_t;

typedef struct {
    uint8_t *rx_buf;
    volatile uint16_t wr;
    volatile uint16_t rd;
    volatile uint16_t count;
    volatile uint16_t frame_pos[LORA_FRAME_QUEUE];
    volatile uint8_t  frame_head;
    volatile uint8_t  frame_tail;
    hal_uart_rx_callback_t rx_callback;
    uint8_t  initialized;
    uint16_t buf_size;
} uart_runtime_t;

static const uart_hw_config_t g_uart_hw[HAL_UART_MAX] = {
    [HAL_UART_ID_DEBUG] = {
        .usart_periph = USART0,
        .usart_clk    = RCU_USART0,
        .gpio_clk     = RCU_GPIOA,
        .gpio_port    = GPIOA,
        .tx_pin       = GPIO_PIN_9,
        .rx_pin       = GPIO_PIN_10,
        .af           = GPIO_AF_7,
        .irq_channel  = USART0_IRQn,
        .rx_buf_size  = USART0_RX_BUF_SIZE,
        .has_frame_queue = 0,
    },
    [HAL_UART_ID_LORA] = {
        .usart_periph = USART1,
        .usart_clk    = RCU_USART1,
        .gpio_clk     = RCU_GPIOA,
        .gpio_port    = GPIOA,
        .tx_pin       = GPIO_PIN_2,
        .rx_pin       = GPIO_PIN_3,
        .af           = GPIO_AF_7,
        .irq_channel  = USART1_IRQn,
        .rx_buf_size  = USART1_RX_BUF_SIZE,
        .has_frame_queue = 1,
    },
};

static uart_runtime_t g_uart_rt[HAL_UART_MAX];
static uint8_t g_uart0_rx_buf[USART0_RX_BUF_SIZE];
static uint8_t g_uart1_rx_buf[USART1_RX_BUF_SIZE];

static void uart_isr_handler(uint8_t id)
{
    const uart_hw_config_t *hw = &g_uart_hw[id];
    uart_runtime_t *rt = &g_uart_rt[id];

    if (usart_interrupt_flag_get(hw->usart_periph, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t data = (uint8_t)usart_data_receive(hw->usart_periph);
        if (rt->count < rt->buf_size) {
            rt->rx_buf[rt->wr] = data;
            rt->wr = (rt->wr + 1) % rt->buf_size;
            rt->count++;
        }
        if (rt->rx_callback) {
            rt->rx_callback(id, data);
        }
        usart_interrupt_flag_clear(hw->usart_periph, USART_INT_FLAG_RBNE);
    }

    if (usart_flag_get(hw->usart_periph, USART_FLAG_ORERR) != RESET) {
        usart_flag_clear(hw->usart_periph, USART_FLAG_ORERR);
    }
    usart_interrupt_flag_clear(hw->usart_periph, USART_INT_FLAG_ERR_NERR);
}

void USART0_IRQHandler(void)
{
    uart_isr_handler(HAL_UART_ID_DEBUG);
}

void USART1_IRQHandler(void)
{
    uart_isr_handler(HAL_UART_ID_LORA);
}

int hal_uart_init(hal_uart_id_t uart_id, uint32_t baudrate)
{
    if (uart_id >= HAL_UART_MAX) return HAL_ERR_INVAL;

    const uart_hw_config_t *hw = &g_uart_hw[uart_id];
    uart_runtime_t *rt = &g_uart_rt[uart_id];

    if (uart_id == HAL_UART_ID_DEBUG) {
        rt->rx_buf = g_uart0_rx_buf;
    } else {
        rt->rx_buf = g_uart1_rx_buf;
    }
    rt->buf_size = hw->rx_buf_size;
    rt->wr = 0;
    rt->rd = 0;
    rt->count = 0;
    rt->rx_callback = NULL;
    rt->initialized = 1;

    rcu_periph_clock_enable(hw->usart_clk);
    rcu_periph_clock_enable(hw->gpio_clk);

    gpio_af_set(hw->gpio_port, hw->af, hw->tx_pin);
    gpio_af_set(hw->gpio_port, hw->af, hw->rx_pin);
    gpio_mode_set(hw->gpio_port, GPIO_MODE_AF, GPIO_PUPD_PULLUP, hw->tx_pin | hw->rx_pin);
    gpio_output_options_set(hw->gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, hw->tx_pin);

    usart_deinit(hw->usart_periph);
    usart_baudrate_set(hw->usart_periph, baudrate);
    usart_word_length_set(hw->usart_periph, USART_WL_8BIT);
    usart_stop_bit_set(hw->usart_periph, USART_STB_1BIT);
    usart_parity_config(hw->usart_periph, USART_PM_NONE);
    usart_receive_config(hw->usart_periph, USART_RECEIVE_ENABLE);
    usart_transmit_config(hw->usart_periph, USART_TRANSMIT_ENABLE);
    usart_enable(hw->usart_periph);

    nvic_irq_enable(hw->irq_channel, 3);
    usart_interrupt_enable(hw->usart_periph, USART_INT_RBNE);

    return HAL_OK;
}

int hal_uart_register_rx_callback(hal_uart_id_t uart_id, hal_uart_rx_callback_t callback)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;
    g_uart_rt[uart_id].rx_callback = callback;
    return HAL_OK;
}

int hal_uart_send_byte(hal_uart_id_t uart_id, uint8_t byte)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;

    const uart_hw_config_t *hw = &g_uart_hw[uart_id];
    while (usart_flag_get(hw->usart_periph, USART_FLAG_TBE) == RESET);
    usart_data_transmit(hw->usart_periph, byte);
    return HAL_OK;
}

int hal_uart_send(hal_uart_id_t uart_id, const uint8_t *data, uint16_t len)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;

    const uart_hw_config_t *hw = &g_uart_hw[uart_id];
    for (uint16_t i = 0; i < len; i++) {
        while (usart_flag_get(hw->usart_periph, USART_FLAG_TBE) == RESET);
        usart_data_transmit(hw->usart_periph, data[i]);
    }
    return (int)len;
}

int hal_uart_data_available(hal_uart_id_t uart_id)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;
    return (int)g_uart_rt[uart_id].count;
}

int hal_uart_read(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;

    uart_runtime_t *rt = &g_uart_rt[uart_id];
    uint16_t len = (rt->count < max_len) ? rt->count : max_len;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rt->rx_buf[rt->rd];
        rt->rd = (rt->rd + 1) % rt->buf_size;
    }
    rt->count -= len;
    return (int)len;
}

int hal_uart_peek(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;

    uart_runtime_t *rt = &g_uart_rt[uart_id];
    uint16_t len = (rt->count < max_len) ? rt->count : max_len;
    uint16_t pos = rt->rd;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rt->rx_buf[pos];
        pos = (pos + 1) % rt->buf_size;
    }
    return (int)len;
}

int hal_uart_clear_buffer(hal_uart_id_t uart_id)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;
    uart_runtime_t *rt = &g_uart_rt[uart_id];
    rt->rd = rt->wr;
    rt->count = 0;
    return HAL_OK;
}

int hal_uart_frame_available(hal_uart_id_t uart_id)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;
    uart_runtime_t *rt = &g_uart_rt[uart_id];
    return (rt->frame_head != rt->frame_tail) ? 1 : 0;
}

int hal_uart_mark_frame(hal_uart_id_t uart_id)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;
    uart_runtime_t *rt = &g_uart_rt[uart_id];
    uint8_t next = (rt->frame_head + 1) % LORA_FRAME_QUEUE;
    if (next != rt->frame_tail) {
        rt->frame_pos[rt->frame_head] = rt->wr;
        rt->frame_head = next;
    }
    return HAL_OK;
}

int hal_uart_read_frame(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len)
{
    if (uart_id >= HAL_UART_MAX || !g_uart_rt[uart_id].initialized)
        return HAL_ERR_INVAL;

    uart_runtime_t *rt = &g_uart_rt[uart_id];
    const uart_hw_config_t *hw = &g_uart_hw[uart_id];

    if (rt->frame_head == rt->frame_tail) return 0;

    uint16_t end   = rt->frame_pos[rt->frame_tail];
    uint16_t start = rt->rd;
    rt->frame_tail = (rt->frame_tail + 1) % LORA_FRAME_QUEUE;

    uint16_t total = (end >= start) ? (end - start) : (hw->rx_buf_size - start + end);
    uint16_t len = (total < max_len) ? total : max_len;
    if (len == 0) return 0;

    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rt->rx_buf[start];
        start = (start + 1) % rt->buf_size;
    }
    rt->rd   = end;
    rt->count -= len;
    return (int)len;
}

int fputc(int ch, FILE *f)
{
    (void)f;
    hal_uart_send_byte(HAL_UART_ID_DEBUG, (uint8_t)ch);
    return ch;
}
