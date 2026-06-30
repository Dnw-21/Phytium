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

/**
 * @brief  字节置换 - 打乱数据顺序
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * 
 * @note   置换算法:
 *         1. 从前向后遍历数据
 *         2. 使用混沌密钥流决定交换位置
 *         3. 交换当前位置与目标位置的数据
 * 
 */
static void chaos_scramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    
    uint8_t key_byte;
    /* 遍历前半部分数据进行交换 */
    for (uint16_t i = 0; i < len / 2; i++) {
        /* 从密钥流获取一个字节，决定交换位置 */
        key_byte = chaos_next_byte();
        /* 计算交换目标位置: j ∈ [i, len-1] */
        uint16_t j = i + (key_byte % (len - i));
        
        /* 交换位置i和位置j的数据 */
        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
}

/**
 * @brief  字节逆置换 - 恢复数据顺序
 * @param  data 数据缓冲区
 * @param  len  数据长度
 * 
 * @note   逆置换算法:
 *         1. 先记录所有交换位置（需要相同的密钥流）
 *         2. 按相反顺序恢复交换
 */
static void chaos_unscramble(uint8_t *data, uint16_t len)
{
    if (len < 2) return;
    
    /* 记录交换位置的数组 */
    uint8_t swap_records[256];
    uint16_t swap_count = len / 2;
    if (swap_count > 256) swap_count = 256;
    
    /* 先读取所有密钥流字节，记录交换位置 */
    for (uint16_t i = 0; i < swap_count; i++) {
        swap_records[i] = chaos_next_byte();
    }
    
    /* 按相反顺序恢复交换 */
    for (int16_t i = swap_count - 1; i >= 0; i--) {
        /* 计算原始交换目标位置 */
        uint16_t j = i + (swap_records[i] % (len - i));
        
        /* 恢复交换（与原始交换操作相同，但顺序相反） */
        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
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
uint32_t chaos_get_sync_code(void)
{
    uint32_t code = 0;
    
    code |= ((uint32_t)(fabsf(g_x) * 10000) & 0xFFFF) << 16;
    code |= ((uint32_t)(fabsf(g_y) * 10000) & 0xFFFF);
    
    return code;
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
void chaos_sync_from_code(uint32_t sync_code)
{
    /* 从同步码解析x和y状态 */
    float x_hint = ((sync_code >> 16) & 0xFFFF) / 10000.0f;
    float y_hint = (sync_code & 0xFFFF) / 10000.0f;
    
    /* 恢复混沌状态（带有效性检查） */
    if (x_hint > 0 && x_hint < 100) g_x = x_hint;
    if (y_hint > 0 && y_hint < 100) g_y = y_hint;
    
    /* 
     * 直接生成密钥流，不执行预热迭代！
     * 
     * 原因：同步码记录的是加密前的混沌状态
     * 加密端在获取同步码时，已经完成了预热迭代
     * 解密端只需要恢复到相同状态，然后生成相同的密钥流
     */
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
 * @param  input     原始数据
 * @param  input_len 原始数据长度
 * @param  output    输出缓冲区（加密数据）
 * @param  sync_code 输出同步码
 * @return 加密后数据长度，0表示失败
 * 
 * @note   加密流程:
 *         1. 参数校验
 *         2. 获取同步码（记录加密前的混沌状态）
 *         3. 复制原始数据到输出缓冲区
 *         4. 字节置换（打乱数据顺序）
 *         5. XOR加密（与密钥流异或）
 * 
 * @details 同步码必须在加密操作前获取，因为加密过程会消耗密钥流，
 *          改变混沌状态。解密端需要用同步码恢复到加密前的状态，
 *          才能生成相同的密钥流进行解密。
 */
uint16_t chaos_encrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t *sync_code)
{
    /* 参数校验 */
    if (input == NULL || output == NULL || input_len == 0 || input_len > MAX_ENCRYPT_DATA_LEN) {
        return 0;
    }
    
    /* 步骤1: 获取同步码 - 记录加密前的混沌状态 */
    *sync_code = chaos_get_sync_code();
    
    /* 步骤2: 复制数据到输出缓冲区 */
    memcpy(output, input, input_len);
    
    /* 步骤3: 字节置换 - 打乱数据顺序 */
    chaos_scramble(output, input_len);
    
    /* 步骤4: XOR加密 - 与密钥流异或 */
    chaos_encrypt_block(output, input_len);
    
    return input_len;
}

/**
 * @brief  解密数据包
 * @param  input     加密数据
 * @param  input_len 加密数据长度
 * @param  output    输出缓冲区（原始数据）
 * @param  sync_code 同步码
 * @return 解密后数据长度，0表示失败
 * 
 * @note   解密流程:
 *         1. 参数校验
 *         2. 从同步码恢复混沌状态
 *         3. 复制加密数据到输出缓冲区
 *         4. XOR解密（与密钥流异或）
 *         5. 字节逆置换（恢复数据顺序）
 * 
 */
uint16_t chaos_decrypt_packet(const uint8_t *input, uint16_t input_len, uint8_t *output, uint32_t sync_code)
{
    /* 参数校验 */
    if (input == NULL || output == NULL || input_len == 0) {
        return 0;
    }
    
    /* 步骤1: 从同步码恢复混沌状态 */
    chaos_sync_from_code(sync_code);
    
    /* 步骤2: 复制数据到输出缓冲区 */
    memcpy(output, input, input_len);
    
    /* 步骤3: XOR解密 - 与密钥流异或 */
    chaos_decrypt_block(output, input_len);
    
    /* 步骤4: 字节逆置换 - 恢复数据顺序 */
    chaos_unscramble(output, input_len);
    
    return input_len;
}
