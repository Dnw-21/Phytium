#ifndef __CHAOS_ENCRYPT_H
#define __CHAOS_ENCRYPT_H

#include <stdint.h>
#define CHAOS_WARMUP_ITERATIONS  1000
#define KEY_STREAM_SIZE          256
#define MAX_ENCRYPT_DATA_LEN     212                             /* 240 - 28B帧开销 */
#define CHAOS_SYNC_SIZE          16                              /* 4×float = 16字节 */

/**
 * @brief 4D混沌系统参数
 *        模型: x_{k,n+1} = μ·x_{k,n} + Σ G·trig(x_{j,n}+φ)
 *        G11=(g11,g22,g33,g44)  G12=(g14,g21,g32,g43)  G21=(g12,g23,g34,g41)
 */
typedef struct {
    float mu;                 /**< 系统参数 μ                     */
    float g11, g22, g33, g44; /**< 对角sin项: (-4,-4,-4,-4)      */
    float g14, g21, g32, g43; /**< 耦合sin/cos项1: (0.5,...)     */
    float g12, g23, g34, g41; /**< 耦合sin/cos项2: (-0.5,...)    */
    float phi[4];             /**< 相位: (-π/3, 0, π/3, 2π/3)   */
} ChaosParams_t;

void chaos_init(uint32_t seed);
void chaos_set_params(const ChaosParams_t *params);

void chaos_get_sync_code(uint8_t sync_code[CHAOS_SYNC_SIZE]);
void chaos_sync_from_code(const uint8_t sync_code[CHAOS_SYNC_SIZE]);

uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len,
                               uint8_t *output, uint8_t sync_code[CHAOS_SYNC_SIZE]);
uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len,
                               uint8_t *output, const uint8_t sync_code[CHAOS_SYNC_SIZE]);

void chaos_encrypt_block(uint8_t *data, uint16_t len);
void chaos_decrypt_block(uint8_t *data, uint16_t len);

#endif /* __CHAOS_ENCRYPT_H */
