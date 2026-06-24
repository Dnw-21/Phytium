#include "rtc_time.h"
#include "gd32l23x.h"
#include "FreeRTOS.h"
#include "task.h"

#define RTC_ASYNCH_PREDIV  127U
#define RTC_SYNCH_PREDIV   255U

#define EPOCH_YEAR_BASE    2025U

static volatile int32_t g_systick_at_sec;

static const uint16_t g_month_days[2][12] = {
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 },
};

static uint8_t is_leap(uint16_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint32_t date_to_days(uint16_t y, uint8_t m, uint8_t d)
{
    uint32_t days = 0;
    for (uint16_t i = EPOCH_YEAR_BASE; i < y; i++)
        days += is_leap(i) ? 366U : 365U;
    days += g_month_days[is_leap(y)][m - 1U];
    days += d - 1U;
    return days;
}

static uint8_t to_bcd(uint8_t val)
{
    return ((val / 10U) << 4) | (val % 10U);
}

static uint8_t from_bcd(uint8_t bcd)
{
    return ((bcd >> 4) * 10U) + (bcd & 0x0F);
}

static uint32_t rtc_read_seconds_of_day(void)
{
    rtc_register_sync_wait();
    uint32_t tr = RTC_TIME;
    return ((uint32_t)from_bcd((tr >> 16) & 0x3F) * 3600U) +
           ((uint32_t)from_bcd((tr >> 8)  & 0x7F) * 60U) +
           ((uint32_t)from_bcd(tr & 0x7F));
}

static uint32_t rtc_read_days(void)
{
    rtc_register_sync_wait();
    uint32_t dr = RTC_DATE;
    uint16_t y = EPOCH_YEAR_BASE + from_bcd((dr >> 16) & 0xFF);
    uint8_t  m = from_bcd((dr >> 8) & 0x1F);
    uint8_t  d = from_bcd(dr & 0x3F);
    return date_to_days(y, m, d);
}

static void rtc_write_time(uint32_t day_seconds)
{
    uint8_t h = (uint8_t)(day_seconds / 3600U);
    uint8_t m = (uint8_t)((day_seconds % 3600U) / 60U);
    uint8_t s = (uint8_t)(day_seconds % 60U);

    RTC_TIME = ((uint32_t)to_bcd(h) << 16) |
               ((uint32_t)to_bcd(m) << 8)  |
               ((uint32_t)to_bcd(s));
}

static void rtc_write_date(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t dow)
{
    RTC_DATE = ((uint32_t)to_bcd((uint8_t)(year % 100U)) << 16) |
               ((uint32_t)to_bcd(month) << 8) |
               ((uint32_t)to_bcd(day)) |
               ((uint32_t)dow << 13);
}

void rtc_time_init(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    rcu_osci_on(RCU_LXTAL);
    rcu_osci_stab_wait(RCU_LXTAL);
    rcu_rtc_clock_config(RCU_RTCSRC_LXTAL);
    rcu_periph_clock_enable(RCU_RTC);

    if (rtc_flag_get(RTC_FLAG_INIT) == RESET) {
        rtc_init_mode_enter();
        RTC_PSC = (uint32_t)(PSC_FACTOR_A(RTC_ASYNCH_PREDIV) |
                             PSC_FACTOR_S(RTC_SYNCH_PREDIV));
        rtc_write_time(0);
        rtc_write_date(2025, 5, 25, 7);
        rtc_init_mode_exit();
    }

    g_systick_at_sec = (int32_t)xTaskGetTickCount();
}

void rtc_time_set_beijing(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t sod = (uint32_t)hour * 3600U + (uint32_t)minute * 60U + second;
    uint8_t dow  = (uint8_t)((date_to_days(year, month, day) + 2U) % 7U + 1U);

    rtc_init_mode_enter();
    rtc_write_time(sod);
    rtc_write_date(year, month, day, dow);
    rtc_init_mode_exit();

    g_systick_at_sec = (int32_t)xTaskGetTickCount();
}

static uint8_t parse_month(const char *mon)
{
    static const char names[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    for (uint8_t i = 0; i < 12; i++)
        if (mon[0] == names[i][0] && mon[1] == names[i][1] && mon[2] == names[i][2])
            return i + 1;
    return 1;
}

static uint8_t parse_2digit(const char *s)
{
    return (uint8_t)((s[0] - '0') * 10U + (s[1] - '0'));
}

static uint16_t parse_4digit(const char *s)
{
    return (uint16_t)((s[0] - '0') * 1000U + (s[1] - '0') * 100U +
                      (s[2] - '0') * 10U   + (s[3] - '0'));
}

void rtc_time_init_from_compile(void)
{
    const char *d = __DATE__;
    const char *t = __TIME__;

    uint8_t month = parse_month(d);
    uint8_t day   = (d[4] == ' ') ? (uint8_t)(d[5] - '0') :
                    (uint8_t)((d[4] - '0') * 10U + (d[5] - '0'));
    uint16_t year = parse_4digit(d + 7);

    uint8_t hour   = parse_2digit(t);
    uint8_t minute = parse_2digit(t + 3);
    uint8_t second = parse_2digit(t + 6);

    rtc_time_set_beijing(year, month, day, hour, minute, second);
}

uint32_t rtc_get_timestamp_ms(void)
{
    uint32_t days = rtc_read_days();
    uint32_t sod  = rtc_read_seconds_of_day();
    int32_t  tick = (int32_t)xTaskGetTickCount();

    taskENTER_CRITICAL();
    int32_t base_tick = g_systick_at_sec;
    taskEXIT_CRITICAL();

    int32_t  delta_ticks = tick - base_tick;
    uint32_t delta_ms    = (delta_ticks > 0) ? (uint32_t)delta_ticks * portTICK_PERIOD_MS : 0;

    uint32_t epoch_s = days * 86400U + sod;

    return epoch_s * 1000U + delta_ms;
}
