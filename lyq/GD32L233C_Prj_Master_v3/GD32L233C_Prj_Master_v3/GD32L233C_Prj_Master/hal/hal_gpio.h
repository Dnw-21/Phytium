#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO 引脚逻辑 ID (平台无关, 由平台实现映射到物理引脚)
 */
typedef uint8_t hal_pin_id_t;

/**
 * @brief GPIO 引脚方向
 */
typedef enum {
    HAL_GPIO_DIR_INPUT  = 0,
    HAL_GPIO_DIR_OUTPUT = 1
} hal_gpio_dir_t;

/**
 * @brief GPIO 上下拉配置
 */
typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP,
    HAL_GPIO_PULL_DOWN
} hal_gpio_pull_t;

/**
 * @brief LED 逻辑 ID
 */
typedef uint8_t hal_led_id_t;

/**
 * @brief 中断触发边沿
 */
typedef enum {
    HAL_GPIO_IRQ_NONE     = 0,
    HAL_GPIO_IRQ_RISING,
    HAL_GPIO_IRQ_FALLING,
    HAL_GPIO_IRQ_BOTH
} hal_gpio_irq_edge_t;

/**
 * @brief GPIO 中断回调类型
 */
typedef void (*hal_gpio_irq_callback_t)(hal_pin_id_t pin_id);

/*============================================================================
 *  GPIO 通用控制
 *============================================================================*/

/**
 * @brief 初始化 GPIO 为输出模式
 * @param pin_id  引脚逻辑 ID
 * @param pull    上下拉配置
 * @return 0=成功, 负数=错误码
 */
int hal_gpio_output_init(hal_pin_id_t pin_id, hal_gpio_pull_t pull);

/**
 * @brief 初始化 GPIO 为输入模式
 * @param pin_id  引脚逻辑 ID
 * @param pull    上下拉配置
 * @return 0=成功, 负数=错误码
 */
int hal_gpio_input_init(hal_pin_id_t pin_id, hal_gpio_pull_t pull);

/**
 * @brief 设置 GPIO 输出电平
 * @param pin_id  引脚逻辑 ID
 * @param level   0=低电平, 非0=高电平
 * @return 0=成功, 负数=错误码
 * @note  可在 ISR 中调用
 */
int hal_gpio_output_set(hal_pin_id_t pin_id, uint8_t level);

/**
 * @brief 获取 GPIO 输出电平
 * @param pin_id  引脚逻辑 ID
 * @param level   输出: 当前输出电平
 * @return 0=成功, 负数=错误码
 */
int hal_gpio_output_get(hal_pin_id_t pin_id, uint8_t *level);

/**
 * @brief 读取 GPIO 输入电平
 * @param pin_id  引脚逻辑 ID
 * @param level   输出: 当前输入电平 (0/1)
 * @return 0=成功, 负数=错误码
 * @note  可在 ISR 中调用
 */
int hal_gpio_input_read(hal_pin_id_t pin_id, uint8_t *level);

/**
 * @brief 翻转 GPIO 输出电平
 * @param pin_id  引脚逻辑 ID
 * @return 0=成功, 负数=错误码
 * @note  仅对已初始化的输出引脚有效
 */
int hal_gpio_toggle(hal_pin_id_t pin_id);

/**
 * @brief 配置 GPIO 中断
 * @param pin_id   引脚逻辑 ID
 * @param edge     触发边沿
 * @param callback 中断回调 (NULL 取消)
 * @param priority 中断优先级 (0=最高, 平台相关)
 * @return 0=成功, 负数=错误码
 * @note  回调在 ISR 上下文中调用, 不得调用阻塞 API
 */
int hal_gpio_irq_config(hal_pin_id_t pin_id, hal_gpio_irq_edge_t edge,
                        hal_gpio_irq_callback_t callback, uint8_t priority);

/*============================================================================
 *  LED 控制
 *============================================================================*/

/**
 * @brief 初始化指定 LED
 * @param led_id  LED 逻辑 ID
 * @return 0=成功, 负数=错误码
 */
int hal_led_init(hal_led_id_t led_id);

/**
 * @brief 打开 LED
 * @param led_id  LED 逻辑 ID
 * @return 0=成功, 负数=错误码
 */
int hal_led_on(hal_led_id_t led_id);

/**
 * @brief 关闭 LED
 * @param led_id  LED 逻辑 ID
 * @return 0=成功, 负数=错误码
 */
int hal_led_off(hal_led_id_t led_id);

/**
 * @brief 翻转 LED
 * @param led_id  LED 逻辑 ID
 * @return 0=成功, 负数=错误码
 */
int hal_led_toggle(hal_led_id_t led_id);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
