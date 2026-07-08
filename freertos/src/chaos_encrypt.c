/**
 * @file    chaos_encrypt.c
 * @brief   4D混沌加密模块实现 — 移植自 GD32L233C_Prj_Master
 * @details 使用4D混沌系统生成密钥流，实现数据加密解密
 *
 * @section 混沌方程 (4D耦合正弦映射)
 * @code
 *   x1_{n+1} = μ·x1_n + g11·sin(x1_n) + g12·cos(x2_n+φ1) + g14·sin(x4_n)
 *   x2_{n+1} = μ·x2_n + g21·sin(x1_n) + g22·sin(x2_n) + g23·cos(x3_n+φ2)
 *   x3_{n+1} = μ·x3_n + g32·sin(x2_n) + g33·sin(x3_n) + g34·cos(x4_n+φ3)
 *   x4_{n+1} = μ·x4_n + g41·cos(x1_n+φ4) + g43·sin(x3_n) + g44·sin(x4_n)
 */

#include "chaos_encrypt.h"
#include <math.h>
#include <string.h>

static float g_x[4] = {0.1f, 0.1f, 0.1f, 0.1f};  /**< 混沌状态 x1~x4 */

/**
 * @brief 4D混沌系统默认参数
 *        μ=0.5, G11=(-4,-4,-4,-4), G12=(0.5,0.5,0.5,0.5)
 *        G21=(-0.5,-0.5,-0.5,-0.5), Φ=(-π/3, 0, π/3, 2π/3)
 */
static ChaosParams_t g_params = {
    .mu  = 0.5f,
    .g11 = -4.0f, .g22 = -4.0f, .g33 = -4.0f, .g44 = -4.0f,
    .g14 =  0.5f, .g21 =  0.5f, .g32 =  0.5f, .g43 =  0.5f,
    .g12 = -0.5f, .g23 = -0.5f, .g34 = -0.5f, .g41 = -0.5f,
    .phi = {-1.0471975512f, 0.0f, 1.0471975512f, 2.0943951024f}
};

static uint8_t g_key_stream[KEY_STREAM_SIZE];
static uint16_t g_key_index = 0;

/* ===================================================================
 *  跨平台一致的 sinf / cosf 实现 (fmodf + Taylor 级数)
 *  三个平台 (ARMCC/GCC x86_64/GCC AArch64) 验证通过, 输出完全相同
 * =================================================================== */
static const float PI = 3.14159265358979323846f;
static const float PI_2 = 1.57079632679489661923f;

static float fast_sinf(float x)
{
    x = fmodf(x, 2 * PI);
    if (x >  PI) x -= 2*PI;
    if (x < -PI) x += 2*PI;

    float x2 = x * x;
    float x3 = x * x2;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3/6.0f + x5/120.0f - x7/5040.0f;
}

static float fast_cosf(float x)
{
    return fast_sinf(x + PI_2);
}
/* =================================================================== */

/**
 * @brief  混沌迭代 - 执行一次4D混沌方程计算
 * @note   更新全局状态 g_x[0..3]
 */
static void chaos_iterate(void)
{
    float x1 = g_x[0], x2 = g_x[1], x3 = g_x[2], x4 = g_x[3];
    float p1 = g_params.phi[0], p2 = g_params.phi[1];
    float p3 = g_params.phi[2], p4 = g_params.phi[3];

    float nx1 = g_params.mu  * x1
              + g_params.g11 * fast_sinf(x1)
              + g_params.g12 * fast_cosf(x2 + p1)
              + g_params.g14 * fast_sinf(x4);

    float nx2 = g_params.mu  * x2
              + g_params.g21 * fast_sinf(x1)
              + g_params.g22 * fast_sinf(x2)
              + g_params.g23 * fast_cosf(x3 + p2);

    float nx3 = g_params.mu  * x3
              + g_params.g32 * fast_sinf(x2)
              + g_params.g33 * fast_sinf(x3)
              + g_params.g34 * fast_cosf(x4 + p3);

    float nx4 = g_params.mu  * x4
              + g_params.g41 * fast_cosf(x1 + p4)
              + g_params.g43 * fast_sinf(x3)
              + g_params.g44 * fast_sinf(x4);

    g_x[0] = nx1;
    g_x[1] = nx2;
    g_x[2] = nx3;
    g_x[3] = nx4;
}

static uint8_t chaos_generate_byte(void)
{
    for (int i = 0; i < 4; i++) {
        chaos_iterate();
    }
    uint32_t x1_bits, x2_bits;
    memcpy(&x1_bits, &g_x[0], sizeof(float));
    memcpy(&x2_bits, &g_x[1], sizeof(float));

    uint8_t byte = (uint8_t)((x1_bits ^ x2_bits) & 0xFF);
    byte ^= (uint8_t)((x1_bits >> 8) & 0xFF);
    byte ^= (uint8_t)((x2_bits >> 16) & 0xFF);
    byte ^= (uint8_t)((x1_bits >> 24) & 0xFF);

    return byte;
}

static void chaos_generate_key_stream(void)
{
    for (uint16_t i = 0; i < KEY_STREAM_SIZE; i++) {
        g_key_stream[i] = chaos_generate_byte();
    }
    g_key_index = 0;
}

static uint8_t chaos_next_byte(void)
{
    uint8_t byte = g_key_stream[g_key_index];
    g_key_index++;

    if (g_key_index >= KEY_STREAM_SIZE) {
        chaos_generate_key_stream();
    }

    return byte;
}

/*============================================================================*/
/*                              公共函数                                       */
/*============================================================================*/

void chaos_init(uint32_t seed)
{
    g_x[0] = 0.1f + ((seed >> 24) & 0xFF) / 255.0f * 0.5f;
    g_x[1] = 0.1f + ((seed >> 16) & 0xFF) / 255.0f * 0.5f;
    g_x[2] = 0.1f + ((seed >>  8) & 0xFF) / 255.0f * 0.5f;
    g_x[3] = 0.1f + ((seed      ) & 0xFF) / 255.0f * 0.5f;
    g_key_index = 0;

    for (uint32_t i = 0; i < CHAOS_WARMUP_ITERATIONS; i++) {
        chaos_generate_byte();
    }

    chaos_generate_key_stream();
}

void chaos_set_params(const ChaosParams_t *params)
{
    if (params) {
        memcpy(&g_params, params, sizeof(ChaosParams_t));
    }
}

/**
 * @brief  获取16字节同步码 (4×float = x1~x4)
 */
void chaos_get_sync_code(uint8_t sync_code[CHAOS_SYNC_SIZE])
{
    memcpy(sync_code, g_x, sizeof(g_x));
}

/**
 * @brief  从16字节同步码恢复4D混沌状态
 */
void chaos_sync_from_code(const uint8_t sync_code[CHAOS_SYNC_SIZE])
{
    memcpy(g_x, sync_code, sizeof(g_x));
    chaos_generate_key_stream();
}

void chaos_encrypt_block(uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return;

    for (uint16_t i = 0; i < len; i++) {
        data[i] ^= chaos_next_byte();
    }
}

void chaos_decrypt_block(uint8_t *data, uint16_t len)
{
    chaos_encrypt_block(data, len);
}

/**
 * @brief  加密数据包
 * @return 加密后数据长度，0表示失败
 */
uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len,
                                uint8_t *output, uint8_t sync_code[CHAOS_SYNC_SIZE])
{
    if (input == NULL || output == NULL || input_len == 0 || input_len > MAX_ENCRYPT_DATA_LEN) {
        return 0;
    }

    chaos_get_sync_code(sync_code);
    chaos_generate_key_stream();
    memcpy(output, input, input_len);
    chaos_encrypt_block(output, input_len);

    return input_len;
}

uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len,
                                uint8_t *output, const uint8_t sync_code[CHAOS_SYNC_SIZE])
{
    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }

    chaos_sync_from_code(sync_code);
    memcpy(output, input, input_len);
    chaos_decrypt_block(output, input_len);

    return input_len;
}
