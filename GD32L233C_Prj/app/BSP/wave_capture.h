#ifndef __WAVE_CAPTURE_H
#define __WAVE_CAPTURE_H

#include <stdint.h>

#define WAVE_LUT_SIZE           256
#define PINGPONG_BUF_SIZE       1280
#define PINGPONG_TOTAL_SIZE     (PINGPONG_BUF_SIZE * 2)

void wave_capture_init(void);
void wave_capture_start(uint16_t adc_rate);
void wave_capture_stop(void);

#endif