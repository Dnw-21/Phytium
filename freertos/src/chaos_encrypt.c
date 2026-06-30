/**
 * @file    chaos_encrypt.c
 * @brief   混沌加密模块实现 — 跨平台一致版
 * @details 用自定义 sinf/cosf 替代标准库, 保证 ARMCC/GCC/x86_64 结果完全相同
 *          编译要求: GCC 需加 -ffp-contract=off -ffloat-store
 */

#include "chaos_encrypt.h"
#include <string.h>

/* ===================================================================
 *  跨平台一致的 sinf / cosf 实现 (fmodf + Taylor 级数)
 *  三个平台 (ARMCC/GCC x86_64/GCC AArch64) 验证通过, 输出完全相同
 * =================================================================== */
#include <math.h>   /* 仅用于 fmodf */

static const float CHAOS_PI     = 3.14159265358979323846f;
static const float CHAOS_PI_HALF = 1.57079632679489661923f;
static const float CHAOS_2PI    = 6.28318530717958647692f;

static float chaos_sinf(float x)
{
    x = fmodf(x, CHAOS_2PI);
    if (x >  CHAOS_PI)      x -= CHAOS_2PI;
    if (x < -CHAOS_PI)      x += CHAOS_2PI;

    float x2 = x * x;
    float x3 = x * x2;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static float chaos_cosf(float x)
{
    return chaos_sinf(x + CHAOS_PI_HALF);
}
/* =================================================================== */

static float g_x = 0.1f;
static float g_y = 0.2f;

static ChaosParams_t g_params = {
    .a = 2.5f,
    .b0 = 1.0f,
    .b1 = 3.0f,
    .c = 2.5f,
    .d0 = 1.0f,
    .d1 = 3.0f,
    .phi = {0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.2f}
};

static uint8_t g_key_stream[KEY_STREAM_SIZE];
static uint16_t g_key_index = 0;

static void chaos_iterate(void)
{
    float x_new = g_params.a * chaos_cosf(g_x + g_params.phi[0]) +
                  g_params.b0 * chaos_sinf(g_params.b1 * chaos_sinf(g_y + g_params.phi[1]) + g_params.phi[2]);

    float y_new = g_params.c * chaos_cosf(g_y + g_params.phi[3]) +
                  g_params.d0 * chaos_sinf(g_params.d1 * chaos_sinf(g_x + g_params.phi[4]) + g_params.phi[5]);

    g_x = x_new;
    g_y = y_new;
}

static uint8_t chaos_generate_byte(void)
{
    for (int i = 0; i < 4; i++) {
        chaos_iterate();
    }
    uint32_t x_bits;
    memcpy(&x_bits, &g_x, sizeof(float));
    uint32_t y_bits;
    memcpy(&y_bits, &g_y, sizeof(float));

    uint8_t byte = (uint8_t)((x_bits ^ y_bits) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 8) & 0xFF);
    byte ^= (uint8_t)((y_bits >> 16) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 24) & 0xFF);

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

void chaos_init(uint32_t seed)
{
    g_x = 0.1f + (seed & 0xFFFF) / 65536.0f * 0.5f;
    g_y = 0.2f + ((seed >> 16) & 0xFFFF) / 65536.0f * 0.5f;
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

uint64_t chaos_get_sync_code(void)
{
    uint32_t x_bits, y_bits;
    memcpy(&x_bits, &g_x, sizeof(float));
    memcpy(&y_bits, &g_y, sizeof(float));

    return ((uint64_t)x_bits << 32) | (uint64_t)y_bits;
}

void chaos_sync_from_code(uint64_t sync_code)
{
    uint32_t x_bits = (uint32_t)(sync_code >> 32);
    uint32_t y_bits = (uint32_t)(sync_code & 0xFFFFFFFF);

    memcpy(&g_x, &x_bits, sizeof(float));
    memcpy(&g_y, &y_bits, sizeof(float));

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

uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint64_t *sync_code)
{
    if (input == NULL || output == NULL || input_len == 0 || input_len > MAX_ENCRYPT_DATA_LEN) {
        return 0;
    }

    *sync_code = chaos_get_sync_code();
    chaos_generate_key_stream();
    memcpy(output, input, input_len);
    chaos_encrypt_block(output, input_len);

    return input_len;
}

uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint64_t sync_code)
{
    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }

    chaos_sync_from_code(sync_code);
    memcpy(output, input, input_len);
    chaos_decrypt_block(output, input_len);

    return input_len;
}
