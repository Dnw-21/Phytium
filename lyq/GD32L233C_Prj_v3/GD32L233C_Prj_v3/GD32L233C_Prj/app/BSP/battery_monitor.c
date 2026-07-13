#include "battery_monitor.h"
#include "gd32l23x.h"
#include "log.h"
#include <string.h>

static uint16_t g_battery_mv;

/* 手动清除SWRCST后置位，确保硬件检测到0→1边沿 */
static void adc_trigger(void)
{
    ADC_CTL1 &= ~ADC_CTL1_SWRCST;
    ADC_CTL1 |= ADC_CTL1_SWRCST;
}

static void gpio_init(void)
{
    rcu_periph_clock_enable(BATTERY_GPIO_CLK);
    gpio_mode_set(BATTERY_GPIO_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, BATTERY_GPIO_PIN);
}

static void adc_init(void)
{
    rcu_periph_clock_enable(RCU_ADC);   
    rcu_adc_clock_config(RCU_ADCCK_AHB_DIV3);   // 12MHz

    adc_deinit();
    adc_data_alignment_config(ADC_DATAALIGN_RIGHT); // 右对齐
    adc_channel_length_config(ADC_REGULAR_CHANNEL, 1);  //正常 ADC 采样通道,1通道
    adc_regular_channel_config(0, BATTERY_ADC_CHANNEL, ADC_SAMPLETIME_239POINT5);
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, DISABLE);  // 禁用外部触发,使用软件触发
    adc_special_function_config(ADC_CONTINUOUS_MODE, DISABLE);  // 禁用连续模式
    adc_enable();
    adc_calibration_enable();   //启动ADC自校准 
    adc_flag_clear(ADC_FLAG_EOC);
    adc_flag_clear(ADC_FLAG_STRC);

    /* 暖机：校准后首次触发可能被忽略，发一次空转换预热ADC */
    adc_trigger();
    uint32_t timeout = 100000;
    while (adc_flag_get(ADC_FLAG_EOC) == RESET && --timeout);
    (void)adc_regular_data_read();
    adc_flag_clear(ADC_FLAG_EOC);
    adc_flag_clear(ADC_FLAG_STRC);
}

void battery_monitor_init(void)
{
    g_battery_mv = 0;
    gpio_init();
    adc_init();
    log_info("Battery monitor init: PC0 ADC_CH10");
}

void battery_monitor_sample(void)
{
    adc_trigger();

    uint32_t timeout = 10000;
    while (adc_flag_get(ADC_FLAG_EOC) == RESET && --timeout);

    uint16_t raw = adc_regular_data_read();

    if (timeout > 0) {
        g_battery_mv = (uint16_t)((uint32_t)raw * BATTERY_VREF_MV * BATTERY_DIVIDER_RATIO / BATTERY_ADC_MAX);
    } else {
        log_error("ADC conversion timeout!");
    }
    
    adc_flag_clear(ADC_FLAG_EOC);
    adc_flag_clear(ADC_FLAG_STRC);
}

uint16_t battery_get_voltage_mv(void)
{
    return g_battery_mv;
}

/*============================================================================
 *                          基准值管理
 *============================================================================*/
static uint16_t g_battery_baseline_arr[BATTERY_BASELINE_COUNT];
static uint8_t  g_battery_baseline_count;
static uint16_t g_battery_baseline_mv;

void battery_baseline_init(void)
{
    memset(g_battery_baseline_arr, 0, sizeof(g_battery_baseline_arr));
    g_battery_baseline_count = 0;
    g_battery_baseline_mv    = 0;
    log_info("Battery baseline init: %d samples", BATTERY_BASELINE_COUNT);
}

/* 每200ms调用一次，存满20点后计算平均基准值并停止 */
void battery_baseline_collect(void)
{
    if (g_battery_baseline_count >= BATTERY_BASELINE_COUNT)
        return;    /* 已存满，不再采集 */

    battery_monitor_sample();
    g_battery_baseline_arr[g_battery_baseline_count] = g_battery_mv;
    g_battery_baseline_count++;

    if (g_battery_baseline_count >= BATTERY_BASELINE_COUNT) {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < BATTERY_BASELINE_COUNT; i++)
            sum += g_battery_baseline_arr[i];
        g_battery_baseline_mv = (uint16_t)(sum / BATTERY_BASELINE_COUNT);
        log_info("Battery baseline ready: %u mV (avg of %d)", g_battery_baseline_mv, BATTERY_BASELINE_COUNT);
    }
}

uint8_t battery_baseline_ready(void)
{
    return (g_battery_baseline_count >= BATTERY_BASELINE_COUNT) ? 1 : 0;
}

uint16_t battery_get_baseline_mv(void)
{
    return g_battery_baseline_mv;
}

/* 获取当前电池电压相对于基准值的百分比 (0~100), 返回255表示基准值未就绪 */
uint8_t battery_get_percentage(void)
{
    if (!battery_baseline_ready())
        return 0xFF;

    battery_monitor_sample();  /* 获取最新电压 */

    if (g_battery_baseline_mv == 0)
        return 0xFF;

    uint32_t pct = (uint32_t)g_battery_mv * 100 / g_battery_baseline_mv;
    if (pct > 100) pct = 100;  /* 上限饱和 */
    return (uint8_t)pct;
}
