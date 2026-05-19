#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

typedef struct {
    float a, b0, b1, c, d0, d1;
    float phi[6];
} ChaosParams;

#define KEY_STREAM_SIZE  (2048)
#define CHAOS_WARMUP     (200)

static float g_cx = 0.1f;
static float g_cy = 0.2f;
static uint8_t g_key_stream[KEY_STREAM_SIZE];
static uint16_t g_key_idx = 0;

static const ChaosParams g_default_params = {
    .a = 2.5f, .b0 = 1.0f, .b1 = 3.0f,
    .c = 2.5f, .d0 = 1.0f, .d1 = 3.0f,
    .phi = {0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.2f}
};

static void chaos_iterate(const ChaosParams *p)
{
    float xn = p->a * cosf(g_cx + p->phi[0]) +
               p->b0 * sinf(p->b1 * sinf(g_cy + p->phi[1]) + p->phi[2]);
    float yn = p->c * cosf(g_cy + p->phi[3]) +
               p->d0 * sinf(p->d1 * sinf(g_cx + p->phi[4]) + p->phi[5]);
    g_cx = xn;
    g_cy = yn;
}

static uint8_t chaos_byte(void)
{
    for (int i = 0; i < 4; i++) chaos_iterate(&g_default_params);
    uint32_t xb, yb;
    memcpy(&xb, &g_cx, sizeof(float));
    memcpy(&yb, &g_cy, sizeof(float));
    uint8_t b = (uint8_t)((xb ^ yb) & 0xFF);
    b ^= (uint8_t)((xb >> 8) & 0xFF);
    b ^= (uint8_t)((yb >> 16) & 0xFF);
    b ^= (uint8_t)((xb >> 24) & 0xFF);
    return b;
}

static void chaos_gen_stream(void)
{
    for (int i = 0; i < KEY_STREAM_SIZE; i++)
        g_key_stream[i] = chaos_byte();
    g_key_idx = 0;
}

static uint8_t chaos_next(void)
{
    uint8_t b = g_key_stream[g_key_idx++];
    if (g_key_idx >= KEY_STREAM_SIZE) chaos_gen_stream();
    return b;
}

static void chaos_scramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    for (uint16_t i = 0; i < len / 2; i++) {
        uint8_t k = chaos_next();
        uint16_t j = i + (k % (len - i));
        uint8_t t = data[i]; data[i] = data[j]; data[j] = t;
    }
}

static void chaos_unscramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    uint16_t n = len / 2;
    if (n > 256) n = 256;
    uint8_t rec[256];
    for (uint16_t i = 0; i < n; i++) rec[i] = chaos_next();
    for (int16_t i = n - 1; i >= 0; i--) {
        uint16_t j = i + (rec[i] % (len - i));
        uint8_t t = data[i]; data[i] = data[j]; data[j] = t;
    }
}

static void chaos_xor(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        data[i] ^= chaos_next();
}

static void chaos_init(uint32_t seed)
{
    g_cx = 0.1f + (seed & 0xFFFF) / 65536.0f * 0.5f;
    g_cy = 0.2f + ((seed >> 16) & 0xFFFF) / 65536.0f * 0.5f;
    g_key_idx = 0;
    for (int i = 0; i < CHAOS_WARMUP; i++) chaos_byte();
    chaos_gen_stream();
}

static int chaos_encrypt(uint8_t *data, uint16_t len, uint32_t seed)
{
    chaos_init(seed);
    chaos_xor(data, len);
    chaos_init(seed);
    chaos_scramble(data, len);
    return 0;
}

static int chaos_decrypt(uint8_t *data, uint16_t len, uint32_t seed)
{
    chaos_init(seed);
    chaos_unscramble(data, len);
    chaos_init(seed);
    chaos_xor(data, len);
    return 0;
}

static int test_round_trip(const char *label, uint16_t len, uint32_t seed)
{
    uint8_t *orig = (uint8_t *)malloc(len);
    uint8_t *work = (uint8_t *)malloc(len);
    if (!orig || !work) { free(orig); free(work); return 0; }

    for (uint16_t i = 0; i < len; i++)
        orig[i] = (uint8_t)(i * 7 + 0xA5 + seed);

    memcpy(work, orig, len);
    chaos_encrypt(work, len, seed);
    chaos_decrypt(work, len, seed);

    int ok = (memcmp(orig, work, len) == 0);
    printf("  [%s] len=%4u seed=0x%08X %s\n", label, len, seed,
           ok ? "PASS" : "FAIL");
    free(orig); free(work);
    return ok;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  TC04: Chaos Encrypt/Decrypt Test           │\n");
    printf("│  Local computation: encrypt→decrypt→verify  │\n");
    printf("└─────────────────────────────────────────────┘\n\n");

    int pass = 0, fail = 0;

    int r = test_round_trip("small  ",  16, 0x12345678); r ? pass++ : fail++;
    r = test_round_trip("medium ", 128, 0xDEADBEEF); r ? pass++ : fail++;
    r = test_round_trip("large  ", 512, 0xCAFEBABE); r ? pass++ : fail++;
    r = test_round_trip("unalign", 255, 0x01010101); r ? pass++ : fail++;
    r = test_round_trip("single ",   1, 0xFFFFFFFF); r ? pass++ : fail++;
    r = test_round_trip("multi  ", 200, 0x55AA55AA); r ? pass++ : fail++;

    printf("\n─── Results ───\n");
    printf("  PASS: %d  FAIL: %d  TOTAL: %d\n", pass, fail, pass + fail);

    if (fail > 0) {
        printf("\n[TC04] RESULT: FAIL (%d/%d)\n", fail, pass + fail);
        return 1;
    }
    printf("\n[TC04] RESULT: PASS (all %d rounds consistent)\n", pass);
    return 0;
}