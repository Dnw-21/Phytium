#include "wave_monitor.h"
#include "data_monitor.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tasks.h"
#include "gd32l23x_fmc.h"
#include "wave_capture.h"
#include <string.h>

static WaveBuffer_t   g_wave;
static FaultRecord_t  g_fault_records[MAX_FAULT_RECORDS];
static uint8_t        g_fault_record_count; // 已保存故障记录数
static uint8_t        g_fault_saved;        // 故障记录是否已保存
static uint8_t        g_active_node = 0;    // 当前采样节点

void wave_flash_init(void)
{
    fmc_unlock();
    for (uint32_t a = WAVE_FLASH_ADDR; a < WAVE_FLASH_ADDR + WAVE_FLASH_SIZE; a += WAVE_FLASH_PAGE_SIZE)
        fmc_page_erase(a);
    fmc_lock();
    g_wave.flash_offset = 0;
    g_wave.total_recorded = 0;
}

/**
 * @brief 预擦除指定字节数的Flash页
 * @param byte_count 要擦除的字节数
 */
static void wave_flash_pre_erase(uint32_t byte_count)
{
    uint32_t start = g_wave.flash_offset % WAVE_FLASH_SIZE; //计算起始页地址
    uint32_t end   = (start + byte_count - 1) % WAVE_FLASH_SIZE + 1;    //计算结束页地址
    uint32_t active_pages = (end + WAVE_FLASH_PAGE_SIZE - 1) / WAVE_FLASH_PAGE_SIZE; //计算需要擦除的页数
    uint32_t start_page   = start / WAVE_FLASH_PAGE_SIZE; //计算起始页号

    fmc_unlock();
    for (uint32_t i = 0; i < active_pages; i++) {
        uint32_t p = (start_page + i) % (WAVE_FLASH_SIZE / WAVE_FLASH_PAGE_SIZE);
        fmc_page_erase(WAVE_FLASH_ADDR + p * WAVE_FLASH_PAGE_SIZE);
    }
    fmc_lock();
}

void wave_flash_write_bytes(uint32_t off, const uint8_t *data, uint16_t len)
{
    uint32_t w = off % WAVE_FLASH_SIZE;
    if (w + len <= WAVE_FLASH_SIZE) {
        uint32_t addr = WAVE_FLASH_ADDR + w;

        const uint32_t *src = (const uint32_t *)data;
        uint16_t words = (len + 3) / 4;
        fmc_unlock();
        for (uint16_t i = 0; i < words; i++) 
            fmc_word_program(addr + i * 4, src[i]);
        fmc_lock();
    } else {
        uint16_t first = WAVE_FLASH_SIZE - w;
        wave_flash_write_bytes(off, data, first);
        wave_flash_write_bytes(off + first, data + first, len - first);
    }
}

void wave_monitor_init(void)
{
    memset(&g_wave, 0, sizeof(g_wave));
    memset(g_fault_records, 0, sizeof(g_fault_records));
    g_fault_record_count = 0;
    g_fault_saved = 0;
    wave_flash_init();
    wave_capture_init();
    log_info("WAVE: buffer=%dB, Flash=%dKB", sizeof(g_wave), WAVE_FLASH_SIZE / 1024);
}

/*
 *                  dma_wave_buf_done: DMA缓冲区完成中断处理
 */
void dma_wave_buf_done(uint8_t buf_idx)
{
    if (!g_wave.is_recording) return;

    uint32_t bytes = DMA_PINGPONG_SIZE * sizeof(int16_t);
    wave_flash_write_bytes(g_wave.flash_offset,
                           (uint8_t *)g_wave.dma_buf[buf_idx], bytes);
    g_wave.flash_offset += bytes;
    g_wave.total_recorded += DMA_PINGPONG_SIZE;

    if (g_wave.total_recorded >= g_wave.max_samples) {
        g_wave.is_recording = 0;
        dma_wave_stop();

        if (!g_fault_saved && g_fault_record_count < MAX_FAULT_RECORDS) {
            wave_save_fault_record(g_wave.fault_detect_time,
                                   g_wave.severity, g_wave.fault_rms);
        }
        log_info("REC done: %d samples", g_wave.total_recorded);
    }
}

void wave_set_fault_detect_time(uint32_t fault_time)
{
    g_wave.fault_detect_time = fault_time;
}

void wave_set_fault_rms(float rms)
{
    g_wave.fault_rms = rms;
}

void wave_set_active_node(uint8_t node_idx)
{
    if (node_idx < 10) g_active_node = node_idx;
}

void dma_wave_start(uint16_t rate)
{
    g_wave.is_recording = 1;
    g_wave.sample_rate = rate;
    g_wave.total_recorded = 0;

    wave_flash_pre_erase(g_wave.max_samples * sizeof(int16_t));
    wave_capture_start(rate);
    log_info("DMA wave start: %dHz @ offset=%d", rate, g_wave.flash_offset);
}

void dma_wave_stop(void)
{
    wave_capture_stop();
    log_info("DMA wave stop");
}

void *dma_wave_buf_ptr(void)
{
    return g_wave.dma_buf;
}

static void wave_mode_init(SeverityLevel_t sev)
{
    g_wave.record_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_wave.severity = sev;
    g_wave.flash_offset_start = g_wave.flash_offset;
    g_fault_saved = 0;
}

void switch_to_warning_mode(void)
{
    if (get_system_mode() == MODE_DANGER) return;
    set_system_mode(MODE_WARNING);
    g_wave.max_samples = WARNING_WAVE_SAMPLES;
    wave_mode_init(SEVERITY_WARNING);
    dma_wave_start(WAVE_SAMPLE_RATE_WARN);
    xEventGroupSetBits(g_event_group, EVENT_WARNING_BIT);
    log_error("WARNING: %dkHz %dms -> %.1fKB Flash",
              WAVE_SAMPLE_RATE_WARN / 1000,
              WARNING_WAVE_MS, WARNING_WAVE_SAMPLES * 2.0f / 1024);
}

void switch_to_danger_mode(void)
{
    set_system_mode(MODE_DANGER);
    g_wave.max_samples = DANGER_WAVE_SAMPLES;
    wave_mode_init(SEVERITY_DANGER);
    dma_wave_start(WAVE_SAMPLE_RATE_DANGER);
    xEventGroupSetBits(g_event_group, EVENT_DANGER_BIT);
    log_error("DANGER: %dkHz %dms -> %.1fKB Flash",
              WAVE_SAMPLE_RATE_DANGER / 1000,
              DANGER_WAVE_MS, DANGER_WAVE_SAMPLES * 2.0f / 1024);
}

void switch_to_normal_mode(void)
{
    if (g_wave.is_recording) {
        g_wave.is_recording = 0;
        dma_wave_stop();
    }
    set_system_mode(MODE_NORMAL);
    xEventGroupSetBits(g_event_group, EVENT_SWITCH_NORMAL);
    log_info("NORMAL mode");
}

/*
 *                  wave_save_fault_record: 保存故障记录
 */
void wave_save_fault_record(uint32_t fault_time, SeverityLevel_t severity, float rms)
{
    if (g_fault_saved || g_fault_record_count >= MAX_FAULT_RECORDS) return;

    FaultRecord_t *r = &g_fault_records[g_fault_record_count];
    r->timestamp = fault_time;  // 故障发生时间
    r->severity = severity;     // 故障等级
    r->flash_offset = g_wave.flash_offset_start;    // 故障记录在Flash中的偏移量
    r->sample_count = g_wave.total_recorded;    // 故障记录的样本数
    r->sample_rate = g_wave.sample_rate;    // 故障记录的采样率
    r->valid = 1;    // 标记为有效记录
    r->node_index = g_active_node;    // 故障记录的节点索引
    r->fault_value = rms;    // 故障记录的电压幅值

    r->fault_type = FAULT_NONE;
    if (rms < VOLTAGE_NOMINAL * 0.8f) r->fault_type = FAULT_VOLTAGE_SAG;
    else if (rms > VOLTAGE_NOMINAL * 1.15f) r->fault_type = FAULT_VOLTAGE_SWELL;
    else if (rms > VOLTAGE_NOMINAL * 1.10f) r->fault_type = FAULT_OVER_VOLTAGE;
    else if (rms < VOLTAGE_NOMINAL * 0.90f) r->fault_type = FAULT_UNDER_VOLTAGE;

    g_fault_record_count++;
    g_fault_saved = 1;
    log_info("Fault#%d saved: node%d %d samp@%dHz",
             g_fault_record_count - 1, r->node_index, r->sample_count, r->sample_rate);
}
