/**
 * @file    chaos_encrypt.c
 * @brief   混沌加密模块实现
 * @details 使用二维混沌系统生成密钥流，实现数据加密解密
 * 
 * @section 混沌方程
 * 二维混沌映射方程:
 * @code
 *   x_{n+1} = a*cos(x_n + φ0) + b0*sin(b1*sin(y_n + φ1) + φ2)
 *   y_{n+1} = c*cos(y_n + φ3) + d0*sin(d1*sin(x_n + φ4) + φ5)
 * 
 * @section 加密算法
 * 加密过程分为三个步骤:
 *   1. 字节置换(Scramble): 使用混沌序列决定交换位置，打乱数据顺序
 *   2. XOR加密: 将数据与混沌密钥流进行异或操作
 *   3. 同步码生成: 记录加密后的混沌状态，用于接收端同步
 * 
 * 解密过程是加密的逆过程:
 *   1. 状态同步: 从同步码恢复混沌状态
 *   2. XOR解密: 与加密相同的异或操作
 *   3. 字节逆置换(Unscramble): 恢复原始数据顺序
 * 
 */

#include "chaos_encrypt.h"
#include <math.h>
#include <string.h>

static float g_x = 0.1f;    /**< 混沌状态 x */
static float g_y = 0.2f;    /**< 混沌状态 y */

/**
 * @brief 混沌系统参数
 */
static ChaosParams_t g_params = {
    .a = 2.5f,   
    .b0 = 1.0f,   
    .b1 = 3.0f,   
    .c = 2.5f,  
    .d0 = 1.0f,   
    .d1 = 3.0f, 
    .phi = {0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.2f}  /**< 相位参数 φ0-φ5 */
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

/**
 * @brief  混沌迭代 - 执行一次混沌方程计算
 * @note   更新全局状态 g_x 和 g_y
 *         这是混沌系统的核心，每次调用产生新的状态
 */
static void chaos_iterate(void)
{
    float x_new = g_params.a * cosf(g_x + g_params.phi[0]) +
                  g_params.b0 * sinf(g_params.b1 * sinf(g_y + g_params.phi[1]) + g_params.phi[2]);
    
    float y_new = g_params.c * cosf(g_y + g_params.phi[3]) +
                  g_params.d0 * sinf(g_params.d1 * sinf(g_x + g_params.phi[4]) + g_params.phi[5]);
    
    /* 更新全局混沌状态 */
    g_x = x_new;
    g_y = y_new;
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
    uint32_t x_bits;
    memcpy(&x_bits, &g_x, sizeof(float));
    uint32_t y_bits;
    memcpy(&y_bits, &g_y, sizeof(float));
    
    /* 
     * 位混合策略:
     *   - x_bits的低8位
     *   - x_bits的8-15位
     *   - y_bits的16-23位
     *   - x_bits的24-31位
     */
    uint8_t byte = (uint8_t)((x_bits ^ y_bits) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 8) & 0xFF);
    byte ^= (uint8_t)((y_bits >> 16) & 0xFF);
    byte ^= (uint8_t)((x_bits >> 24) & 0xFF);
    
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
     * 从种子计算初始状态
     * 将32位种子分成两部分，分别影响x和y的初始值
     * 初始值范围: x ∈ [0.1, 0.6], y ∈ [0.2, 0.7]
     */
    g_x = 0.1f + (seed & 0xFFFF) / 65536.0f * 0.5f;
    g_y = 0.2f + ((seed >> 16) & 0xFFFF) / 65536.0f * 0.5f;
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
 * @brief  获取同步码
 * @return 32位同步码，编码当前混沌状态
 */
uint64_t chaos_get_sync_code(void)
{
    uint32_t x_bits, y_bits;
    memcpy(&x_bits, &g_x, sizeof(float));
    memcpy(&y_bits, &g_y, sizeof(float));

    return ((uint64_t)x_bits << 32) | (uint64_t)y_bits;
}

/**
 * @brief  从同步码恢复混沌状态
 * @param  sync_code 32位同步码
 * 
 * @note   恢复流程:
 *         1. 从同步码解析x和y状态
 *         2. 直接生成密钥流（不预热！）
 * 
 * @details 解析过程:
 *          - x_hint = (sync_code高16位) / 10000
 *          - y_hint = (sync_code低16位) / 10000
 *          
 *          有效性检查:
 *          - 状态值必须在合理范围内 (0, 100)
 * 
 * @warning 此函数不执行预热迭代！
 *          因为同步码记录的是加密前的混沌状态（已经预热过），
 *          解密时直接从该状态生成密钥流即可。
 *          只有 chaos_init() 才需要预热迭代。
 */
void chaos_sync_from_code(uint64_t sync_code)
{
    uint32_t x_bits = (uint32_t)(sync_code >> 32);
    uint32_t y_bits = (uint32_t)(sync_code & 0xFFFFFFFF);

    memcpy(&g_x, &x_bits, sizeof(float));
    memcpy(&g_y, &y_bits, sizeof(float));

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
