#ifndef __SIM_DATA_H
#define __SIM_DATA_H

#include <stdint.h>

#define NODE_COUNT           1
#define NODE_TIME_POINTS     60

typedef struct {
    float active_power;       /* 有功功率 (W) */
    float reactive_power;     /* 无功功率 (Var) */
    float voltage_mag;        /* 电压幅值 (V) */
    float voltage_angle;      /* 电压相角 (°) */
} NodeDataPoint_t;

extern const NodeDataPoint_t g_node_data[NODE_COUNT][NODE_TIME_POINTS];

uint32_t get_time_point_count(void);

#endif