#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART 端口逻辑 ID (平台无关)
 * @note  具体映射由各平台实现文件定义, 应用层仅使用逻辑 ID
 */
typedef uint8_t hal_uart_id_t;

/**
 * @brief UART 接收完成回调类型
 * @param uart_id  触发回调的 UART 端口
 * @param byte     接收到的字节
 * @note  在 ISR 上下文中调用, 不得调用 FreeRTOS API, 不得阻塞
 */
typedef void (*hal_uart_rx_callback_t)(hal_uart_id_t uart_id, uint8_t byte);

/*============================================================================
 *  初始化和配置
 *============================================================================*/

/**
 * @brief 初始化指定 UART 端口
 * @param uart_id   UART 逻辑 ID
 * @param baudrate  波特率 (如 115200)
 * @return 0=成功, 负数=错误码
 * @note  线程安全, 通常在 main() 初始化阶段调用
 */
int hal_uart_init(hal_uart_id_t uart_id, uint32_t baudrate);

/**
 * @brief 注册 UART 接收中断回调
 * @param uart_id   UART 逻辑 ID
 * @param callback  回调函数指针, 传入 NULL 取消注册
 * @return 0=成功, 负数=错误码
 * @note  线程安全, 回调在 ISR 中执行
 */
int hal_uart_register_rx_callback(hal_uart_id_t uart_id, hal_uart_rx_callback_t callback);

/*============================================================================
 *  数据发送
 *============================================================================*/

/**
 * @brief 阻塞发送单个字节
 * @param uart_id   UART 逻辑 ID
 * @param byte      待发送的字节
 * @return 0=成功, 负数=错误码
 * @note  可在 ISR 中调用 (用于 printf 重定向等场景)
 */
int hal_uart_send_byte(hal_uart_id_t uart_id, uint8_t byte);

/**
 * @brief 阻塞发送数据块
 * @param uart_id   UART 逻辑 ID
 * @param data      数据指针
 * @param len       数据长度
 * @return 实际发送的字节数, 负数=错误码
 * @note  线程安全; 不能在 ISR 中调用 (阻塞时间取决于数据量)
 */
int hal_uart_send(hal_uart_id_t uart_id, const uint8_t *data, uint16_t len);

/*============================================================================
 *  数据接收 (查询模式)
 *============================================================================*/

/**
 * @brief 查询接收缓冲区中可用字节数
 * @param uart_id   UART 逻辑 ID
 * @return 可用字节数 (>=0), 负数=错误码
 * @note  可在 ISR 中调用
 */
int hal_uart_data_available(hal_uart_id_t uart_id);

/**
 * @brief 从接收缓冲区读取数据 (消费)
 * @param uart_id   UART 逻辑 ID
 * @param buf       目标缓冲区
 * @param max_len   最大读取字节数
 * @return 实际读取的字节数 (>=0), 负数=错误码
 * @note  线程安全; 数据读取后从缓冲区移除
 */
int hal_uart_read(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len);

/**
 * @brief 查看接收缓冲区数据 (不消费)
 * @param uart_id   UART 逻辑 ID
 * @param buf       目标缓冲区
 * @param max_len   最大查看字节数
 * @return 实际查看的字节数 (>=0), 负数=错误码
 * @note  数据仍在缓冲区中, 不会被移除
 */
int hal_uart_peek(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len);

/**
 * @brief 清空接收缓冲区
 * @param uart_id   UART 逻辑 ID
 * @return 0=成功, 负数=错误码
 */
int hal_uart_clear_buffer(hal_uart_id_t uart_id);

/*============================================================================
 *  帧边界管理 (环形缓冲 + 软超时场景)
 *============================================================================*/

/**
 * @brief 查询是否有完整帧可读
 * @param uart_id   UART 逻辑 ID
 * @return >0=有帧可读, 0=无帧, 负数=错误码
 */
int hal_uart_frame_available(hal_uart_id_t uart_id);

/**
 * @brief 标记当前接收缓冲区位置为一帧数据的结束
 * @param uart_id   UART 逻辑 ID
 * @return 0=成功, 负数=错误码
 * @note  应用层通过软超时判断帧边界后调用此函数标记
 */
int hal_uart_mark_frame(hal_uart_id_t uart_id);

/**
 * @brief 从接收缓冲区读取一帧完整数据
 * @param uart_id   UART 逻辑 ID
 * @param buf       目标缓冲区
 * @param max_len   最大读取字节数
 * @return 实际读取的帧字节数 (>=0), 负数=错误码
 * @note  读取后帧从队列中移除
 */
int hal_uart_read_frame(hal_uart_id_t uart_id, uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
