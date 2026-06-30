/*
 * shm_data.h — 共享内存数据环形缓冲区
 * ======================================
 * FreeRTOS 端直接写测量数据到共享内存, Linux 端通过 /dev/mem 读取。
 * 绕过 RPMsg 接收路径, 100% 可靠, 支持无限节点。
 *
 * 布局 (每节点):
 *   [0] write_index (u32)
 *   [4] read_index  (u32)  — Linux 读取后更新
 *   [8] frame_count (u32)  — 总写入帧数
 *   [12] frame_size (u32)  — 每帧字节数
 *   [16] ring_data[]       — 环形缓冲区
 *
 * 地址分配:
 *   0xC8100000: 5bus/2-gen  (256KB, frame=64B)
 *   0xC8140000: 39bus/10-gen (512KB, frame=400B)
 *   0xC81C0000: 9bus/3-gen  (128KB, frame=104B)
 */

#ifndef SHM_DATA_H
#define SHM_DATA_H

#include "ftypes.h"

#define SHM_5BUS_BASE   0xC8100000UL
#define SHM_5BUS_SIZE   0x40000UL     /* 256KB, 4095 frames @64B */
#define SHM_39BUS_BASE  0xC8140000UL
#define SHM_39BUS_SIZE  0x80000UL     /* 512KB, 1310 frames @400B (2000Hz) */
#define SHM_9BUS_BASE   0xC81C0000UL  /* 移动: 给39bus腾出空间 */
#define SHM_9BUS_SIZE   0x20000UL     /* 128KB, 1260 frames @104B */

/* 环形缓冲区访问宏 */
#define SHM_WI(base)    (*(volatile u32 *)(base))
#define SHM_RI(base)    (*(volatile u32 *)((base) + 4))
#define SHM_CNT(base)   (*(volatile u32 *)((base) + 8))
#define SHM_FSZ(base)   (*(volatile u32 *)((base) + 12))
#define SHM_DATA(base)  ((volatile u8 *)((base) + 16))
#define SHM_DSZ(base)   (SHM_FSZ(base) - 16)  /* data region size */

/* 写入一帧到帧级环形缓冲区 (FreeRTOS 端调用)
 * v3: 帧级环形缓冲区 - 每帧占用固定槽位, 无包裹损坏 */
static inline void shm_put_frame(u32 base, u32 buf_size, const u8 *frame, u32 frame_size)
{
    u32 fsz  = SHM_FSZ(base);
    if (fsz != frame_size) return;

    u32 data_size = buf_size - 16;
    u32 cap = data_size / fsz;
    if (cap == 0) return;

    u32 cnt = SHM_CNT(base);
    u32 slot = cnt % cap;  /* 帧在环形缓冲区中的槽位号 */

    /* 拷贝数据到槽位 (无包裹) */
    volatile u8 *dst = SHM_DATA(base) + (u32)(slot * fsz);
    for (u32 i = 0; i < frame_size; i++) {
        dst[i] = frame[i];
    }

    SHM_WI(base) = (slot + 1) % cap;  /* 下一个槽位 */
    SHM_CNT(base) = cnt + 1;
}

/* 初始化共享内存区域 */
static inline void shm_init_region(u32 base, u32 frame_size)
{
    SHM_WI(base)  = 0;
    SHM_RI(base)  = 0;
    SHM_CNT(base) = 0;
    SHM_FSZ(base) = frame_size;
}

#endif
