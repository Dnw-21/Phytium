#ifndef RTC_TIME_H
#define RTC_TIME_H

#include <stdint.h>

void rtc_time_init(void);

uint32_t rtc_get_timestamp_ms(void);

void rtc_time_set_beijing(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second);

void rtc_time_init_from_compile(void);

#endif
