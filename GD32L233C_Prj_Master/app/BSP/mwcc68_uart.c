#include "mwcc68_uart.h"
#include "systick.h"
#include "string.h"

static uint8_t usart1RxBuffer[256]; // USART1接收缓冲区 LoRa
static volatile uint16_t USART1_RX_STA = 0;
static volatile uint32_t usart1IntCount = 0;

static uint8_t usart0RxBuffer[128];
static volatile uint16_t USART0_RX_STA = 0;
static volatile uint32_t usart0IntCount = 0;

void USART0_IRQHandler(void)
{
    usart0IntCount++;
    if(usart_interrupt_flag_get(USART0, USART_INT_FLAG_RBNE) != RESET) {
        if(USART0_RX_STA < 256) {
            uint8_t data = usart_data_receive(USART0);
            usart0RxBuffer[USART0_RX_STA++] = data;
        }
        usart_interrupt_flag_clear(USART0, USART_INT_FLAG_RBNE);
    }
}

void USART1_IRQHandler(void)
{
    usart1IntCount++;
    if(usart_interrupt_flag_get(USART1, USART_INT_FLAG_RBNE) != RESET) {
        if(USART1_RX_STA < 128) {
            uint8_t data = usart_data_receive(USART1);
            usart1RxBuffer[USART1_RX_STA++] = data;
        }
        else{
            usart_data_receive(USART1);
        }
        usart_interrupt_flag_clear(USART1, USART_INT_FLAG_RBNE);

    }
}

int fputc(int ch, FILE *f)
{
    usart_data_transmit(USART0, (uint8_t)ch);
    while(usart_flag_get(USART0, USART_FLAG_TBE) == RESET);
    return ch;
}

void usart0_init(void)
{
    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_GPIOA);

    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_10);

    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9 | GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);

    nvic_irq_enable(USART0_IRQn, 3);    //串口调试的中断优先级
    usart_interrupt_enable(USART0, USART_INT_RBNE);
}

uint16_t usart1_data_available(void)
{
    return USART1_RX_STA;
}

uint16_t usart1_peek_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = USART1_RX_STA;
    if(len > maxLen - 1) len = maxLen - 1;
    for(uint16_t i = 0; i < len; i++) {
        buf[i] = usart1RxBuffer[i];
    }
    buf[len] = '\0';
    return len;
}

uint16_t usart1_read_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = USART1_RX_STA;
    if(len > maxLen) len = maxLen;
    for(uint16_t i = 0; i < len; i++) {
        buf[i] = usart1RxBuffer[i];
    }
    USART1_RX_STA = 0;
    return len;
}

void usart1_clear_buffer(void)
{
    USART1_RX_STA = 0;
}

uint16_t usart0_data_available(void)
{
    return USART0_RX_STA;
}

uint16_t usart0_peek_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = USART0_RX_STA;
    if(len > maxLen - 1) len = maxLen - 1;
    for(uint16_t i = 0; i < len; i++) {
        buf[i] = usart0RxBuffer[i];
    }
    buf[len] = '\0';
    return len;
}

uint16_t usart0_read_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = USART0_RX_STA;
    if(len > maxLen) len = maxLen;
    for(uint16_t i = 0; i < len; i++) {
        buf[i] = usart0RxBuffer[i];
    }
    USART0_RX_STA = 0;
    return len;
}

void usart0_clear_buffer(void)
{
    USART0_RX_STA = 0;
}

uint8_t *lora_check_cmd(uint8_t *str)
{
    char *strx = 0;
    if (USART1_RX_STA & 0X8000) {
        usart1RxBuffer[USART1_RX_STA & 0X7FFF] = 0;
        strx = strstr((const char *)usart1RxBuffer, (const char *)str);
    }
    return (uint8_t *)strx;
}

uint8_t lora_send_cmd(uint8_t *cmd, uint8_t *ack, uint16_t waittime)
{
    uint8_t res = 0;
    USART1_RX_STA = 0;

    if ((uint32_t)cmd <= 0XFF) {
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, (uint8_t)cmd);
    } else {
        uint8_t len = strlen((char*)cmd);
        for(uint8_t i=0; i<len; i++) {
            while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
            usart_data_transmit(USART1, cmd[i]);
        }
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, '\r');
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, '\n');
    }

    if (ack && waittime) {
        while (--waittime) {
            cpu_delay_ms(10);
            if (USART1_RX_STA & 0X8000) {
                if (lora_check_cmd(ack)) break;
                USART1_RX_STA = 0;
            }
        }
        if (waittime == 0) res = 1;
    }
    return res;
}