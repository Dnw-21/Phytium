#ifndef __ZDATA_ADAPTIVE_H
#define __ZDATA_ADAPTIVE_H
#include <stdint.h>
#define ZDATA_NORMAL_POINTS  60
#define ZDATA_FAULT_POINTS   20
#define ZDATA_TOTAL_POINTS   (ZDATA_NORMAL_POINTS + ZDATA_FAULT_POINTS)
typedef struct { float pg1,pg2,pg3, qg1,qg2,qg3; float vmag1,vmag2,vmag3,vmag4,vmag5,vmag6,vmag7,vmag8,vmag9; float vangle1,vangle2,vangle3,vangle4,vangle5,vangle6,vangle7,vangle8,vangle9; } ZDataPoint_t;
extern const ZDataPoint_t g_zdata_normal[ZDATA_NORMAL_POINTS];
extern const ZDataPoint_t g_zdata_fault[ZDATA_FAULT_POINTS];
uint32_t zdata_get_normal_count(void);
uint32_t zdata_get_fault_count(void);
uint32_t zdata_get_total_count(void);
#endif
