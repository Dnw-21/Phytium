#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 存储区信息结构体 (运行时查询, 避免硬编码)
 */
typedef struct {
    uint32_t base_addr;       /**< 存储区基地址 (字节地址) */
    uint32_t total_size;      /**< 存储区总大小 (字节) */
    uint32_t page_size;       /**< 最小擦除单元大小 (字节) */
    uint32_t write_align;     /**< 写操作对齐要求 (字节, 1=无对齐要求) */
    uint8_t  storage_type;    /**< 存储介质类型: 0=内部Flash, 1=外挂Flash, 2=EEPROM, 3=文件系统 */
    uint8_t  reserved[3];
} hal_flash_info_t;

/**
 * @brief 存储介质类型枚举
 */
#define HAL_FLASH_TYPE_INTERNAL   0
#define HAL_FLASH_TYPE_EXTERNAL   1
#define HAL_FLASH_TYPE_EEPROM     2
#define HAL_FLASH_TYPE_FILESYSTEM 3

/**
 * @brief 预定义存储分区 ID (平台实现可扩展)
 */
typedef uint8_t hal_flash_partition_t;

#define HAL_FLASH_PARTITION_STATUS  0   /**< 状态数据分区 */
#define HAL_FLASH_PARTITION_WAVE    1   /**< 波形数据分区 */
#define HAL_FLASH_PARTITION_USER    2   /**< 用户自定义分区 */

/*============================================================================
 *  初始化和信息查询
 *============================================================================*/

/**
 * @brief 初始化 Flash 子系统
 * @return 0=成功, 负数=错误码
 * @note  线程安全, 在 main() 初始化阶段调用
 */
int hal_flash_init(void);

/**
 * @brief 获取指定分区的存储信息
 * @param partition  分区 ID
 * @param info       输出: 分区存储信息
 * @return 0=成功, HAL_ERR_INVAL=无效分区, 其他负数为错误码
 * @note  应用层通过此接口获知基地址/大小/页大小, 无需硬编码
 */
int hal_flash_get_info(hal_flash_partition_t partition, hal_flash_info_t *info);

/*============================================================================
 *  擦除操作
 *============================================================================*/

/**
 * @brief 擦除指定地址范围的 Flash 区域
 * @param addr  起始地址 (字节地址, 会自动对齐到页边界)
 * @param size  擦除大小 (字节)
 * @return 0=成功, 负数=错误码
 * @note  底层按页擦除, 传入地址自动向下对齐, size 自动向上对齐
 * @note  阻塞操作 (擦除时间取决于区域大小), 不能在 ISR 中调用
 */
int hal_flash_erase(uint32_t addr, uint32_t size);

/*============================================================================
 *  读写操作
 *============================================================================*/

/**
 * @brief 写入数据到 Flash
 * @param addr  目标地址 (字节地址)
 * @param data  数据指针
 * @param len   数据长度 (字节)
 * @return 0=成功, 负数=错误码
 * @note  目标区域需先擦除; 底层处理字对齐
 * @note  阻塞操作, 不能在 ISR 中调用
 */
int hal_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief 从 Flash 读取数据
 * @param addr  源地址 (字节地址)
 * @param buf   目标缓冲区
 * @param len   读取长度 (字节)
 * @return 0=成功, 负数=错误码
 * @note  可在 ISR 中调用 (仅 memcpy 操作)
 */
int hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
