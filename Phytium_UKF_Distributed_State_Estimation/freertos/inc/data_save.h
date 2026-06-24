#ifndef __DATA_SAVE_H
#define __DATA_SAVE_H

#include <stdint.h>

void data_save_init(void);
void data_save_frame(uint8_t rx_type, uint32_t sync_code,
                     const uint8_t *data, uint16_t len);

#endif
