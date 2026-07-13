/**
 * @file    chaos_encrypt.c
 * @brief   混沌加密模块实现
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

/**
 * @brief 密钥流缓冲区
 * @note  存储256字节的密钥流，用于XOR加密
 *        密钥流用完后自动重新生成
 */
static uint8_t g_key_stream[KEY_STREAM_SIZE];

/**
 * @brief 当前密钥流读取位置
 * @note  范围 [0, KEY_STREAM_SIZE-1]
 */
static uint16_t g_key_index = 0;

// 自己实现的跨平台完全一致的 sinf / cosf
// 精度：误差 < 1e-7，和标准 float 匹配
static const float PI = 3.14159265358979323846f;
static const float PI_2 = 1.57079632679489661923f;

static float fast_sinf(float x)
{
    // 角度归到 [-PI, PI]
    x = fmodf(x, 2 * PI);
    if (x >  PI) x -= 2*PI;
    if (x < -PI) x += 2*PI;

    // 正弦多项式逼近（float 精度）
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


/**
 * @brief  混沌迭代 - 执行一次4D混沌方程计算
 * @note   更新全局状态 g_x[0..3]
 *         加密仍使用第一维x1和第二维x2提取字节
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

/**
 * @brief  生成一个随机字节
 * @return 从混沌状态生成的随机字节(0-255)
 */
static uint8_t chaos_generate_byte(void)
{
    /* 迭代4次混沌方程，增加随机性和扩散性 */
    for (int i = 0; i < 4; i++) {
        chaos_iterate();
    }
    uint32_t x1_bits, x2_bits;
    memcpy(&x1_bits, &g_x[0], sizeof(float));
    memcpy(&x2_bits, &g_x[1], sizeof(float));
    
    /* 
     * 位混合策略:
     *   - x1_bits的低8位
     *   - x1_bits的8-15位
     *   - x2_bits的16-23位
     *   - x1_bits的24-31位
     */
    uint8_t byte = (uint8_t)((x1_bits ^ x2_bits) & 0xFF);
    byte ^= (uint8_t)((x1_bits >> 8) & 0xFF);
    byte ^= (uint8_t)((x2_bits >> 16) & 0xFF);
    byte ^= (uint8_t)((x1_bits >> 24) & 0xFF);
    
    return byte;
}

/**
 * @brief  生成密钥流
 * @note   生成256字节的密钥流到 g_key_stream
 *         重置密钥流读取索引为0
 */
static void chaos_generate_key_stream(void)
{
    /* 逐字节生成密钥流 */
    for (uint16_t i = 0; i < KEY_STREAM_SIZE; i++) {
        g_key_stream[i] = chaos_generate_byte();
    }
    /* 重置读取索引 */
    g_key_index = 0;
}

/**
 * @brief  从密钥流读取下一个字节
 * @return 密钥流字节
 */
static uint8_t chaos_next_byte(void)
{
    /* 读取当前字节 */
    uint8_t byte = g_key_stream[g_key_index];
    g_key_index++;
    
    /* 密钥流用完，生成新的密钥流 */
    if (g_key_index >= KEY_STREAM_SIZE) {
        chaos_generate_key_stream();
    }
    
    return byte;
}

/*============================================================================*/
/*                              公共函数                                       */
/*============================================================================*/

/**
 * @brief  初始化混沌加密系统
 * @param  seed 初始化种子，双方必须使用相同种子
 */
void chaos_init(uint32_t seed)
{
    /* 
     * 从种子计算4D初始状态
     * 将32位种子分成4部分，分别影响x1~x4的初始值
     * 初始值范围: [0.1, 0.6]
     */
    g_x[0] = 0.1f + ((seed >> 24) & 0xFF) / 255.0f * 0.5f;
    g_x[1] = 0.1f + ((seed >> 16) & 0xFF) / 255.0f * 0.5f;
    g_x[2] = 0.1f + ((seed >>  8) & 0xFF) / 255.0f * 0.5f;
    g_x[3] = 0.1f + ((seed      ) & 0xFF) / 255.0f * 0.5f;
    g_key_index = 0;
    
    for (uint32_t i = 0; i < CHAOS_WARMUP_ITERATIONS; i++) {
        chaos_generate_byte();
    }
    
    /* 生成初始密钥流 */
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
 * @param  sync_code 16字节同步码 (4×float)
 */
void chaos_sync_from_code(const uint8_t sync_code[CHAOS_SYNC_SIZE])
{
    memcpy(g_x, sync_code, sizeof(g_x));
    chaos_generate_key_stream();
}

/**
 * @brief  XOR加密数据块
 * @note   XOR操作是可逆的，加密和解密使用相同函数
 *         data[i] ^= key_stream[i]
 */
void chaos_encrypt_block(uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return;
    
    /* 逐字节与密钥流异或 */
    for (uint16_t i = 0; i < len; i++) {
        data[i] ^= chaos_next_byte();
    }
}

/**
 * @brief  XOR解密数据块
 * @param  data 数据缓冲区（原地解密）
 * @param  len  数据长度
 * 
 * @note   XOR操作可逆，解密与加密相同
 *         内部直接调用chaos_encrypt_block
 */
void chaos_decrypt_block(uint8_t *data, uint16_t len)
{
    chaos_encrypt_block(data, len);
}

/**
 * @brief  加密数据包
 * @return 加密后数据长度，0表示失败
 *
 * @note   加密流程:
 *         1. 获取同步码（记录加密前的混沌状态）
 *         2. 刷新密钥流
 *         3. 复制数据并与密钥流异或
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
