#ifndef __BATTERY_MONITOR_H
#define __BATTERY_MONITOR_H

#include <stdint.h>
#include "gd32l23x.h"

#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_10
#define BATTERY_GPIO_PORT       GPIOC
#define BATTERY_GPIO_PIN        GPIO_PIN_0
#define BATTERY_GPIO_CLK        RCU_GPIOC

#define BATTERY_VREF_MV         3300
#define BATTERY_ADC_MAX         4095
#define BATTERY_DIVIDER_RATIO   5

#define BATTERY_BASELINE_COUNT  10       /* 基准值采样点数 */

void battery_monitor_init(void);
void battery_monitor_sample(void);
uint16_t battery_get_voltage_mv(void);

/* 基准值管理 */
void     battery_baseline_init(void);
void     battery_baseline_collect(void);
uint8_t  battery_baseline_ready(void);
uint16_t battery_get_baseline_mv(void);
uint8_t  battery_get_percentage(void);    /* 返回 0~100, 255=未就绪 */

#endif
