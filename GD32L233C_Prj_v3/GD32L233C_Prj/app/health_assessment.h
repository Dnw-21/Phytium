#ifndef __HEALTH_ASSESSMENT_H
#define __HEALTH_ASSESSMENT_H

#include <stdint.h>

#define RMS_DEVIATION_MAX         5.0f  /* 最大允许电压偏差 百分比 */
#define DV_DT_MAX                 5000.0f
#define PEAK_DEVIATION_MAX        5.0f  /* 最大允许峰值偏差 百分比 */

#define HEALTH_SCORE_NORMAL      90 // 正常健康分数
#define HEALTH_SCORE_WARNING     80 // 警告健康分数
#define HEALTH_SCORE_DANGER      60 // 危险健康分数

typedef enum {
    RISK_LEVEL_NORMAL = 0,
    RISK_LEVEL_WARNING,
    RISK_LEVEL_DANGER
} RiskLevel_t;

typedef struct {
    float rms_score;
    float dv_dt_score;
    float peak_score;
    float health_score;
    RiskLevel_t risk_level;
} HealthState_t;

typedef struct {
    float rms;
    float dv_dt;
    float peak;
    float health_score;
    RiskLevel_t risk_level;
} HealthReport_t;

void health_assessment_init(void);
void health_calculate(float rms, float dv_dt, float peak);

float health_get_score(void);
RiskLevel_t health_get_risk_level(void);

#endif