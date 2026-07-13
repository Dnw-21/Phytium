#ifndef HAL_PLATFORM_GD32L23X_H
#define HAL_PLATFORM_GD32L23X_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *  UART 逻辑 ID 映射
 *============================================================================*/
#define HAL_UART_ID_DEBUG       0   /* USART0: PA9(TX)/PA10(RX), 调试串口 */
#define HAL_UART_ID_LORA        1   /* USART1: PA2(TX)/PA3(RX), LoRa 模块通信 */
#define HAL_UART_MAX            2

/*============================================================================
 *  GPIO 引脚逻辑 ID 映射
 *============================================================================*/
/* 通用控制引脚 */
#define HAL_PIN_MD0             0   /* PB8: LoRa MD0 配置模式控制 */
#define HAL_PIN_AUX             1   /* PB9: LoRa AUX 状态监测 */

/* 预留扩展引脚 (从 10 开始以避免冲突) */
#define HAL_PIN_USER_BASE       10

/*============================================================================
 *  LED 逻辑 ID 映射
 *============================================================================*/
#define HAL_LED_ID_0            0   /* LED1: PA7 */
#define HAL_LED_ID_1            1   /* LED2: PA8 */
#define HAL_LED_ID_MAX          2

/*============================================================================
 *  Flash 分区预定义 (平台相关基地址)
 *============================================================================*/
#define HAL_FLASH_PARTITION_STATUS  0   /* 状态数据区: 0x08030000, 64KB */
#define HAL_FLASH_PARTITION_WAVE    1   /* 波形数据区: 0x08040000, 64KB */

/*============================================================================
 *  Flash 物理参数 (通过 hal_flash_get_info 运行时查询)
 *============================================================================*/
#define HAL_FLASH_GD32L23X_PAGE_SIZE      4096
#define HAL_FLASH_GD32L23X_STATUS_BASE    0x08030000
#define HAL_FLASH_GD32L23X_STATUS_SIZE    (64 * 1024)
#define HAL_FLASH_GD32L23X_WAVE_BASE      0x08040000
#define HAL_FLASH_GD32L23X_WAVE_SIZE      (64 * 1024)

/*============================================================================
 *  LoRa 模块默认配置 (可通过 hal_lora_init 带参数初始化)
 *============================================================================*/
#define HAL_LORA_GD32L23X_DEFAULT_ADDR     0x000B
#define HAL_LORA_GD32L23X_DEFAULT_CHN      23
#define HAL_LORA_GD32L23X_DEFAULT_NETID    0

#ifdef __cplusplus
}
#endif

#endif /* HAL_PLATFORM_GD32L23X_H */
