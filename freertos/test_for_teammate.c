/* ===================================================================
 *  混沌加解密跨平台验证 —— 发给队友在 GD32 (ARMCC) 上编译运行
 *
 *  编译: 用 GD32 的 ARMCC 编译器, printf 改为串口输出
 *  对比: 把输出的 key[0..15] 和主控飞腾派 (AArch64 GCC) 对比
 *
 *  如果 key 不同 → sinf/cosf 差异导致, 跟移植无关
 *  如果 key 相同 → 移植有问题
 * =================================================================== */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ---- 以下直接取自 chaos_encrypt.c, 不要改 ---- */
#define KEY_STREAM_SIZE  256

static float g_x = 0.1f;
static float g_y = 0.2f;

static struct {
    float a, b0, b1, c, d0, d1;
    float phi[6];
} g_params = {
    2.5f, 1.0f, 3.0f, 2.5f, 1.0f, 3.0f,
    {0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.2f}
};

static uint8_t  g_key_stream[KEY_STREAM_SIZE];
static uint16_t g_key_index = 0;

static void chaos_iterate(void) {
    float x_new = g_params.a * cosf(g_x + g_params.phi[0])
                + g_params.b0 * sinf(g_params.b1 * sinf(g_y + g_params.phi[1]) + g_params.phi[2]);
    float y_new = g_params.c * cosf(g_y + g_params.phi[3])
                + g_params.d0 * sinf(g_params.d1 * sinf(g_x + g_params.phi[4]) + g_params.phi[5]);
    g_x = x_new;
    g_y = y_new;
}

static uint8_t chaos_generate_byte(void) {
    for (int i = 0; i < 4; i++) chaos_iterate();
    uint32_t xb, yb;
    memcpy(&xb, &g_x, sizeof(float));
    memcpy(&yb, &g_y, sizeof(float));
    uint8_t b = (uint8_t)((xb ^ yb) & 0xFF);
    b ^= (uint8_t)((xb >> 8) & 0xFF);
    b ^= (uint8_t)((yb >> 16) & 0xFF);
    b ^= (uint8_t)((xb >> 24) & 0xFF);
    return b;
}

static void chaos_generate_key_stream(void) {
    for (uint16_t i = 0; i < KEY_STREAM_SIZE; i++)
        g_key_stream[i] = chaos_generate_byte();
    g_key_index = 0;
}

static uint8_t chaos_next_byte(void) {
    uint8_t b = g_key_stream[g_key_index];
    g_key_index++;
    if (g_key_index >= KEY_STREAM_SIZE) chaos_generate_key_stream();
    return b;
}

void chaos_sync_from_code(uint64_t sync_code) {
    uint32_t xb = (uint32_t)(sync_code >> 32);
    uint32_t yb = (uint32_t)(sync_code & 0xFFFFFFFF);
    memcpy(&g_x, &xb, sizeof(float));
    memcpy(&g_y, &yb, sizeof(float));
    chaos_generate_key_stream();
}
/* ---- 以上直接取自 chaos_encrypt.c ---- */

int main(void) {
    /* 固定 sync_code */
    uint64_t sync = 0x3F0ADB73BED33EC5ULL;

    chaos_sync_from_code(sync);

    /* 取前 16 个密钥字节 */
    uint8_t key[16];
    for (int i = 0; i < 16; i++)
        key[i] = chaos_next_byte();

    printf("sync=0x%016llX\r\n", (unsigned long long)sync);
    printf("key=");
    for (int i = 0; i < 16; i++)
        printf("%02X ", key[i]);
    printf("\r\n");

    return 0;
}
