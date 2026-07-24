/*
 * sim_node_task.h — FreeRTOS 模拟数据生成任务
 * ==============================================
 * 在 FreeRTOS 上运行电力系统动态仿真 (RK4),
 * 生成测量数据并通过 RPMsg 批量发送给 Linux。
 */

#ifndef SIM_NODE_TASK_H
#define SIM_NODE_TASK_H

#include "ftypes.h"

/* ─── 任务配置 ─── */
#define SIM_TASK_PRIO      8      /* 优先级 (数字大=优先级低) */
#define SIM_TASK_STACK      16384 /* 栈大小 (16KB) */
#define SIM_BATCH_SIZE      8      /* 每 RPMsg 包包含的测量帧数 */
/* 单帧: 10B header + 8*56B data = 458B (在 RPMSG_MAX_PAYLOAD=489 内) */

/* ─── 命令码 ─── */
#define CMD_SIM_DATA        0x50U  /* FreeRTOS → Linux: 批量测量数据 */
#define CMD_SIM_CTRL        0x51U  /* Linux → FreeRTOS: 控制命令 */
#define CMD_SIM_ACK         0x52U  /* FreeRTOS → Linux: 控制响应 */
#define CMD_SIM_DONE        0x53U  /* FreeRTOS → Linux: 仿真完成通知 */

/* 控制命令子码 */
#define SIM_CTRL_STOP       0
#define SIM_CTRL_START      1
#define SIM_CTRL_RESET      2
#define SIM_CTRL_SPEED      3      /* data: 速度倍率, 0=最快, 1=实时 */

/* ─── 批量数据包 (FreeRTOS → Linux) ─── */
typedef struct __attribute__((packed)) {
    uint8_t  node_id;           /* 节点ID */
    uint8_t  gen_count;         /* 发电机数 */
    uint8_t  bus_count;         /* 母线数 */
    uint8_t  frame_count;       /* 本批帧数 (1~SIM_BATCH_SIZE) */
    uint16_t start_seq;         /* 起始序列号 */
    uint32_t ts_ms_start;       /* 本批第一帧仿真时间 (ms) */
    float    Z_batch[SIM_BATCH_SIZE][14]; /* 测量数据: N帧 × 14维 */
} SimDataBatch;

/* ─── 控制包 (Linux → FreeRTOS) ─── */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;               /* SIM_CTRL_STOP/START/RESET/SPEED */
    uint8_t  node_id;           /* 目标节点ID */
    uint16_t data;              /* 附加数据 (如速度倍率) */
} SimCtrlPkt;

/* ─── 确认包 (FreeRTOS → Linux) ─── */
typedef struct __attribute__((packed)) {
    uint8_t  ack_cmd;           /* 确认的命令 */
    uint8_t  status;            /* 0=OK, 非0=错误 */
    uint16_t reserved;
} SimAckPkt;

/* ─── 任务入口 ─── */
void sim_node_task(void *pv);

#endif /* SIM_NODE_TASK_H */
