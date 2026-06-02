#include "health_assessment.h"
#include "data_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "log.h"
#include <string.h>
#include <math.h>

static HealthState_t g_health;

void health_assessment_init(void)
{
    memset(&g_health, 0, sizeof(HealthState_t));
    g_health.health_score = 100.0f;
    g_health.risk_level = RISK_LEVEL_NORMAL;
}

static float calc_rms_score(float rms)
{
    float dev = fabsf(rms - VOLTAGE_NOMINAL) / VOLTAGE_NOMINAL;
    if (dev >= RMS_DEVIATION_MAX) return 0.0f;  
    return 100.0f * (1.0f - dev / RMS_DEVIATION_MAX);
}

static float calc_dv_dt_score(float dv_dt)
{
    float dv = fabsf(dv_dt);
    if (dv >= DV_DT_MAX) return 0.0f;
    return 100.0f * (1.0f - dv / DV_DT_MAX);
}

static float calc_peak_score(float peak, float rms)
{
    float expected = 2.828f * rms;
    float dev = fabsf(peak - expected) / expected;
    if (dev >= PEAK_DEVIATION_MAX) return 0.0f;
    return 100.0f * (1.0f - dev / PEAK_DEVIATION_MAX);
}

void health_calculate(float rms, float dv_dt, float peak)
{
    g_health.rms_score    = calc_rms_score(rms);     /* 计算RMS电压分数 */
    g_health.dv_dt_score  = calc_dv_dt_score(dv_dt); /* 计算最大相邻差分斜率分数 */
    g_health.peak_score   = calc_peak_score(peak, rms); /* 计算峰值分数 */

    g_health.health_score = g_health.rms_score   * 0.40f +
                            g_health.dv_dt_score * 0.40f +
                            g_health.peak_score  * 0.20f;
    // log_info("Health Score: %.2f", g_health.health_score);
    
    if (g_health.health_score < HEALTH_SCORE_DANGER) {
        g_health.risk_level = RISK_LEVEL_DANGER;
    } else if (g_health.health_score < HEALTH_SCORE_WARNING) {
        g_health.risk_level = RISK_LEVEL_WARNING;
    } else {
        g_health.risk_level = RISK_LEVEL_NORMAL;
    }

}

float health_get_score(void)
{
    return g_health.health_score;
}

RiskLevel_t health_get_risk_level(void)
{
    return g_health.risk_level;
}