/**
 * @file    data_save.c
 * @brief   接收数据落盘 — 通过 shm_print 输出文本格式数据到共享内存
 *
 * 输出格式: "[SAVE] type=0x%02X sync=0x%08X len=%d: %02X %02X ..."
 * trace_reader 或 data_dump 可直接读取/保存
 */

#include "data_save.h"
#include "shm_print.h"

void data_save_init(void)
{
    /* 无需初始化，shm_print 已在 main.c 中初始化 */
}

void data_save_frame(uint8_t rx_type, uint32_t sync_code,
                     const uint8_t *data, uint16_t len)
{
    if (!data || len == 0)
        return;

    shm_spf("[SAVE] type=0x%02X sync=0x%08X len=%d:",
            rx_type, sync_code, len);

    for (uint16_t i = 0; i < len; i++)
        shm_spf(" %02X", data[i]);

    shm_puts("\r\n");
}
