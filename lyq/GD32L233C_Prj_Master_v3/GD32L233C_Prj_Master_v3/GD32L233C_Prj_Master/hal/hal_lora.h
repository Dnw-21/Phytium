#ifndef HAL_LORA_H
#define HAL_LORA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *  通用错误码
 *============================================================================*/
#define HAL_OK               0
#define HAL_ERR_TIMEOUT     -1
#define HAL_ERR_INVAL       -2
#define HAL_ERR_BUSY        -3
#define HAL_ERR_NOT_READY   -4
#define HAL_ERR_IO          -5

/**
 * @brief LoRa 模块运行状态
 */
typedef enum {
    HAL_LORA_STATE_CONFIG = 0,
    HAL_LORA_STATE_RX,
    HAL_LORA_STATE_TX
} hal_lora_state_t;

/**
 * @brief LoRa 模块配置参数结构体
 * @note  各字段含义与底层模块一致, 通过运行时接口读写
 */
typedef struct {
    uint16_t addr;
    uint8_t  chn;
    uint8_t  netid;
    uint8_t  power;
    uint8_t  wlrate;
    uint8_t  wltime;
    uint8_t  wmode;
    uint8_t  tmode;
    uint8_t  packsize;
    uint8_t  bps;
    uint8_t  parity;
    uint8_t  lbt;
} hal_lora_config_t;

/**
 * @brief LoRa 数据接收回调类型
 * @param data   接收到的数据指针
 * @param len    数据长度
 * @note  在任务上下文中调用, 可调用 FreeRTOS API
 */
typedef void (*hal_lora_rx_callback_t)(const uint8_t *data, uint16_t len);

/*============================================================================
 *  初始化和状态管理
 *============================================================================*/

/**
 * @brief 初始化 LoRa 模块 (含硬件引脚、USART、AT 配置等)
 * @return 0=成功, 负数=错误码
 * @note  仅在线程上下文中调用, 初始化阶段阻塞数秒
 */
int hal_lora_init(void);

/**
 * @brief 获取 LoRa 模块当前运行状态
 * @param state  输出: 当前状态
 * @return 0=成功, 负数=错误码
 */
int hal_lora_get_state(hal_lora_state_t *state);

/**
 * @brief 获取 AUX 引脚电平状态
 * @param level  输出: 0=低电平, 1=高电平
 * @return 0=成功, 负数=错误码
 * @note  可在 ISR 中调用
 */
int hal_lora_get_aux(uint8_t *level);

/**
 * @brief 等待 AUX 引脚变为高电平
 * @param timeout_ms  超时时间 (毫秒)
 * @return 0=成功, HAL_ERR_TIMEOUT=超时
 * @note  阻塞等待, 不能在 ISR 中调用
 */
int hal_lora_wait_aux_high(uint32_t timeout_ms);

/*============================================================================
 *  配置模式切换
 *============================================================================*/

/**
 * @brief 进入 AT 配置模式 (MD0=1)
 * @return 0=成功, 负数=错误码
 * @note  阻塞操作, 不能在 ISR 中调用
 */
int hal_lora_enter_config(void);

/**
 * @brief 退出 AT 配置模式 (MD0=0), 等待模块就绪
 * @return 0=成功, 负数=错误码
 * @note  阻塞操作 (等待 AUX 高电平, 最长约 5s), 不能在 ISR 中调用
 */
int hal_lora_exit_config(void);

/*============================================================================
 *  参数配置 (仅在配置模式下有效)
 *============================================================================*/

int hal_lora_set_addr(uint16_t addr);
int hal_lora_set_netid(uint8_t netid);
int hal_lora_set_chn(uint8_t chn);
int hal_lora_set_packsize(uint8_t packsize);
int hal_lora_set_wlrate(uint8_t wlrate);
int hal_lora_set_tmode(uint8_t tmode);
int hal_lora_set_power(uint8_t power);
int hal_lora_set_bps(uint8_t bps);

/*============================================================================
 *  数据收发
 *============================================================================*/

/**
 * @brief 发送数据到默认目标地址和信道
 * @param data  数据指针
 * @param len   数据长度
 * @return 0=成功, 负数=错误码
 * @note  阻塞发送, 不能在 ISR 中调用
 */
int hal_lora_send(const uint8_t *data, uint16_t len);

/**
 * @brief 发送数据到指定目标地址和信道 (透明传输模式)
 * @param data      数据指针
 * @param len       数据长度
 * @param dest_addr 目标设备地址
 * @param chn       信道号
 * @return 0=成功, 负数=错误码
 * @note  阻塞发送, 不能在 ISR 中调用
 */
int hal_lora_send_to(const uint8_t *data, uint16_t len,
                     uint16_t dest_addr, uint8_t chn);

/**
 * @brief 发送字符串 (以 '\0' 结尾)
 * @param str  字符串指针
 * @return 0=成功, 负数=错误码
 */
int hal_lora_send_string(const char *str);

/**
 * @brief 查询 LoRa 接收缓冲区中可用数据量
 * @return 可用字节数
 * @note  实际查询的是底层 UART 环形缓冲区的可读字节数
 */
int hal_lora_data_available(void);

/**
 * @brief 读取 LoRa 接收缓冲区数据
 * @param buf      目标缓冲区
 * @param max_len  最大读取字节数
 * @return 实际读取字节数 (>=0), 负数=错误码
 */
int hal_lora_read(uint8_t *buf, uint16_t max_len);

/**
 * @brief 读取 LoRa 数据但不消费
 * @param buf      目标缓冲区
 * @param max_len  最大查看字节数
 * @return 实际查看字节数 (>=0), 负数=错误码
 */
int hal_lora_peek(uint8_t *buf, uint16_t max_len);

/**
 * @brief 清空 LoRa 接收缓冲区
 * @return 0=成功, 负数=错误码
 */
int hal_lora_clear_buffer(void);

/*============================================================================
 *  帧边界管理 (环形缓冲 + 软超时)
 *============================================================================*/

int hal_lora_frame_available(void);
int hal_lora_mark_frame(void);
int hal_lora_read_frame(uint8_t *buf, uint16_t max_len);
int hal_lora_get_rx_count(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_LORA_H */
