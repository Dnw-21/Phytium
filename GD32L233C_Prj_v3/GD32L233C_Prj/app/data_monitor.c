#include <string.h>
#include <math.h>

#include "data_monitor.h"
#include "zdata_adaptive.h"
#include "wave_monitor.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tasks.h"
#include "health_assessment.h"
#include "mwcc68_cfg.h"
#include "gd32l23x_fmc.h"

static NodeBuffer_t     g_node;   /* 节点数据缓冲区 */
static MidFreqWindow_t  g_mid;    /* 中频滑动窗口 */
static uint8_t          g_mid_step;
static uint16_t         g_window_pos;  /* 当前窗口在环缓冲中的起始索引 */
static uint8_t          g_active_node = 0;  /* 当前活动节点索引，0-3 */

static PendingFault_t   g_pending_faults[MAX_PENDING_FAULTS];
static uint8_t          g_pending_fault_count;
static uint32_t         g_fault_snap_off;
static uint32_t         g_last_poll_ts;

static NodeSample_t     g_fault_win_buf[MID_FREQ_WINDOW_SAMPLES];
static uint16_t         g_fault_written;  /* 已写入故障快照点数 */  
static uint8_t          g_fault_snap_active;  /* 是否正在写入故障快照 */
static FaultType_t      g_fault_type_buf;  /* 故障类型缓冲区 */
static SeverityLevel_t  g_fault_sev_buf;  /* 故障严重缓冲区 */
static uint32_t         g_fault_start_ts;  /* 故障开始时间戳缓冲区 */

static void fault_snap_finalize(void);

/*============================================================================
 *                          初始化
 *============================================================================*/
void data_monitor_init(void)
{
    memset(&g_node, 0, sizeof(g_node));
    memset(&g_mid, 0, sizeof(g_mid));
    g_mid_step = 0;
    g_window_pos = 0;
    fault_snap_flash_init();
    wave_monitor_init();
    log_info("MON: %dHz node, %dcyc buf=%dB, fault_snap=%dKB",
             NODE_SAMPLE_RATE, NODE_BUFFER_CYCLES, sizeof(g_node),
             NODE_FAULT_SNAP_FLASH_SIZE / 1024);
}

/*============================================================================
 *          fault_snap_flash_init: 故障快照Flash初始化
 *============================================================================*/
void fault_snap_flash_init(void)
{
    fmc_unlock();
    fmc_page_erase(NODE_FAULT_SNAP_FLASH_ADDR);
    fmc_lock();

    memset(g_pending_faults, 0, sizeof(g_pending_faults));
    g_pending_fault_count = 0;
    g_fault_snap_off      = 0;
    g_last_poll_ts        = 0;
}

/*============================================================================
 *          node_fault_flash_write: 向故障快照Flash写入
 *============================================================================*/
static void node_fault_flash_write(uint32_t off, const uint8_t *data, uint16_t len)
{
    uint32_t addr = NODE_FAULT_SNAP_FLASH_ADDR + off;
    const uint32_t *src = (const uint32_t *)data;
    uint16_t words = (len + 3) / 4;
    fmc_unlock();
    for (uint16_t i = 0; i < words; i++)
        fmc_word_program(addr + i * 4, src[i]);
    fmc_lock();
}

/*============================================================================
 *          node_fault_flash_read: 从故障快照Flash读出
 *============================================================================*/
static void node_fault_flash_read(uint32_t off, void *buf, uint16_t len)
{
    memcpy(buf, (void *)(NODE_FAULT_SNAP_FLASH_ADDR + off), len);
}

/*============================================================================
 *  fault_snap_begin: 开始故障快照, 预留Flash空间
 *============================================================================*/
static void fault_snap_begin(FaultType_t f, SeverityLevel_t s, uint32_t fault_time)
{
    if (g_pending_fault_count >= MAX_PENDING_FAULTS)
        return;

    if (g_fault_snap_off + FAULT_SNAP_TOTAL_BYTES > NODE_FAULT_SNAP_FLASH_SIZE) {
        fmc_unlock();
        fmc_page_erase(NODE_FAULT_SNAP_FLASH_ADDR);
        fmc_lock();
        g_fault_snap_off = 0;
    }

    g_fault_type_buf   = f;
    g_fault_sev_buf    = s;
    g_fault_start_ts   = fault_time;
    g_fault_written    = 0;
    g_fault_snap_active = 1;
}

/*============================================================================
 *  fault_snap_write_window: 逐窗口增量写入Flash (10点/窗)
 *============================================================================*/
static void fault_snap_write_window(uint16_t window_start)
{
    if (!g_fault_snap_active) return;

    uint16_t n = MID_FREQ_WINDOW_SAMPLES;
    if (g_fault_written + n > FAULT_SNAP_POINTS)
        n = FAULT_SNAP_POINTS - g_fault_written;

    for (uint16_t i = 0; i < n; i++)
        g_fault_win_buf[i] = g_node.buffer[(window_start + i) % NODE_BUFFER_SIZE];

    uint32_t off = g_fault_snap_off + FAULT_SNAP_HEADER_BYTES
                   + g_fault_written * sizeof(NodeSample_t);
    node_fault_flash_write(off, (uint8_t *)g_fault_win_buf, n * sizeof(NodeSample_t));
    g_fault_written += n;

    if (g_fault_written >= FAULT_SNAP_POINTS)
        fault_snap_finalize();
}

/*============================================================================
 *  fault_snap_finalize: 故障结束, 写入头部完成快照
 *============================================================================*/
static void fault_snap_finalize(void)
{
    if (!g_fault_snap_active) return;
    g_fault_snap_active = 0;

    if (g_fault_written == 0) return;

    uint8_t hdr[FAULT_SNAP_HEADER_BYTES];
    memcpy(&hdr[0], &g_fault_start_ts, 4);
    hdr[4] = (uint8_t)g_fault_sev_buf;
    hdr[5] = (uint8_t)g_fault_type_buf;
    hdr[6] = (uint8_t)(g_fault_written & 0xFF);
    hdr[7] = (uint8_t)((g_fault_written >> 8) & 0xFF);
    hdr[8] = 0; hdr[9] = 0; hdr[10] = 0; hdr[11] = 0;
    node_fault_flash_write(g_fault_snap_off, hdr, FAULT_SNAP_HEADER_BYTES);

    PendingFault_t *pf = &g_pending_faults[g_pending_fault_count];
    pf->fault_time  = g_fault_start_ts;
    pf->flash_off   = g_fault_snap_off;
    pf->severity    = (uint8_t)g_fault_sev_buf;
    pf->fault_type  = (uint8_t)g_fault_type_buf;
    pf->point_count = g_fault_written;
    pf->valid       = 1;
    g_pending_fault_count++;

    g_fault_snap_off += FAULT_SNAP_TOTAL_BYTES;

    log_info("Fault snap saved: type=%d sev=%d pts=%u ts=%lu",
             g_fault_type_buf, g_fault_sev_buf, g_fault_written, g_fault_start_ts);
}

/*============================================================================
 *                  upload_fault_snaps: 上传所有待处理故障快照
 *============================================================================*/
void upload_fault_snaps(void)
{
    if (g_pending_fault_count == 0)
        return;

    static uint8_t  raw[208];

    for (uint8_t k = 0; k < g_pending_fault_count; k++) {
        PendingFault_t *pf = &g_pending_faults[k];
        if (!pf->valid) continue;

        NodeUploadHeader_t d;
        memset(&d, 0, sizeof(d));
        d.data_type    = DATA_TYPE_FAULT_HEAD;
        d.severity     = pf->severity;
        d.fault_type   = pf->fault_type;
        d.node_index   = g_active_node;
        d.sample_rate  = NODE_SAMPLE_RATE;
        d.health_score = health_get_score();
        d.total_points = pf->point_count;
        d.timestamp    = pf->fault_time;
        send_normal_data(DATA_TYPE_FAULT_HEAD, &d, sizeof(d));

        uint16_t total_pts = pf->point_count;
        uint16_t sent = 0;
        uint32_t off  = pf->flash_off + FAULT_SNAP_HEADER_BYTES;
        while (sent < total_pts) {
            uint16_t n = (total_pts - sent) > 4 ? 4 : (total_pts - sent);
            node_fault_flash_read(off, raw, n * sizeof(NodeSample_t));
            send_waveform_packet(raw, n * sizeof(NodeSample_t), DATA_TYPE_NODE_RAW);
            sent += n;
            off  += n * sizeof(NodeSample_t);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    g_pending_fault_count = 0;
    log_info("Fault snaps uploaded");
}

void set_active_node(uint8_t node_idx)
{
    if (node_idx < 10) {
        g_active_node = node_idx;
        wave_set_active_node(node_idx);
    }
}

uint8_t get_active_node(void)
{
    return g_active_node;
}

/*============================================================================
 *                  节点采样 (1kHz, 每1ms一次)
 *============================================================================*/
void node_sample_process(const ZDataPoint_t *z, uint32_t timestamp)
{
    NodeSample_t *s = &g_node.buffer[g_node.write_index];
    s->pg1     = (int16_t)(z->pg1 * 10000.0f);
    s->pg2     = (int16_t)(z->pg2 * 10000.0f);
    s->pg3     = (int16_t)(z->pg3 * 10000.0f);
    s->qg1     = (int16_t)(z->qg1 * 10000.0f);
    s->qg2     = (int16_t)(z->qg2 * 10000.0f);
    s->qg3     = (int16_t)(z->qg3 * 10000.0f);
    s->vmag1   = (int16_t)(z->vmag1 * 10000.0f);
    s->vmag2   = (int16_t)(z->vmag2 * 10000.0f);
    s->vmag3   = (int16_t)(z->vmag3 * 10000.0f);
    s->vmag4   = (int16_t)(z->vmag4 * 10000.0f);
    s->vmag5   = (int16_t)(z->vmag5 * 10000.0f);
    s->vmag6   = (int16_t)(z->vmag6 * 10000.0f);
    s->vmag7   = (int16_t)(z->vmag7 * 10000.0f);
    s->vmag8   = (int16_t)(z->vmag8 * 10000.0f);
    s->vmag9   = (int16_t)(z->vmag9 * 10000.0f);
    s->vangle1 = (int16_t)(z->vangle1 * 10000.0f);
    s->vangle2 = (int16_t)(z->vangle2 * 10000.0f);
    s->vangle3 = (int16_t)(z->vangle3 * 10000.0f);
    s->vangle4 = (int16_t)(z->vangle4 * 10000.0f);
    s->vangle5 = (int16_t)(z->vangle5 * 10000.0f);
    s->vangle6 = (int16_t)(z->vangle6 * 10000.0f);
    s->vangle7 = (int16_t)(z->vangle7 * 10000.0f);
    s->vangle8 = (int16_t)(z->vangle8 * 10000.0f);
    s->vangle9 = (int16_t)(z->vangle9 * 10000.0f);
    s->timestamp = timestamp;

    g_node.write_index = (g_node.write_index + 1) % NODE_BUFFER_SIZE;
    g_node.cycle_count++;

    if (g_node.cycle_count < SAMPLES_PER_CYCLE) {
        return;
    }

    g_mid_step++;
    if (g_mid_step >= MID_FREQ_WINDOW_SAMPLES) {
        g_mid_step = 0;
        uint16_t ahead = (g_node.write_index - g_window_pos + NODE_BUFFER_SIZE) % NODE_BUFFER_SIZE;
        if (ahead < MID_FREQ_WINDOW_SAMPLES) {
            g_window_pos = (g_node.write_index - MID_FREQ_WINDOW_SAMPLES + NODE_BUFFER_SIZE) % NODE_BUFFER_SIZE;
        }
        for (uint16_t i = 0; i < MID_FREQ_WINDOW_SAMPLES; i++) {
            g_mid.voltage_mag[i] = (float)g_node.buffer[(g_window_pos + i) % NODE_BUFFER_SIZE].vmag1 / 10000.0f;
        }
        g_window_pos = (g_window_pos + MID_FREQ_WINDOW_SAMPLES) % NODE_BUFFER_SIZE;

        FaultType_t f = detect_mid_freq_fault(&g_mid);
        if (f != FAULT_NONE) {
            SeverityLevel_t sev = classify_severity(f, g_mid.rms);
            if (sev != SEVERITY_NORMAL) {
                uint16_t fault_idx = (g_window_pos - MID_FREQ_WINDOW_SAMPLES + g_mid.dv_dt_index + NODE_BUFFER_SIZE) % NODE_BUFFER_SIZE;
                uint32_t fault_time = g_node.buffer[fault_idx].timestamp;
                wave_set_fault_detect_time(fault_time);
                uint16_t win_start = (g_window_pos - MID_FREQ_WINDOW_SAMPLES + NODE_BUFFER_SIZE) % NODE_BUFFER_SIZE;
                if (get_system_mode() == MODE_NORMAL) {
                    log_info("Fault record! Severity: %d", sev);
                    log_info("Wave record fault at %d", fault_time);
                    fault_snap_begin(f, sev, fault_time);
                    fault_snap_write_window(win_start);
                    trigger_fault(f, sev, fault_time);
                } else if (g_fault_snap_active) {
                    fault_snap_write_window(win_start);
                }
            } 
        } 
        else if (get_system_mode() != MODE_NORMAL) {
            if (g_fault_snap_active)
                fault_snap_finalize();
            switch_to_normal_mode();
        }
    }
}

/*============================================================================
 *                  内部辅助函数: 最大相邻差分斜率
 *============================================================================*/
static float calc_max_adjacent_dv_dt(MidFreqWindow_t *w)
{
    float max_dv = 0.0f;
    float t_step = MID_FREQ_WINDOW_MS / 1000.0f / MID_FREQ_WINDOW_SAMPLES;
    w->dv_dt_index = 0;
    
    for (uint16_t i = 1; i < MID_FREQ_WINDOW_SAMPLES; i++) {
        float dv = fabsf(w->voltage_mag[i] - w->voltage_mag[i-1]);
        if (dv > max_dv) {
            max_dv = dv;
            w->dv_dt_index = i;
        }
    }
    return max_dv / t_step;
}

/*============================================================================
 *                  故障检测 (基于10ms中频窗口)
 *============================================================================*/
FaultType_t detect_mid_freq_fault(MidFreqWindow_t *w)
{
    float sq = 0, maxv = -1e9f, minv = 1e9f;
    
    for (uint16_t i = 0; i < MID_FREQ_WINDOW_SAMPLES; i++) {
        float v = w->voltage_mag[i];
        sq += v * v;
        if (v > maxv) maxv = v;
        if (v < minv) minv = v;
    }
    
    w->rms   = sqrtf(sq / MID_FREQ_WINDOW_SAMPLES);
    w->peak  = maxv - minv;
    w->dv_dt = calc_max_adjacent_dv_dt(w);
    
    /* 更新健康度 */
    health_calculate(w->rms, w->dv_dt, w->peak);
    
    if (w->rms > VOLTAGE_SWELL_THRESHOLD) return FAULT_VOLTAGE_SWELL;
    if (w->rms > VOLTAGE_OVER_LIMIT)      return FAULT_OVER_VOLTAGE;
    if (w->rms < VOLTAGE_SAG_THRESHOLD)   return FAULT_VOLTAGE_SAG;
    if (w->rms < VOLTAGE_UNDER_LIMIT)     return FAULT_UNDER_VOLTAGE;
    
    return FAULT_NONE;
}

/*============================================================================
 *                  classify_severity: 根据健康度和RMS判定级别
 *============================================================================*/
SeverityLevel_t classify_severity(FaultType_t f, float rms)
{
    float health_score = health_get_score();
    
    /* 先根据健康度评估 */
    if (health_score < HEALTH_SCORE_DANGER) {
        return SEVERITY_DANGER;
    }
    
    if (health_score < HEALTH_SCORE_WARNING) {
        return SEVERITY_WARNING;
    }
    
    /* 再根据RMS阈值 */
    // if (f == FAULT_VOLTAGE_SAG || f == FAULT_VOLTAGE_SWELL) {
    //     return (rms < VOLTAGE_NOMINAL * 0.5f || rms > VOLTAGE_NOMINAL * 1.3f)
    //            ? SEVERITY_DANGER : SEVERITY_WARNING;
    // }
    
    // if (f == FAULT_OVER_VOLTAGE) {
    //     return (rms > VOLTAGE_NOMINAL * 1.3f) ? SEVERITY_DANGER : SEVERITY_WARNING;
    // }
    
    return SEVERITY_WARNING;
}

/*============================================================================
 *                  trigger_fault: 触发故障模式
 *============================================================================*/
void trigger_fault(FaultType_t f, SeverityLevel_t s, uint32_t fault_time)
{
    log_warn("FAULT: type=%d sev=%d rms=%.1f at %dms", f, s, g_mid.rms, fault_time);

    wave_set_fault_rms(g_mid.rms);

    if (s == SEVERITY_WARNING) {
        switch_to_warning_mode();
    } else {
        switch_to_danger_mode();
    }
}

/*============================================================================
 *    node_upload_by_timestamp: 从RAM环缓冲查找最近1周期上传
 *============================================================================*/
/* >>> MARKER_20250604_VOLATILE_24FIELDS -- 搜这个确认 Keil 里是最新文件 <<< */
void node_upload_by_timestamp(uint32_t poll_ts)
{
    static uint8_t s_seq = 0;
    uint16_t start = (uint16_t)s_seq * 20;
    s_seq = (s_seq + 1) % 4;
    (void)poll_ts;

    NodeUploadHeader_t d;
    memset(&d, 0, sizeof(d));
    d.data_type    = DATA_TYPE_NODE_HEAD;
    d.severity     = (SeverityLevel_t)get_system_mode();
    d.fault_type   = FAULT_NONE;
    d.fault_pending = (g_pending_fault_count > 0) ? 1 : 0;
    d.node_index   = g_active_node;
    d.sample_rate  = NODE_SAMPLE_RATE;
    d.health_score = health_get_score();
    d.total_points = NORMAL_UPLOAD_POINTS;
    send_normal_data(DATA_TYPE_NODE_HEAD, &d, sizeof(d));

    static volatile NodeSample_t s_volatile;  /* volatile 防止编译器优化掉写入 */
    uint16_t sent = 0;
    while (sent < NORMAL_UPLOAD_POINTS) {
        uint16_t idx = start + sent;
        const ZDataPoint_t *z;
        if (idx < ZDATA_NORMAL_POINTS)
            z = &g_zdata_normal[idx];
        else
            z = &g_zdata_fault[idx - ZDATA_NORMAL_POINTS];

        volatile int16_t *p = (volatile int16_t *)&s_volatile;
        p[0]  = (int16_t)(z->pg1 * 10000.0f);
        p[1]  = (int16_t)(z->pg2 * 10000.0f);
        p[2]  = (int16_t)(z->pg3 * 10000.0f);
        p[3]  = (int16_t)(z->qg1 * 10000.0f);
        p[4]  = (int16_t)(z->qg2 * 10000.0f);
        p[5]  = (int16_t)(z->qg3 * 10000.0f);
        p[6]  = (int16_t)(z->vmag1 * 10000.0f);
        p[7]  = (int16_t)(z->vmag2 * 10000.0f);
        p[8]  = (int16_t)(z->vmag3 * 10000.0f);
        p[9]  = (int16_t)(z->vmag4 * 10000.0f);
        p[10] = (int16_t)(z->vmag5 * 10000.0f);
        p[11] = (int16_t)(z->vmag6 * 10000.0f);
        p[12] = (int16_t)(z->vmag7 * 10000.0f);
        p[13] = (int16_t)(z->vmag8 * 10000.0f);
        p[14] = (int16_t)(z->vmag9 * 10000.0f);
        p[15] = (int16_t)(z->vangle1 * 10000.0f);
        p[16] = (int16_t)(z->vangle2 * 10000.0f);
        p[17] = (int16_t)(z->vangle3 * 10000.0f);
        p[18] = (int16_t)(z->vangle4 * 10000.0f);
        p[19] = (int16_t)(z->vangle5 * 10000.0f);
        p[20] = (int16_t)(z->vangle6 * 10000.0f);
        p[21] = (int16_t)(z->vangle7 * 10000.0f);
        p[22] = (int16_t)(z->vangle8 * 10000.0f);
        p[23] = (int16_t)(z->vangle9 * 10000.0f);
        *(volatile uint32_t *)(p + 24) = idx;

        /* >>> HARDCODED_TEST -- PG2=0x4E21 PG3=0x4E21 证明函数被编译执行 <<< */
        p[1] = 0x4E21;
        p[2] = 0x4E21;

        send_waveform_packet((const uint8_t *)&s_volatile, sizeof(NodeSample_t), DATA_TYPE_NODE_RAW);
        sent++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    g_last_poll_ts = poll_ts;
}

/*============================================================================
 *                  底层发送
 *============================================================================*/
#define LORA_HEADER_MAX   40
#define LORA_PAYLOAD_MAX  220

void send_normal_data(DataType_t type, void *data, uint16_t len)
{
    LoRaSendPacket_t p;
    p.data_type = type;
    p.data_len = (len > LORA_HEADER_MAX) ? LORA_HEADER_MAX : len;
    p.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    p.dest.my_addr = LORA_ADDR;
    p.dest.channel = LORA_CHN;
    memcpy(p.data, data, p.data_len);
    xQueueSendToBack(g_lora_send_queue, &p, portMAX_DELAY);
}

void send_waveform_packet(const uint8_t *data, uint16_t len, DataType_t type)
{
    LoRaSendPacket_t p;
    p.data_type = type;
    p.data_len = (len > LORA_PAYLOAD_MAX) ? LORA_PAYLOAD_MAX : len;
    p.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    p.dest.my_addr = LORA_ADDR;
    p.dest.channel = LORA_CHN;
    memcpy(p.data, data, p.data_len);
    xQueueSendToBack(g_lora_send_queue, &p, portMAX_DELAY);
}

/*============================================================================
 *                  辅助查询
 *============================================================================*/
NodeSample_t *get_node_sample(uint16_t index)
{
    if (index >= NODE_BUFFER_SIZE) return NULL;
    return &g_node.buffer[index % NODE_BUFFER_SIZE];
}
