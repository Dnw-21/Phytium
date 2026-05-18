#include "chaos_encrypt.h"
#include <math.h>
#include <string.h>

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
    float x_new = g_params.a * cosf(g_x + g_params.phi[0]) +
                  g_params.b0 * sinf(g_params.b1 * sinf(g_y + g_params.phi[1]) + g_params.phi[2]);

    float y_new = g_params.c * cosf(g_y + g_params.phi[3]) +
                  g_params.d0 * sinf(g_params.d1 * sinf(g_x + g_params.phi[4]) + g_params.phi[5]);

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

static void chaos_scramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;

    for (uint16_t i = 0; i < len / 2; i++) {
        uint8_t key_byte = chaos_next_byte();
        uint16_t j = i + (key_byte % (len - i));

        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
}

static void chaos_unscramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;

    uint8_t swap_records[256];
    uint16_t swap_count = len / 2;
    if (swap_count > 256) swap_count = 256;

    for (uint16_t i = 0; i < swap_count; i++) {
        swap_records[i] = chaos_next_byte();
    }

    for (int16_t i = swap_count - 1; i >= 0; i--) {
        uint16_t j = i + (swap_records[i] % (len - i));

        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
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

uint32_t chaos_get_sync_code(void)
{
    uint32_t code = 0;

    code |= ((uint32_t)(fabsf(g_x) * 10000) & 0xFFFF) << 16;
    code |= ((uint32_t)(fabsf(g_y) * 10000) & 0xFFFF);

    return code;
}

void chaos_sync_from_code(uint32_t sync_code)
{
    float x_hint = ((sync_code >> 16) & 0xFFFF) / 10000.0f;
    float y_hint = (sync_code & 0xFFFF) / 10000.0f;

    if (x_hint > 0 && x_hint < 100) g_x = x_hint;
    if (y_hint > 0 && y_hint < 100) g_y = y_hint;

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

uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t *sync_code)
{
    if (input == NULL || output == NULL || input_len == 0 || input_len > MAX_ENCRYPT_DATA_LEN) {
        return 0;
    }

    *sync_code = chaos_get_sync_code();

    memcpy(output, input, input_len);

    chaos_scramble(output, input_len);

    chaos_encrypt_block(output, input_len);

    return input_len;
}

uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t sync_code)
{
    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }

    chaos_sync_from_code(sync_code);

    memcpy(output, input, input_len);

    chaos_decrypt_block(output, input_len);

    chaos_unscramble(output, input_len);

    return input_len;
}