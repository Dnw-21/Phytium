#include "wave_capture.h"
#include "gd32l23x.h"
#include "data_monitor.h"
#include "wave_monitor.h"
#include "log.h"
#include <string.h>
#include <math.h>
#include <inttypes.h>

#define DAC_TIMER           TIMER2
#define DAC_TIMER_CLK       RCU_TIMER2
#define ADC_TIMER           TIMER1
#define ADC_TIMER_CLK       RCU_TIMER1

#define DAC_DMA_CH          DMA_CH2
#define ADC_DMA_CH          DMA_CH1
#define ADC_DMA_CH_IRQ      DMA_Channel1_IRQn

#define DAC_GPIO_PORT       GPIOA
#define DAC_GPIO_PIN        GPIO_PIN_4
#define DAC_GPIO_CLK        RCU_GPIOA
#define ADC_GPIO_PORT       GPIOA
#define ADC_GPIO_PIN        GPIO_PIN_0
#define ADC_GPIO_CLK        RCU_GPIOA

#define DAC_OUTPUT_RATE     51200

static uint16_t g_dac_lut[WAVE_LUT_SIZE];

#define DAC_VREF            3.3f
#define DAC_AMPLITUDE_V     1.0f
#define DAC_OFFSET_V        1.65f
#define DAC_MAX_CODE        4095.0f

static void sine_lut_generate(void)
{
    for (uint16_t i = 0; i < WAVE_LUT_SIZE; i++) {
        float phase = (2.0f * 3.14159265f * i) / WAVE_LUT_SIZE;
        float voltage = sinf(phase) * DAC_AMPLITUDE_V + DAC_OFFSET_V;
        float code = voltage / DAC_VREF * DAC_MAX_CODE;
        if (code > DAC_MAX_CODE) code = DAC_MAX_CODE;
        if (code < 0.0f) code = 0.0f;
        g_dac_lut[i] = (uint16_t)(code + 0.5f);
    }
}

static void gpio_init(void)
{
    rcu_periph_clock_enable(DAC_GPIO_CLK);  // 使能DAC GPIO时钟
    rcu_periph_clock_enable(ADC_GPIO_CLK);  // 使能ADC GPIO时钟
    gpio_mode_set(DAC_GPIO_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, DAC_GPIO_PIN);
    gpio_mode_set(ADC_GPIO_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, ADC_GPIO_PIN);
}

static void dac_timer_init(void)
{
    rcu_periph_clock_enable(DAC_TIMER_CLK);

    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = 0;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = (SystemCoreClock / DAC_OUTPUT_RATE) - 1;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init(DAC_TIMER, &timer_initpara);

    timer_master_output_trigger_source_select(DAC_TIMER, TIMER_TRI_OUT_SRC_UPDATE);
}

static void adc_timer_init(uint16_t rate)
{
    rcu_periph_clock_enable(ADC_TIMER_CLK);

    uint32_t period = (SystemCoreClock / rate) - 1;

    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = 0;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = period;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init(ADC_TIMER, &timer_initpara);

    timer_oc_parameter_struct ocpara;
    timer_channel_output_struct_para_init(&ocpara);
    ocpara.outputstate = TIMER_CCX_ENABLE;
    ocpara.ocpolarity  = TIMER_OC_POLARITY_HIGH;
    timer_channel_output_config(ADC_TIMER, TIMER_CH_1, &ocpara);
    timer_channel_output_mode_config(ADC_TIMER, TIMER_CH_1, TIMER_OC_MODE_PWM1);
    timer_channel_output_pulse_value_config(ADC_TIMER, TIMER_CH_1, period / 2);
}

static void dac_dma_init(void)
{
    rcu_periph_clock_enable(RCU_DMA);
    rcu_periph_clock_enable(RCU_DAC);

    dac_deinit();
    dac_trigger_source_config(DAC_TRIGGER_T2_TRGO); // 配置DAC触发源为T2_TRGO
    dac_trigger_enable(); // 使能DAC触发
    dac_output_buffer_disable();     // 禁用DAC输出缓冲区
    dac_wave_mode_config(DAC_WAVE_DISABLE); // 禁用DAC波形模式
    dac_enable();

    dma_parameter_struct dma_init_struct;
    dma_struct_para_init(&dma_init_struct);
    dma_init_struct.periph_addr  = (uint32_t)&OUT_R12DH; // 配置DMA外设地址为DAC输出寄存器
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT; // 配置DMA外设宽度为16位
    dma_init_struct.memory_addr  = (uint32_t)g_dac_lut; // 配置DMA内存地址为DAC LUT
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT; // 配置DMA内存宽度为16位
    dma_init_struct.number       = WAVE_LUT_SIZE; // 配置DMA传输数据量为DAC LUT大小
    dma_init_struct.priority     = DMA_PRIORITY_HIGH; // 配置DMA优先级为高
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE; // 配置DMA外设地址不增加
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE; // 配置DMA内存地址增加
    dma_init_struct.direction    = DMA_MEMORY_TO_PERIPHERAL; // 配置DMA传输方向为内存到外设
    dma_init_struct.request      = DMA_REQUEST_DAC; // 配置DMA请求为DAC
    dma_init(DAC_DMA_CH, &dma_init_struct);

    dma_circulation_enable(DAC_DMA_CH);
    dac_dma_enable();
    dma_channel_enable(DAC_DMA_CH);
}

static void adc_dma_init(void)
{
    rcu_periph_clock_enable(RCU_ADC);
    rcu_adc_clock_config(RCU_ADCCK_AHB_DIV3);

    adc_deinit();
    adc_data_alignment_config(ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC_REGULAR_CHANNEL, 1);
    adc_regular_channel_config(0, ADC_CHANNEL_0, ADC_SAMPLETIME_7POINT5);
    adc_external_trigger_config(ADC_REGULAR_CHANNEL, ENABLE);
    adc_external_trigger_source_config(ADC_REGULAR_CHANNEL, ADC_EXTTRIG_REGULAR_T1_CH1);
    adc_special_function_config(ADC_CONTINUOUS_MODE, ENABLE);
    adc_dma_mode_enable();
    adc_enable();
    adc_calibration_enable();

    dma_parameter_struct dma_init_struct;
    dma_struct_para_init(&dma_init_struct);
    dma_init_struct.periph_addr  = (uint32_t)&ADC_RDATA;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_init_struct.memory_addr  = (uint32_t)dma_wave_buf_ptr();
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT;  // 配置DMA内存宽度为16位
    dma_init_struct.number       = DMA_PINGPONG_SIZE * 2; // 配置DMA传输数据量为2个Ping-Pong缓冲区大小
    dma_init_struct.priority     = DMA_PRIORITY_ULTRA_HIGH; // 配置DMA优先级为超高
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE; // 配置DMA外设地址不增加
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE; // 配置DMA内存地址增加
    dma_init_struct.direction    = DMA_PERIPHERAL_TO_MEMORY; // 配置DMA传输方向为外设到内存
    dma_init_struct.request      = DMA_REQUEST_ADC; // 配置DMA请求为ADC
    dma_init(ADC_DMA_CH, &dma_init_struct);

    dma_circulation_enable(ADC_DMA_CH);
    dma_interrupt_enable(ADC_DMA_CH, DMA_INT_FLAG_HTF);
    dma_interrupt_enable(ADC_DMA_CH, DMA_INT_FLAG_FTF);
    nvic_irq_enable(ADC_DMA_CH_IRQ, 1);

    dma_channel_enable(ADC_DMA_CH);
}

void wave_capture_init(void)
{
    memset(g_dac_lut, 0, sizeof(g_dac_lut));

    sine_lut_generate();    // 生成正弦波LUT
    gpio_init();

    log_warn("DAC LUT[0..15]: %" PRIu16 " %" PRIu16 " %" PRIu16 " %" PRIu16 " %" PRIu16 " %" PRIu16 "",
             g_dac_lut[0], g_dac_lut[1], g_dac_lut[2], g_dac_lut[3],
             g_dac_lut[4], g_dac_lut[5], g_dac_lut[6], g_dac_lut[7],
             g_dac_lut[8], g_dac_lut[9], g_dac_lut[10], g_dac_lut[11],
             g_dac_lut[12], g_dac_lut[13], g_dac_lut[14], g_dac_lut[15]);
    log_warn("Wave HW init: DAC→PA4, ADC←PA0, LUT=%d, Buf=%dx%d",
             WAVE_LUT_SIZE, DMA_PINGPONG_SIZE, 2);
}

void wave_capture_start(uint16_t adc_rate)
{
    wave_capture_stop();

    dac_timer_init();
    adc_timer_init(adc_rate);
    dac_dma_init();
    adc_dma_init();

    timer_enable(DAC_TIMER);
    timer_enable(ADC_TIMER);
    adc_software_trigger_enable(ADC_REGULAR_CHANNEL);

    log_warn("Wave capture: ADC=%dHz, DAC=%dHz", adc_rate, DAC_OUTPUT_RATE);
}

void wave_capture_stop(void)
{
    timer_disable(ADC_TIMER);
    timer_disable(DAC_TIMER);
    dma_channel_disable(ADC_DMA_CH);
    dma_channel_disable(DAC_DMA_CH);
    adc_dma_mode_disable();
    dac_dma_disable();
    adc_disable();
    dac_disable();
    nvic_irq_disable(ADC_DMA_CH_IRQ);
}

void DMA_Channel1_IRQHandler(void)
{
    //半传输完成标志处理写入Flash缓冲区
    if (dma_interrupt_flag_get(ADC_DMA_CH, DMA_INT_FLAG_HTF) != RESET) {
        dma_interrupt_flag_clear(ADC_DMA_CH, DMA_INT_FLAG_HTF);
        dma_wave_buf_done(0);
    }
    //全传输完成标志处理写入Flash缓冲区
    if (dma_interrupt_flag_get(ADC_DMA_CH, DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(ADC_DMA_CH, DMA_INT_FLAG_FTF);
        dma_wave_buf_done(1);
    }
}