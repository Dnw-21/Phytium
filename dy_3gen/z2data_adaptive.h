#ifndef __Z2DATA_ADAPTIVE_H
#define __Z2DATA_ADAPTIVE_H

#include <stdint.h>

#define ZDATA_NORMAL_POINTS     60
#define ZDATA_FAULT_POINTS      20
#define ZDATA_TOTAL_POINTS      (ZDATA_NORMAL_POINTS + ZDATA_FAULT_POINTS)

typedef struct {
    float pg1;
    float pg2;
    float pg3;
    float qg1;
    float qg2;
    float qg3;
    float vmag1;
    float vmag2;
    float vmag3;
    float vmag4;
    float vmag5;
    float vmag6;
    float vmag7;
    float vmag8;
    float vmag9;
    float vangle1;
    float vangle2;
    float vangle3;
    float vangle4;
    float vangle5;
    float vangle6;
    float vangle7;
    float vangle8;
    float vangle9;
} Z2DataPoint_t;

extern const Z2DataPoint_t g_z2data_normal[ZDATA_NORMAL_POINTS];
extern const Z2DataPoint_t g_z2data_fault[ZDATA_FAULT_POINTS];

uint32_t z2data_get_normal_count(void);
uint32_t z2data_get_fault_count(void);
uint32_t z2data_get_total_count(void);

#endif /* __Z2DATA_ADAPTIVE_H */
