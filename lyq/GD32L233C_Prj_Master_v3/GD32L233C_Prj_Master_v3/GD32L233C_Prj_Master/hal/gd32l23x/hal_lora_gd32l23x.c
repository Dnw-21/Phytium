#include "hal_lora.h"
#include "hal_platform_gd32l23x.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hal_delay.h"
#include "gd32l23x.h"
#include "gd32l23x_exti.h"
#include "gd32l23x_syscfg.h"
#include "gd32l23x_misc.h"
#include "string.h"
#include "stdio.h"

#define LORA_CFG_RX_BUF_SIZE   512

static hal_lora_state_t g_lora_state = HAL_LORA_STATE_CONFIG;

int hal_lora_init(void)
{
    hal_uart_init(HAL_UART_ID_LORA, 115200);

    hal_gpio_output_init(HAL_PIN_MD0, HAL_GPIO_PULL_UP);
    hal_gpio_input_init(HAL_PIN_AUX, HAL_GPIO_PULL_UP);

    rcu_periph_clock_enable(RCU_SYSCFG);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN9);
    exti_init(EXTI_9, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    exti_interrupt_enable(EXTI_9);
    exti_interrupt_flag_clear(EXTI_9);
    nvic_irq_enable(EXTI5_9_IRQn, 1);

    hal_lora_enter_config();

    uint8_t retry = 20;
    while (retry) {
        hal_uart_clear_buffer(HAL_UART_ID_LORA);
        hal_uart_send(HAL_UART_ID_LORA, (const uint8_t *)"AT\r\n", 4);
        hal_delay_ms(300);

        int avail = hal_uart_data_available(HAL_UART_ID_LORA);
        if (avail > 0) {
            uint8_t atbuf[32];
            int len = hal_uart_peek(HAL_UART_ID_LORA, atbuf, sizeof(atbuf));
            if (len > 0) {
                atbuf[len] = '\0';
                if (strstr((char *)atbuf, "OK") != NULL) {
                    hal_uart_clear_buffer(HAL_UART_ID_LORA);
                    break;
                }
            }
        }
        retry--;
    }

    if (!retry) {
        return HAL_ERR_TIMEOUT;
    }

    hal_lora_set_addr(HAL_LORA_GD32L23X_DEFAULT_ADDR);
    hal_delay_ms(100);
    hal_lora_set_netid(HAL_LORA_GD32L23X_DEFAULT_NETID);
    hal_delay_ms(100);
    hal_lora_set_chn(HAL_LORA_GD32L23X_DEFAULT_CHN);
    hal_delay_ms(100);
    hal_lora_set_packsize(3);
    hal_delay_ms(100);
    hal_lora_set_wlrate(5);
    hal_delay_ms(100);
    hal_lora_set_tmode(1);
    hal_delay_ms(100);
    hal_lora_set_power(5);
    hal_delay_ms(100);
    hal_lora_set_bps(7);

    hal_lora_exit_config();
    return HAL_OK;
}

int hal_lora_get_state(hal_lora_state_t *state)
{
    if (!state) return HAL_ERR_INVAL;
    *state = g_lora_state;
    return HAL_OK;
}

int hal_lora_get_aux(uint8_t *level)
{
    return hal_gpio_input_read(HAL_PIN_AUX, level);
}

int hal_lora_wait_aux_high(uint32_t timeout_ms)
{
    while (timeout_ms--) {
        uint8_t level;
        hal_lora_get_aux(&level);
        if (level) return HAL_OK;
        hal_delay_ms(1);
    }
    return HAL_ERR_TIMEOUT;
}

int hal_lora_enter_config(void)
{
    hal_gpio_output_set(HAL_PIN_MD0, 1);
    hal_delay_ms(500);
    g_lora_state = HAL_LORA_STATE_CONFIG;
    return HAL_OK;
}

int hal_lora_exit_config(void)
{
    hal_gpio_output_set(HAL_PIN_MD0, 0);
    if (hal_lora_wait_aux_high(5000) != HAL_OK) {
        return HAL_ERR_TIMEOUT;
    }
    g_lora_state = HAL_LORA_STATE_RX;
    return HAL_OK;
}

static int lora_at_check_reply(void)
{
    uint32_t timeout = 500;
    while (timeout--) {
        int avail = hal_uart_data_available(HAL_UART_ID_LORA);
        if (avail > 0) {
            uint8_t buf[64];
            int len = hal_uart_peek(HAL_UART_ID_LORA, buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                if (strstr((char *)buf, "OK") != NULL) {
                    hal_uart_clear_buffer(HAL_UART_ID_LORA);
                    return HAL_OK;
                }
            }
        }
        hal_delay_ms(1);
    }
    hal_uart_clear_buffer(HAL_UART_ID_LORA);
    return HAL_ERR_TIMEOUT;
}

static int lora_set_param(const char *fmt, uint32_t val)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), fmt, val);
    uint16_t cmd_len = (uint16_t)strlen(cmd);

    hal_uart_clear_buffer(HAL_UART_ID_LORA);
    hal_uart_send(HAL_UART_ID_LORA, (const uint8_t *)cmd, cmd_len);
    hal_uart_send(HAL_UART_ID_LORA, (const uint8_t *)"\r\n", 2);

    return lora_at_check_reply();
}

int hal_lora_set_addr(uint16_t addr)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+ADDR=%02X,%02X", (addr >> 8) & 0xFF, addr & 0xFF);
    hal_uart_clear_buffer(HAL_UART_ID_LORA);
    hal_uart_send(HAL_UART_ID_LORA, (const uint8_t *)cmd, (uint16_t)strlen(cmd));
    hal_uart_send(HAL_UART_ID_LORA, (const uint8_t *)"\r\n", 2);
    return lora_at_check_reply();
}

int hal_lora_set_netid(uint8_t netid)   { return lora_set_param("AT+NETID=%d", netid); }
int hal_lora_set_chn(uint8_t chn)       { return lora_set_param("AT+CHN=%d", chn); }
int hal_lora_set_packsize(uint8_t size) { return lora_set_param("AT+PACKSIZE=%d", size); }
int hal_lora_set_wlrate(uint8_t rate)   { return lora_set_param("AT+WLRATE=%d", rate); }
int hal_lora_set_tmode(uint8_t mode)    { return lora_set_param("AT+TMODE=%d", mode); }
int hal_lora_set_power(uint8_t power)   { return lora_set_param("AT+POWER=%d", power); }
int hal_lora_set_bps(uint8_t bps)       { return lora_set_param("AT+BPS=%d", bps); }

int hal_lora_send(const uint8_t *data, uint16_t len)
{
    return hal_lora_send_to(data, len, 0x000A, 23);
}

int hal_lora_send_to(const uint8_t *data, uint16_t len,
                     uint16_t dest_addr, uint8_t chn)
{
    hal_uart_send_byte(HAL_UART_ID_LORA, (uint8_t)((dest_addr >> 8) & 0xFF));
    hal_uart_send_byte(HAL_UART_ID_LORA, (uint8_t)(dest_addr & 0xFF));
    hal_uart_send_byte(HAL_UART_ID_LORA, chn);
    hal_uart_send(HAL_UART_ID_LORA, data, len);
    return HAL_OK;
}

int hal_lora_send_string(const char *str)
{
    return hal_lora_send((const uint8_t *)str, (uint16_t)strlen(str));
}

int hal_lora_data_available(void)
{
    return hal_uart_data_available(HAL_UART_ID_LORA);
}

int hal_lora_read(uint8_t *buf, uint16_t max_len)
{
    return hal_uart_read(HAL_UART_ID_LORA, buf, max_len);
}

int hal_lora_peek(uint8_t *buf, uint16_t max_len)
{
    return hal_uart_peek(HAL_UART_ID_LORA, buf, max_len);
}

int hal_lora_clear_buffer(void)
{
    return hal_uart_clear_buffer(HAL_UART_ID_LORA);
}

int hal_lora_frame_available(void)
{
    return hal_uart_frame_available(HAL_UART_ID_LORA);
}

int hal_lora_mark_frame(void)
{
    return hal_uart_mark_frame(HAL_UART_ID_LORA);
}

int hal_lora_read_frame(uint8_t *buf, uint16_t max_len)
{
    return hal_uart_read_frame(HAL_UART_ID_LORA, buf, max_len);
}

int hal_lora_get_rx_count(void)
{
    return hal_uart_data_available(HAL_UART_ID_LORA);
}
