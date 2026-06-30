#ifndef __NODE_DATA_H
#define __NODE_DATA_H

#include <stdint.h>

#define NODE_NORMAL_POINTS     60
#define NODE_FAULT_POINTS    20
#define NODE_TOTAL_POINTS     (NODE_NORMAL_POINTS + NODE_FAULT_POINTS)

typedef struct {
    float active_power;
    float reactive_power;
    float voltage_mag_a;
    float voltage_mag_b;
    float voltage_mag_c;
    float voltage_angle_a;
    float voltage_angle_b;
    float voltage_angle_c;
} NodeDataPoint_t;

extern const NodeDataPoint_t g_node_normal[NODE_NORMAL_POINTS];
extern const NodeDataPoint_t g_node_fault[NODE_FAULT_POINTS];

uint32_t node_get_normal_count(void);
uint32_t node_get_fault_count(void);
uint32_t node_get_total_count(void);

#endif /* __NODE_DATA_H */
