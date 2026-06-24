#ifndef __CHAOS_ENCRYPT_H
#define __CHAOS_ENCRYPT_H

#include <stdint.h>
#define CHAOS_WARMUP_ITERATIONS  1000
#define KEY_STREAM_SIZE          256
#define MAX_ENCRYPT_DATA_LEN     220

typedef struct {
    float a;
    float b0;
    float b1;
    float c;
    float d0;
    float d1;
    float phi[6];
} ChaosParams_t;

void chaos_init(uint32_t seed);
void chaos_set_params(const ChaosParams_t *params);

uint64_t chaos_get_sync_code(void);
void chaos_sync_from_code(uint64_t sync_code);

uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len,
                               uint8_t *output, uint64_t *sync_code);
uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len,
                               uint8_t *output, uint64_t sync_code);

void chaos_encrypt_block(uint8_t *data, uint16_t len);
void chaos_decrypt_block(uint8_t *data, uint16_t len);

#endif /* __CHAOS_ENCRYPT_H */
