/*
 * ============================================================================
 * FreeRTOS LoRa 接收 — Phytium PE2204 (飞腾派)
 * ============================================================================
 *
 * 文件:  main.c                      所属项目:  openamp_for_linux
 * 路径:  phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/
 * 备份:  /home/alientek/Phytium/freertos/main.c
 * 指南:  /home/alientek/Phytium/freertos/DEBUG_GUIDE.md
 *
 * ─── 数据流 ───
 *   GD32节点LoRa ──无线──> ATK-MWCC68D模块 ──UART2(0x2800E000)──> FreeRTOS轮询
 *                                   │
 *                             MD0(GPIO3_1) + AUX(GPIO2_10) —— 配置/状态
 *
 * ─── 打印 ───
 *   FreeRTOS ──(裸写volatile)──> 共享内存 0xC8000000
 *   Linux 　　──(/dev/mem mmap)──> trace_reader 终端查看
 *
 * ─── 编译 ───
 *   cd phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
 *   export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
 *   make config_pe2204_phytiumpi_aarch64 && make clean && make all -j$(nproc)
 *
 * ─── 部署 ───
 *   scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.11:/tmp/
 *   ssh user@192.168.88.11
 *   sudo cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf /lib/firmware/openamp_core0.elf
 *   sudo reboot
 *   # 等上线后: sudo /home/user/trace_reader
 *
 * ─── 已验证 (2025-06-26) ───
 *   ✅ FreeRTOS 启动并存活
 *   ✅ RPMsg 通道建立 (rpmsg host is online)
 *   ✅ GPIO: MD0 输出控制(AT/透传), AUX 输入检测
 *   ✅ UART2: AT命令发送 + 模块回复 OK 接收
 *   ✅ Step4: UART2 中断接收 (替代轮询, 微秒级响应)
 *
 * ─── 待完成 ───
 *   □ Step5: 业务逻辑解析 (LoRa帧格式解析)
 *   □ 优化: 多任务间共享内存写入互斥
 *   □ 优化: 用 SDK fgpio.h/FPl011.h API 替代直接寄存器访问
 */
#include "ftypes.h"            /* u32, u8, boolean 等基本类型 */
#include "fmmu.h"              /* FMmuMap() — MMU 页表映射 */
#include "fparameters.h"       /* 基址常量: FUART2_BASE_ADDR, FGPIO2_BASE_ADDR 等 */
#include "FreeRTOS.h"          /* FreeRTOS 内核 */
#include "task.h"              /* vTaskDelay, xTaskCreate 等 */
#include "platform_info.h"     /* OpenAMP 平台信息结构体 */
#include "rsc_table.h"         /* remoteproc resource table 定义 */
#include "memory_layout.h"     /* DEVICE00_SHARE_MEM_ADDR 等内存布局 */
#include "libmetal_configs.h"  /* libmetal 配置常量 */
#include "openamp_configs.h"   /* OpenAMP 编译期开关 */
#include "helper.h"            /* OpenAMP 辅助宏 */
#include <openamp/open_amp.h>  /* rpmsg_device, rpmsg_endpoint 等 */
#include <metal/alloc.h>       /* metal 内存分配器 */
#include "rpmsg_service.h"     /* RPMSG_SERVICE_NAME 通道名常量 */
#include "finterrupt.h"         /* InterruptInstall / InterruptUmask 等 */
#include "fcpu_info.h"          /* GetCpuId() — 获取当前核心编号 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ==========================================================================
 *  第1部分: 共享内存打印 — 自包含, 零SDK依赖
 *
 *  设计决策: 不使用 f_printk/ftrace_printk。
 *  原因: f_printk 在多任务环境下不可靠 (write_index 始终为0)。
 *  方案: 直接 volatile 宏裸写, 绕开所有SDK依赖链。
 * ========================================================================== */

/*
 * SHM_BASE: 设备树 reserved-memory 预留的物理内存起始地址
 * 范围: 0xb0100000 ~ 0xc99fffff (409MB, rproc@b0100000)
 * SHM_BUF: 跳过 3 个 u32 头部字段 (wi, ri) 后的实际缓冲区
 * put/puts: 宏实现, 零函数调用开销, volatile 确保每次真正内存访问
 */
#define SHM_BASE  ((volatile u32 *)0xC8000000UL)
#define SHM_WI    (SHM_BASE[0])                      /* write_index: 写指针 */
#define SHM_RI    (SHM_BASE[1])                      /* read_index: 读指针 */
#define SHM_HB    (SHM_BASE[2])                      /* heartbeat: 3个任务的活心跳 (read via devmem 0xC8000008) */
#define SHM_BUF   ((volatile char *)(SHM_BASE + 3))  /* 环形缓冲区起始 */
#define SHM_BSZ   (1024 * 1024 - 12)                 /* 缓冲区大小(1MB-12B) */

/*
 * put: 往环形缓冲区写一个字符
 * 宏展开避免函数调用, volatile 防止编译器将 SHM_WI 缓存在寄存器
 */
#define put(c) do { \
    u32 w = SHM_WI, r = SHM_RI, n = (w + 1) % SHM_BSZ; \
    if (n != r) { SHM_BUF[w] = (c); SHM_WI = n; } \
} while (0)

/*
 * puts: 往环形缓冲区写一个字符串
 */
#define puts(s) do { const char *p = (s); while (*p) put(*p++); } while (0)

/*
 * spf: 格式化输出到共享内存
 * 先 vsnprintf 到 256B 栈缓冲区, 再 puts
 * 注意: 非线程安全, 多任务并发写入时可能字符交错
 */
static void spf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    puts(buf);
}


/* ==========================================================================
 *  第2部分: 引脚定义 — LoRa 模块硬件引脚映射
 *
 *  飞腾派 J1 排针 → ATK-MWCC68D LoRa 模块:
 *    J1 Pin 8  (TXD)     ↔  LoRa RXD (UART2)
 *    J1 Pin 10 (RXD)     ↔  LoRa TXD (UART2)
 *    J1 Pin 11 (GPIO3_1) ↔  LoRa MD0  (模式选择)
 *    J1 Pin 12 (GPIO2_10)↔  LoRa AUX  (状态指示)
 * ========================================================================== */
#define MD0_PIN  1      /* GPIO3_1 — 模式: HIGH=AT命令模式, LOW=透传模式 */
#define AUX_PIN  10     /* GPIO2_10 — 状态: 0=模块忙, 1=模块空闲 */


/* ==========================================================================
 *  第3部分: GPIO 寄存器 — 与 SDK fgpio_hw.h 寄存器偏移一致
 *
 *  GPIO 控制器基址 (来自 fparameters_comm.h):
 *    FGPIO2_BASE_ADDR = 0x28035000  (AUX 所在控制器)
 *    FGPIO3_BASE_ADDR = 0x28036000  (MD0 所在控制器)
 *  寄存器偏移 (每个 GPIO 控制器):
 *    DR  = 0x00 — 输出数据
 *    DDR = 0x04 — 方向 (1=输出, 0=输入)
 *    EXT = 0x08 — 输入引脚电平 (只读)
 * ========================================================================== */
#define GDR(b)  (*(volatile u32 *)((u32)(b) + 0x00))  /* 输出数据寄存器 */
#define GDD(b)  (*(volatile u32 *)((u32)(b) + 0x04))  /* 方向寄存器 */
#define GEX(b)  (*(volatile u32 *)((u32)(b) + 0x08))  /* 输入数据寄存器 */


/* ==========================================================================
 *  第4部分: IOPAD — 引脚复用控制
 *
 *  IOPAD 基址: 0x32B30000 (来自 fparameters_comm.h: FIOPAD_BASE_ADDR)
 *
 *  每个引脚对应一个 4B 控制寄存器, bit[2:0] 为复用功能号 (FUNC):
 *    IP_C49 (0xE0)  → GPIO3_1  (MD0)   FUNC6=GPIO
 *    IP_A37 (0xC4)  → GPIO2_10 (AUX)   FUNC6=GPIO
 *    IP_A47 (0xD8)  → UART2 TX         FUNC0=UART
 *    IP_A49 (0xDC)  → UART2 RX         FUNC0=UART
 *
 *  ipset(): 读-改-写 IOPAD 控制寄存器, 设置 FUNC 字段
 *           与 SDK FIOPadSetFunc() 逻辑一致但跳过实例化
 * ========================================================================== */
#define IP_BASE  0x32B30000U
#define IP_C49   0x00E0U     /* GPIO3_1 → MD0 */
#define IP_A37   0x00C4U     /* GPIO2_10 → AUX */
#define IP_A47   0x00D8U     /* UART2 TX */
#define IP_A49   0x00DCU     /* UART2 RX */

static void ipset(u32 off, u32 fn)
{
    volatile u32 *r = (volatile u32 *)(IP_BASE + off);
    *r = (*r & ~0x7U) | (fn & 0x7U);  /* 清除低3位, 写入功能号 */
}


/* ==========================================================================
 *  第5部分: UART2 PL011 — ARM PrimeCell 串口
 *
 *  基址: FUART2_BASE_ADDR = 0x2800E000 (来自 fparameters_comm.h)
 *  时钟: 100MHz (APB 总线时钟)
 *  波特率计算: IBRD=54, FBRD=16 → 115200bps @ 100MHz
 *
 *  寄存器 (PL011 标准):
 *    DR    0x00 — 收发数据
 *    FR    0x18 — 标志 (TXFF=bit5, RXFE=bit4)
 *    IBRD  0x24 — 整数波特率除数
 *    FBRD  0x28 — 小数波特率除数
 *    LCR_H 0x2C — 线路控制 (0x70 = 8bit + FIFO使能)
 *    CR    0x30 — 控制 (0x0301=UARTEN|TXE|RXE)
 * ========================================================================== */
#define U2_BASE  FUART2_BASE_ADDR
#define U2_DR    (*(volatile u32 *)(U2_BASE + 0x00))   /* 数据寄存器 */
#define U2_ECR   (*(volatile u32 *)(U2_BASE + 0x04))   /* 错误清除寄存器 */
#define U2_FR    (*(volatile u32 *)(U2_BASE + 0x18))   /* 标志寄存器 */
#define U2_IBRD  (*(volatile u32 *)(U2_BASE + 0x24))   /* 整数波特率 */
#define U2_FBRD  (*(volatile u32 *)(U2_BASE + 0x28))   /* 小数波特率 */
#define U2_LCR_H (*(volatile u32 *)(U2_BASE + 0x2C))   /* 线路控制 */
#define U2_CR    (*(volatile u32 *)(U2_BASE + 0x30))   /* 控制寄存器 */


/* ==========================================================================
 *  第6部分: UART2 接收环形缓冲区
 *
 *  rb[4096]: 环形缓冲区本体 (字节数组)
 *  rh, rt:   头/尾指针 (volatile, ISR 安全, 为 Step4 中断做准备)
 *  rp_put:   写入一个字节 (满则丢弃)
 *  rp_get:   读出一个字节 (空返回 -1)
 *  rp_avail: 缓冲区中可读字节数
 * ========================================================================== */
#define RB_SZ      4096
static u8           rb[RB_SZ];
static volatile u32 rh = 0, rt = 0;

static void rp_put(u8 b)
{
    u32 n = (rh + 1) % RB_SZ;
    if (n != rt) { rb[rh] = b; rh = n; }
}

static int rp_get(u8 *b)
{
    if (rt == rh) return -1;
    *b = rb[rt];
    rt = (rt + 1) % RB_SZ;
    return 0;
}

static u32 rp_avail(void)
{
    return (rh - rt + RB_SZ) % RB_SZ;
}

static void rp_hex_dump(const char *tag)
{
    u8 buf[128];
    u32 pos = 0;
    u32 n = rp_avail();
    if (n > 64) n = 64;
    for (u32 i = 0; i < n; i++)
        if (rp_get(&buf[pos]) == 0) pos++;
    if (pos == 0) return;
    puts(tag);
    for (u32 j = 0; j < pos; j++) {
        static const char hex[] = "0123456789ABCDEF";
        char h[4] = { hex[(buf[j] >> 4) & 0xF], hex[buf[j] & 0xF], ' ', 0 };
        puts(h);
    }
    puts("\r\n");
}


/* ==========================================================================
 *  第7部分: UART2 中断服务函数 (Step4 — 替代轮询)
 *
 *  原理:
 *    硬件中断触发 → ISR 从 UART2 FIFO 读数据 → 放入环形缓冲区。
 *    错误中断自动清除, 防止 PL011 溢出锁死。
 *
 *  与轮询对比:
 *    轮询: vTaskDelay(10ms) 导致数据堆积 → FIFO/模块溢出 → 死锁
 *    中断: 硬件触发, 微秒级响应 → 零数据丢失 → 高吞吐
 *
 *  rp_put / rp_get 线程安全分析 (单核):
 *    rh 只由 ISR 写 (生产者), rt 只由 task 写 (消费者)
 *    均为 32-bit aligned word write, 单核无竞争
 * ========================================================================== */
static void uart2_lora_isr(s32 vector, void *param)
{
    (void)vector;
    (void)param;

    u32 mis = *(volatile u32 *)(U2_BASE + 0x40); /* FPL011MIS_OFFSET */

    /* RX 数据中断: RXMIS = 0x10 */
    if (mis & 0x10) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) { /* !RXFE */
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);
        }
    }

    /* RX 超时中断: RTMIS = 0x40 (FIFO有数据但不满, 字符间隔超时触发) */
    if (mis & 0x40) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) {
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);
        }
    }

    /* 错误中断: OE=0x400, BE=0x200, PE=0x100, FE=0x80 — 清除防止锁死 */
    if (mis & 0x780) {
        *(volatile u32 *)(U2_BASE + 0x04) = 0xFF; /* ECR 清除 */
    }

    /* 写1清除中断标志 */
    *(volatile u32 *)(U2_BASE + 0x44) = mis; /* FPL011ICR_OFFSET */
}


/* ==========================================================================
 *  第8部分: FreeRTOS 任务参数
 *
 *  RPM — RPMsg 通道轮询任务 (优先级最高, 保证通信)
 *  AUX — GPIO AUX 监控任务
 *  LoRa — UART2 中断接收 + AT 配置任务
 * ========================================================================== */
#define LORA_STK  8192    /* LoRa 任务栈大小 (增大: 含spf 256B栈+hexdump开销) */
#define LORA_PRIO 3       /* LoRa 优先级 */
#define RPM_STK   8192    /* RPMsg 栈大小 */
#define RPM_PRIO  1       /* RPMsg 优先级 (最高) */
#define AUX_STK   2048    /* AUX 监控栈大小 */
#define AUX_PRIO  2       /* AUX 优先级 */


/* ==========================================================================
 *  第9部分: OpenAMP Resource Table + RemoteProc
 *
 *  resource_table:
 *    - 必须放在 0xC0000000 地址 (链接脚本里 KEEP)
 *    - 描述 vrings 和共享内存布局
 *    - Linux remoteproc 驱动启动时读取此表
 *
 *  remoteproc_priv:
 *    - 共享内存基址、大小、属性
 *    - kick 设备信息 (通知 Linux 有数据)
 *    - 当前 core 的 CPU mask
 * ========================================================================== */
struct remote_resource_table resources
    __attribute__((section(".resource_table"), used)) = {
    /* version | entry_count | reserved[2] */
    1, NUM_TABLE_ENTRIES, {0, 0},

    /* rpmsg_vdev: offset_to_vdev */
    { offsetof(struct remote_resource_table, rpmsg_vdev) },

    /* rpmsg_vdev: type | id | notifyid | dfeatures | gfeatures | config_len | status | num_of_vrings | reserved[2] */
    { RSC_VDEV, VIRTIO_ID_RPMSG_, VDEV_NOTIFYID, RPMSG_IPU_C0_FEATURES,
      0, 0, 0, NUM_VRINGS, {0, 0} },

    /* vring0 (TX): da | align | num | notifyid | reserved */
    { DEVICE00_TX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 1, 0 },

    /* vring1 (RX): da | align | num | notifyid | reserved */
    { DEVICE00_RX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 2, 0 },
};

/* kick device 结构体 — 用于通知 Linux 侧有 RPMsg 数据 */
static metal_phys_addr_t pa = DEVICE00_KICK_IO_ADDR;
struct metal_device kd = {
    .name       = DEVICE_00_KICK_DEV_NAME,
    .bus        = NULL,
    .num_regions = 1,
    .regions    = { { .virt      = (void *)DEVICE00_KICK_IO_ADDR,
                      .physmap   = &pa,
                      .size      = 0x1000,
                      .page_shift = -1UL,
                      .page_mask  = -1UL,
                      .mem_flags = DEVICE00_SOURCE_TABLE_ATTRIBUTE,
                      .ops       = {NULL} } },
    .irq_num    = 1,
    .irq_info   = (void *)DEVICE_00_SGI,
};

/* remoteproc 私有数据 — 描述当前 core 的资源配置 */
struct remoteproc_priv dp = {
    .kick_dev_name       = DEVICE_00_KICK_DEV_NAME,
    .kick_dev_bus_name   = KICK_BUS_NAME,
    .cpu_id              = DRIVER_CORE_MASK,
    .src_table_attribute = DEVICE00_SOURCE_TABLE_ATTRIBUTE,
    .share_mem_va        = DEVICE00_SHARE_MEM_ADDR,
    .share_mem_pa        = DEVICE00_SHARE_MEM_ADDR,
    .share_buffer_offset = DEVICE00_VRING_SIZE,
    .share_mem_size      = DEVICE00_SHARE_MEM_SIZE,
    .share_mem_attribute = DEVICE00_SHARE_BUFFER_ATTRIBUTE,
};

/* OpenAMP 运行时状态 */
static struct remoteproc     rproc;       /* remoteproc 实例 */
static struct rpmsg_device  *rpdev = NULL; /* RPMsg 设备 */


/* ==========================================================================
 *  任务: AUX 监控 (aux_task) — 仅业务逻辑
 *
 *  所有硬件初始化 (MMU映射 / IOMUX / GPIO方向) 已在 main() 中集中完成。
 *  本任务只做: 每 100ms 读一次 AUX 电平, 变化时打印, 30s 心跳。
 * ========================================================================== */
static void aux_task(void *pv)
{
    u32 last = 99;  /* 上次电平 (99=初始未知) */
    u32 tick = 0;   /* 心跳计数器 */

    puts("A1\r\n");

    for (;;) {
        u32 v = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;

        if (v != last) {
            spf("A %u->%u\r\n", last, v);
            last = v;
        }

        tick++;
        SHM_HB++;  /* heartbeat: AUX alive */
        if ((tick % 300) == 0) spf("[hb] t=%u A=%u\r\n", tick, v);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


/* ==========================================================================
 *  任务: LoRa 控制 (lora_task) — AT 配置 + 轮询接收 + 透传控制
 *
 *  硬件初始化 (UART2 / IOPAD / GPIO方向) 已在 main() 中集中完成。
 *  本任务只做 LoRa 模块的 AT 配置和运行时轮询接收。
 *
 *  流程:
 *    1. MD0=HIGH → 等 500ms → 发送 AT 命令 (完全匹配 GD32_V3 主控)
 *    2. MD0=LOW  → 等 AUX=HIGH (最多 5s)
 *    3. 轮询接收: 每 10ms 读 UART2 DR → hex dump
 *
 *  AT命令 (完全匹配 GD32_V3 mwcc68_app.c / mwcc68_cfg.h):
 *    AT, AT+ADDR?, AT+CFG?,
 *    AT+ADDR=00,0B, AT+NETID=0, AT+CHN=23,
 *    AT+PACKSIZE=3, AT+WLRATE=5, AT+TMODE=1,
 *    AT+POWER=4, AT+BPS=7
 * ========================================================================== */
static void lora_task(void *pv)
{
    u32 pkt = 0;

    puts("L1 v7-IRQ\r\n");

    /* ─── 第1步: MD0=HIGH → AT命令模式 ─── */
    GDR(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);  /* MD0 输出 HIGH */
    vTaskDelay(pdMS_TO_TICKS(500));              /* 等 LoRa 模块进入 AT 模式 */

    puts("L2\r\n");

    /*
     * AT 命令序列: Linux 侧已验证格式 + GD32_V3 mwcc68_cfg.h 参数
     * 参数: ADDR=0x0B, CHN=23, NETID=0, POWER=20dBm(TPOWER=4),
     *       RATE=19.2kbps(WLRATE=23,5), TMODE=FP(TMODE=1),
     *       PACKSIZE=240(PACKSIZE=3), BPS=115200(UART=7,0)
     */
    puts("LoRa: AT init (verified format)\r\n");

    /* 辅助宏: 发送 AT 命令 + 轮询读回响应 (回退轮询模式) */
    #define AT_SEND(s) do { \
        const char *_p = (s); \
        while (*_p) { while (U2_FR & (1U << 5)); U2_DR = (u32)*_p++; } \
        while (U2_FR & (1U << 5)); U2_DR = '\r'; \
        while (U2_FR & (1U << 5)); U2_DR = '\n'; \
        vTaskDelay(pdMS_TO_TICKS(200)); \
        puts(s); puts(": "); \
        for (int _i = 0; _i < 200; _i++) { \
            if (!(U2_FR & (1U << 4))) { \
                u8 _b = (u8)(U2_DR & 0xFF); \
                static const char _hx[] = "0123456789ABCDEF"; \
                char _t[4] = { _hx[(_b >> 4) & 0xF], _hx[_b & 0xF], ' ', 0 }; \
                puts(_t); \
            } else { vTaskDelay(pdMS_TO_TICKS(1)); } \
        } \
        puts("\r\n"); \
    } while (0)

    /* 第1条: AT (初始连通性检查, 重试最多3次) */
    for (int at_retry = 0; at_retry < 3; at_retry++) {
        AT_SEND("AT");
        if (at_retry < 2) vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* 查询当前配置 */
    AT_SEND("AT+ADDR?");

    /* 参数配置 (Linux 已验证 + GD32_V3 参数) */
    AT_SEND("AT+ADDR=00,0B");     /* 地址 0x000B */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+NETID=0");        /* 网络ID */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+WLRATE=23,5");    /* 信道23 + 速率19.2kbps */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+PACKSIZE=3");     /* 3=240字节 */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+TMODE=1");        /* 1=定点传输 */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+TPOWER=4");       /* 4=20dBm */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+UART=7,0");       /* 7=115200bps, 0=8N1 */
    vTaskDelay(pdMS_TO_TICKS(100));
    AT_SEND("AT+FLASH=1");        /* 保存到Flash */

    puts("LoRa: AT config done\r\n");
    puts("L3\r\n");

    /* ─── 第2步: MD0=LOW → 透传模式 (匹配 LoRa_ExitConfigMode) ─── */
    GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN);
    puts("L4\r\n");

    /* 等 AUX 变 HIGH (模块切换完成, 最多等 5s) */
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if ((GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U) break;
    }
    spf("LoRa: MD0=LOW AUX=%u (ready)\r\n",
        (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U);
    puts("LoRa: RX ready (IRQ)\r\n");
    puts("L5\r\n");

    /* ─── 第3步: 使能 UART2 硬件中断 (IMSC) ───
     *
     *  在 AT 命令全部完成、模块进入透传模式后, 才开启 UART2 中断。
     *  避免 ISR 与 AT_SEND 轮询读取冲突。
     *
     *  IMSC (Interrupt Mask Set/Clear, 偏移 0x38):
     *    0x10 = RXIM  (接收中断掩码)
     *    0x40 = RTIM  (接收超时中断掩码)
     */
    *(volatile u32 *)(U2_BASE + 0x38) = 0x10 | 0x40;  /* RXIM | RTIM */
    puts("L6-IRQ\r\n");

    /* ─── 第4步: 中断接收循环 ───
     *
     *  ISR (uart2_lora_isr) 从 UART2 FIFO 读取 → 写入环形缓冲区 rb[4096]
     *  本任务每 2ms 检查环形缓冲区 → hex dump 到共享内存
     *
     *  线程安全分析 (单核, CPU3):
     *    rh 仅 ISR 写 (生产者), rt 仅 task 写 (消费者)
     *    均为 32-bit aligned, volatile 修饰, 单核无竞争
     *
     *  2ms 轮询间隔: 115200bps → 11.5 bytes/ms → 23 bytes/间隔
     *  环形缓冲区 4096B → 可缓冲 ~178 次轮询 (~356ms) 的数据
     */
    u32 loop_cnt = 0;
    u32 idle_cnt = 0;
    puts("L7-IRQ\r\n");
    for (;;) {
        u32 avail = rp_avail();

        if (avail > 0) {
            if (idle_cnt > 500) {  /* ~1s 空闲后重新有数据 */
                spf("\r\n!! NEW after %ums idle\r\n", idle_cnt * 2);
            }
            idle_cnt = 0;

            u8 buf[64];
            u32 n = 0;
            while (n < 64 && rp_get(&buf[n]) == 0) n++;

            pkt++;
            spf("\r\nRX #%u %uB: ", pkt, n);
            for (u32 j = 0; j < n; j++) {
                static const char hex[] = "0123456789ABCDEF";
                char h[4] = { hex[(buf[j] >> 4) & 0xF],
                              hex[buf[j] & 0xF], ' ', 0 };
                puts(h);
            }
            puts("\r\n");
        } else {
            idle_cnt++;
        }

        loop_cnt++;
        SHM_HB++;  /* heartbeat: LoRa alive */
        if ((loop_cnt % 1500) == 0) {  /* ~3s (1500 × 2ms) */
            u32 aux = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;
            spf("[RX-hb] loop=%u AUX=%u rp_av=%u idle=%ums\r\n",
                loop_cnt, aux, rp_avail(), idle_cnt * 2);
        }

        vTaskDelay(pdMS_TO_TICKS(2));  /* 2ms 轮询间隔 */
    }
}


/* ==========================================================================
 *  任务: RPMsg 通道轮询 (rpm_task)
 *
 *  功能:
 *    1. 等待 vring 激活 (Linux 侧 remoteproc 启动后)
 *    2. 创建 endpoint (通道名: rpmsg-openamp-demo-channel)
 *    3. 进入 poll 循环, 处理 RPMsg 消息
 *
 *  注意: RPMsg 初始化必须在调度器启动后, 因为需要 tasks
 *  后续可在此实现 LoRa 数据 → RPMsg → Linux 的转发
 * ========================================================================== */
static void rpm_task(void *pv)
{
    (void)pv;
    struct rpmsg_endpoint le = {0};

    for (int i = 0; i < 500; i++) {
        platform_poll(&rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    (void)rpmsg_create_ept(&le, rpdev,
                           RPMSG_SERVICE_NAME, 0,
                           RPMSG_ADDR_ANY,
                           NULL, NULL);

    while (1) {
        SHM_HB++;  /* heartbeat: RPM alive */
        platform_poll(&rproc);
    }
}


/* ==========================================================================
 *  入口: main()
 *
 *  ─── 初始化顺序 (不可调换) ───
 *
 *  A. 共享内存
 *    1. FMmuMap 0xC8000000            — 必须最早, 后续 puts 依赖
 *    2. 清零 SHM_WI / SHM_RI
 *
 *  B. SDK & RPMsg
 *    3. init_system()                 — SDK 系统初始化
 *    4. platform_create_proc()        — 创建 remoteproc 实例
 *    5. setup_src_table / share_mems
 *    6. create_rpmsg_vdev             — 创建 RPMsg virtio 设备
 *
 *  C. 硬件 (集中初始化, 任务不再自行映射)
 *    7. FMmuMap: UART2 + IOPAD + GPIO2 + GPIO3
 *    8. IOMUX:    IP_C49/IP_A37 → FUNC6(GPIO), IP_A47/IP_A49 → FUNC0(UART)
 *    9. GPIO:     AUX 输入, MD0 输出(初态=HIGH)
 *   10. UART2:    115200 8N1
 *
 *  D. 任务
 *   11. xTaskCreate ×3
 *   12. vTaskStartScheduler()         — 永不返回
 * ========================================================================== */
int main(void)
{
    /* ═══ 第1步: SDK平台初始化 — 必须第一句 ═══ */
    init_system();

    /* ═══ 第2步: MMU映射所有物理地址 ═══ */
    FMmuMap(0xC8000000UL, 0xC8000000UL, 0x100000UL,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);   /* 非缓存 — 每写必须到DDR */
    FMmuMap(U2_BASE, U2_BASE, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(IP_BASE, IP_BASE, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(FGPIO2_BASE_ADDR, FGPIO2_BASE_ADDR, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(FGPIO3_BASE_ADDR, FGPIO3_BASE_ADDR, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);

    /* ═══ 第3步: 清空共享内存 ═══ */
    SHM_WI = 0;
    SHM_RI = 0;
    SHM_HB = 0;
    {
        volatile char *p = SHM_BUF;
        volatile char *end = p + SHM_BSZ - 1;
        *end = 0;
        for (u32 k = 0; k < 65536; k++) p[k] = 0;
    }

    /* ═══ 第4步: 安全打印 — 此时全部地址已映射 ═══ */
    puts("\r\n=== FreRTOS LoRa v7 IRQ (2026-05-21) ===\r\n");
    puts("=== Step4: UART2 GICv3 interrupt + ring buffer ===\r\n");

    /* ═══ 第5步: RPMsg + 外设 + 任务 ═══ */
    if (!platform_create_proc(&rproc, &dp, &kd))
        puts("FATAL: proc\r\n");
    else {
        rproc.rsc_table = &resources;
        platform_setup_src_table(&rproc, rproc.rsc_table);
        platform_setup_share_mems(&rproc);
        rpdev = platform_create_rpmsg_vdev(&rproc, 0,
                                           VIRTIO_DEV_DEVICE, NULL, NULL);
        puts("RPMsg done\r\n");
    }
    puts("D2 v3\r\n");
    puts("D3\r\n");

    /* ═══ C8: IOMUX 引脚复用 ═══ */
    ipset(IP_C49, 6);   /* C49  → FUNC6=GPIO  (GPIO3_1 / MD0) */
    ipset(IP_A37, 6);   /* A37  → FUNC6=GPIO  (GPIO2_10 / AUX) */
    ipset(IP_A47, 0);   /* A47  → FUNC0=UART  (UART2 TX) */
    ipset(IP_A49, 0);   /* A49  → FUNC0=UART  (UART2 RX) */
    puts("D4\r\n");

    /* ═══ C9: GPIO 方向配置 ═══ */
    GDD(FGPIO2_BASE_ADDR) &= ~(1U << AUX_PIN);   /* AUX: 输入 */
    GDD(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);   /* MD0: 输出 */
    GDR(FGPIO3_BASE_ADDR)  |=  (1U << MD0_PIN);   /* MD0: 初始 HIGH */
    puts("D5\r\n");

    /* ═══ C10: UART2 初始化 115200 8N1 ═══ */
    U2_CR    = 0;         /* 先禁用UART */
    U2_IBRD  = 54;        /* 整数波特率: 100MHz/(16×115200) ≈ 54 */
    U2_FBRD  = 16;        /* 小数部分 */
    U2_LCR_H = 0x70;      /* 8bit + FIFO 使能 */
    U2_CR    = 0x0301;    /* UARTEN | TXE | RXE */
    puts("D6\r\n");

    /* ═══ Step4: UART2 中断接收 (GICv3) ═══
     *
     *  GIC 配置在此完成, 但 UART2 硬件中断使能 (IMSC) 延迟到 lora_task。
     *  原因: AT 命令阶段需要轮询读取响应, 过早使能中断会导致 ISR
     *        抢先读取 AT 响应数据, 引发 AT 命令解析失败。
     */
    InterruptInstall(FUART2_IRQ_NUM, uart2_lora_isr, NULL, "UART2-LoRa");
    InterruptSetPriority(FUART2_IRQ_NUM, IRQ_PRIORITY_VALUE_8);
    {
        u32 cpu_id;
        GetCpuId(&cpu_id);
        spf("UART2 IRQ%u → CPU%u\n", FUART2_IRQ_NUM, cpu_id);
        InterruptSetTargetCpus(FUART2_IRQ_NUM, cpu_id);
    }
    InterruptUmask(FUART2_IRQ_NUM);
    /* UART2 IMSC 将在 lora_task 透传模式下使能, 避免 AT 阶段冲突 */
    puts("D6-IRQ\r\n");

    /* ═══ D11: 创建任务 ═══ */
    xTaskCreate(rpm_task,  "RPM",  RPM_STK,  NULL, RPM_PRIO,  NULL);
    xTaskCreate(aux_task,  "AUX",  AUX_STK,  NULL, AUX_PRIO,  NULL);
    xTaskCreate(lora_task, "LoRa", LORA_STK, NULL, LORA_PRIO, NULL);
    puts("D7\r\n");

    /* ═══ D12: 启动调度器 ═══ */
    puts("Start sched\r\n");
    vTaskStartScheduler();

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
