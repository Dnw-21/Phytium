/*
 * ============================================================================
 * main.c — FreeRTOS 侧入口
 * ============================================================================
 *
 * 所属示例: Phytium FreeRTOS SDK / openamp_for_linux
 *
 * 功能:
 *   - 初始化 OpenAMP / RPMsg 与 Linux 通信
 *   - 创建仿真节点任务 (5bus / 9bus / 39bus)，按配置频率生成数据
 *   - 通过共享内存 (SHM) 向 Linux 侧 UKF 进程输出测量数据
 *
 * 与本项目相关的任务与文件:
 *   - src/sim_node_task.c  : 仿真节点任务调度
 *   - src/sim_node_5bus.c  : 5bus 数据生成
 *   - src/sim_node_9bus.c  : 9bus 数据生成
 *   - src/sim_node_39bus.c : 39bus 数据生成
 *   - src/sim_math.c       : 仿真所需数学运算
 *   - inc/sim_params_*.h   : 各节点仿真参数（含降频配置）
 *
 * 说明:
 *   本文件为 Phytium FreeRTOS SDK 的 openamp_for_linux 示例入口，
 *   保留原始示例结构。本项目仅修改 inc/sim_params_*.h 中的数据
 *   生成频率参数，其余 FreeRTOS 侧代码保持 SDK 原样。
 * ============================================================================
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
#include "fgeneric_timer.h"     /* GenericTimerRead — 忙等延时 */
#include <string.h>
#include <stdio.h>
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "shm_print.h"
#include "rpmsg_proto.h"
#include "master.h"
#include "tasks.h"
#include "lora_uart.h"
#include "sim_node_task.h"
#include "sim_node_39bus.h"
#include "sim_node_9bus.h"

#define IP_BASE     0x32B30000UL
#define U2_BASE     0x2800E000UL
#define GPIO2_BASE  0x28035000UL
#define GPIO3_BASE  0x28036000UL

#define U2_DR       (*(volatile u32 *)(U2_BASE + 0x00))
#define U2_FR       (*(volatile u32 *)(U2_BASE + 0x18))
#define U2_IBRD     (*(volatile u32 *)(U2_BASE + 0x24))
#define U2_FBRD     (*(volatile u32 *)(U2_BASE + 0x28))
#define U2_LCR_H    (*(volatile u32 *)(U2_BASE + 0x2C))
#define U2_CR       (*(volatile u32 *)(U2_BASE + 0x30))

#define GDR(b)  (*(volatile u32 *)((b) + 0x00))
#define GDD(b)  (*(volatile u32 *)((b) + 0x04))
#define GEX(b)  (*(volatile u32 *)((b) + 0x08))

#define IP_C49    0x00E0U     /* GPIO3_1 → MD0 */
#define IP_A37    0x00C4U     /* GPIO2_10 → AUX */
#define IP_A47    0x00D8U     /* UART2 TX */
#define IP_A49    0x00DCU     /* UART2 RX */

#define AUX_PIN   10
#define MD0_PIN   1

#define AUX_PRIO   2
#define RPM_PRIO   1
#define AUX_STK    2048
#define RPM_STK    8192

static void ipset(u32 off, u32 fn)
{
    volatile u32 *r = (volatile u32 *)(IP_BASE + off);
    *r = (*r & ~0x7U) | (fn & 0x7U);
}

static int rpmsg_send_lora_raw(const u8 *frame, u32 frame_len);

static volatile u32 g_real_frame_cnt = 0;

QueueHandle_t g_recv_queue = NULL;


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
struct remoteproc           g_rproc;       /* remoteproc 实例 (任务二也使用) */
struct rpmsg_device         *g_rpdev = NULL; /* RPMsg 设备 (任务一+二共享) */
static struct rpmsg_endpoint *g_ept = NULL; /* 全局 endpoint (rpm_task 设置) */
static volatile u32 g_rpmsg_tx_cnt = 0;     /* RPMsg 发送计数 */
static volatile u32 g_rpmsg_tx_err = 0;     /* RPMsg 发送失败计数 */

static int rpmsg_endpoint_cb(struct rpmsg_endpoint *ept, void *data,
                              size_t len, u32 src, void *priv)
{
    (void)priv;
    (void)src;
    ept->dest_addr = src;

    if (len < RPMSG_PKT_HDR_SIZE) return RPMSG_SUCCESS;

    RpmsgPkt pkt;
    memset(&pkt, 0, sizeof(pkt));
    if (len > sizeof(pkt)) len = sizeof(pkt);
    memcpy(&pkt, data, len);

    switch (pkt.command) {
    case CMD_ECHO_REQ: {
        RpmsgPkt resp;
        memset(&resp, 0, sizeof(resp));
        resp.command = CMD_ECHO_RESP;
        resp.length  = pkt.length;
        if (pkt.length > RPMSG_MAX_PAYLOAD) pkt.length = RPMSG_MAX_PAYLOAD;
        memcpy(resp.data, pkt.data, pkt.length);
        int ret = rpmsg_send(ept, &resp, RPMSG_PKT_HDR_SIZE + resp.length);
        if (ret < 0) g_rpmsg_tx_err++;
        else g_rpmsg_tx_cnt++;
        break;
    }
    default:
        shm_spf("RPMsg rx: cmd=0x%04X len=%u\r\n", pkt.command, pkt.length);
        break;
    }
    return RPMSG_SUCCESS;
}

static void rpmsg_ept_unbind_cb(struct rpmsg_endpoint *ept)
{
    (void)ept;
    shm_puts("RPMsg: remote unbind\r\n");
}

static int rpmsg_send_lora_raw(const u8 *frame, u32 frame_len)
{
    if (!g_ept) return -1;

    u32 offset = 0;
    while (offset < frame_len) {
        u32 chunk = frame_len - offset;
        if (chunk > RPMSG_MAX_PAYLOAD) chunk = RPMSG_MAX_PAYLOAD;

        RpmsgPkt pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.command = CMD_LORA_RAW;
        pkt.length  = (u16)chunk;
        memcpy(pkt.data, frame + offset, chunk);

        int ret = rpmsg_send(g_ept, &pkt, RPMSG_PKT_HDR_SIZE + chunk);
        if (ret < 0) {
            g_rpmsg_tx_err++;
            return ret;
        }
        g_rpmsg_tx_cnt++;
        offset += chunk;
    }
    return (int)frame_len;
}


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

    shm_puts("A1\r\n");

    for (;;) {
        u32 v = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;

        if (v != last) {
            shm_spf("A %u->%u\r\n", last, v);
            last = v;
        }

        tick++;
        shm_hb_inc();
        if ((tick % 300) == 0) shm_spf("[hb] t=%u A=%u\r\n", tick, v);

        vTaskDelay(pdMS_TO_TICKS(100));
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

    shm_puts("RPM: start poll\r\n");
    for (int i = 0; i < 200; i++) {
        platform_poll_nonblocking(&g_rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    shm_puts("RPM: poll done\r\n");

    shm_spf("RPM: g_rpdev=%p\r\n", (void *)g_rpdev);
    int ret = rpmsg_create_ept(&le, g_rpdev,
                               RPMSG_SERVICE_NAME, 0,
                               RPMSG_ADDR_ANY,
                               rpmsg_endpoint_cb,
                               rpmsg_ept_unbind_cb);
    shm_spf("RPM: ept ret=%d\r\n", ret);
    if (ret) {
        shm_spf("RPM: ept FAIL (%d)\r\n", ret);
    } else {
        g_ept = &le;
        shm_puts("RPM: ept OK\r\n");
    }

    while (1) {
        shm_hb_inc();
        int poll_ret = platform_poll_nonblocking(&g_rproc);
        {
            static u32 rpm_hb = 0;
            rpm_hb++;
            if ((rpm_hb % 5000) == 0 && g_ept && g_ept->dest_addr != RPMSG_ADDR_ANY) {
                RpmsgPkt hb;
                memset(&hb, 0, sizeof(hb));
                hb.command = CMD_HEARTBEAT;
                hb.length  = 4;
                hb.data[0] = (rpm_hb >> 24) & 0xFF;
                hb.data[1] = (rpm_hb >> 16) & 0xFF;
                hb.data[2] = (rpm_hb >>  8) & 0xFF;
                hb.data[3] =  rpm_hb        & 0xFF;
                int r = rpmsg_send(g_ept, &hb, RPMSG_PKT_HDR_SIZE + hb.length);
                if (r < 0) {
                    g_rpmsg_tx_err++;
                    shm_spf("[RPM] HB send FAIL (%d)\r\n", r);
                } else {
                    g_rpmsg_tx_cnt++;
                }
            }
            if ((rpm_hb % 2500) == 0) {
                shm_spf("[RPM-hb] ept=%p dest=0x%lX tx=%u err=%u poll=%d\r\n",
                    (void *)g_ept,
                    g_ept ? (unsigned long)g_ept->dest_addr : 0,
                    g_rpmsg_tx_cnt, g_rpmsg_tx_err, poll_ret);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
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
    FMmuMap(0xC8000000UL, 0xC8000000UL, 0x200000UL,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);   /* 2MB — SHM数据用高位1MB */
    FMmuMap(U2_BASE, U2_BASE, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(IP_BASE, IP_BASE, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(FGPIO2_BASE_ADDR, FGPIO2_BASE_ADDR, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);
    FMmuMap(FGPIO3_BASE_ADDR, FGPIO3_BASE_ADDR, 0x1000U,
            MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);

    /* ═══ 第3步: 清空共享内存 ═══ */
    shm_clear();

    shm_print_init();

    shm_puts("\r\n=== FreeRTOS LoRa v7-S2 (rpmsg-pipe) ===\r\n");
    shm_puts("=== Step4: UART2 GICv3 interrupt + ring buffer ===\r\n");

    /* ═══ 第5步: RPMsg + 外设 + 任务 ═══ */
    if (!platform_create_proc(&g_rproc, &dp, &kd))
        shm_puts("FATAL: proc\r\n");
    else {
        g_rproc.rsc_table = &resources;
        platform_setup_src_table(&g_rproc, g_rproc.rsc_table);
        platform_setup_share_mems(&g_rproc);
        g_rpdev = platform_create_rpmsg_vdev(&g_rproc, 0,
                                           VIRTIO_DEV_DEVICE, NULL, NULL);
        shm_puts("RPMsg done\r\n");
    }
    shm_puts("D2 v3\r\n");
    shm_puts("D3\r\n");

    /* ═══ C8: IOMUX 引脚复用 ═══ */
    ipset(IP_C49, 6);   /* C49  → FUNC6=GPIO  (GPIO3_1 / MD0) */
    ipset(IP_A37, 6);   /* A37  → FUNC6=GPIO  (GPIO2_10 / AUX) */
    ipset(IP_A47, 0);   /* A47  → FUNC0=UART  (UART2 TX) */
    ipset(IP_A49, 0);   /* A49  → FUNC0=UART  (UART2 RX) */
    shm_puts("D4\r\n");

    /* ═══ C9: GPIO 方向配置 ═══ */
    GDD(FGPIO2_BASE_ADDR) &= ~(1U << AUX_PIN);   /* AUX: 输入 */
    GDD(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);   /* MD0: 输出 */
    GDR(FGPIO3_BASE_ADDR)  |=  (1U << MD0_PIN);   /* MD0: 初始 HIGH */
    shm_puts("D5\r\n");

    /* ═══ C10: UART2 初始化 115200 8N1 ═══ */
    U2_CR    = 0;         /* 先禁用UART */
    U2_IBRD  = 54;        /* 整数波特率: 100MHz/(16×115200) ≈ 54 */
    U2_FBRD  = 16;        /* 小数部分 */
    U2_LCR_H = 0x70;      /* 8bit + FIFO 使能 */
    U2_CR    = 0x0301;    /* UARTEN | TXE | RXE */
    shm_puts("D6\r\n");

    /* ═══ Step4: UART2 中断接收 (lora_uart驱动统一管理) ═══ */
    lora_uart_init();
    shm_puts("D6-IRQ\r\n");

    /* ═══ D11: LoRa 模块 AT 配置 (调度器启动前完成, 匹配 GD32 LoRa_Init) ═══ */
    {
        /* 忙等延时: 调度器未启动, 不能使用 vTaskDelay */
        #define BUSY_DELAY_MS(ms) do { \
            u64 _freq = GenericTimerFrequecy(); \
            u64 _start = GenericTimerRead(GENERIC_TIMER_ID0); \
            u64 _target = _start + (_freq * (ms) / 1000); \
            while (GenericTimerRead(GENERIC_TIMER_ID0) < _target); \
        } while(0)

        shm_puts("AT init start (pre-scheduler)\r\n");

        /* MD0=HIGH → AT命令模式 */
        GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN);
        BUSY_DELAY_MS(200);
        GDR(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);
        BUSY_DELAY_MS(500);

        /* AT 命令: 轮询 UART2 FIFO, 与 lora_task 中 AT_SEND 逻辑一致 */
        #define AT_CMD(s, resp) do { \
            const char *_p = (s); \
            while (*_p) { while (U2_FR & (1U << 5)); U2_DR = (u32)*_p++; } \
            while (U2_FR & (1U << 5)); U2_DR = '\r'; \
            while (U2_FR & (1U << 5)); U2_DR = '\n'; \
            BUSY_DELAY_MS(300); \
            shm_puts(s); shm_puts(": "); \
            for (int _i = 0; _i < 200; _i++) { \
                if (!(U2_FR & (1U << 4))) { \
                    u8 _b = (u8)(U2_DR & 0xFF); \
                    static const char _hx[] = "0123456789ABCDEF"; \
                    char _t[4] = { _hx[(_b >> 4) & 0xF], _hx[_b & 0xF], ' ', 0 }; \
                    shm_puts(_t); \
                } else { BUSY_DELAY_MS(1); } \
            } \
            shm_puts("\r\n"); \
        } while(0)

        for (int at_retry = 0; at_retry < 3; at_retry++) {
            AT_CMD("AT", at_retry);
            if (at_retry < 2) BUSY_DELAY_MS(300);
        }
        AT_CMD("AT+ADDR?", );
        AT_CMD("AT+ADDR=00,0A", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+NETID=0", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+WLRATE=23,7", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+PACKSIZE=3", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+TMODE=1", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+TPOWER=4", );
        BUSY_DELAY_MS(100);
        AT_CMD("AT+UART=7,0", );

        shm_puts("AT init done\r\n");

        /* MD0=LOW → 透传模式 */
        GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN);
        for (int _i = 0; _i < 50; _i++) {
            BUSY_DELAY_MS(100);
            if ((GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U) break;
        }
        shm_spf("MD0=LOW AUX=%u (ready)\r\n",
            (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U);

        /* 清空 UART RX FIFO 残留 */
        while (!(U2_FR & (1U << 4))) { volatile u32 _d = U2_DR; (void)_d; }

        lora_uart_interrupt_enable(1);
        shm_puts("RX IRQ enabled\r\n");

        #undef AT_CMD
        #undef BUSY_DELAY_MS
    }

    /* ═══ D12: 创建任务 ═══ */
    chaos_init(0x12345678);
    master_init();
    g_recv_queue = xQueueCreate(RECV_QUEUE_LENGTH, sizeof(RecvPacket_t));
    xTaskCreate(rpm_task,  "RPM",  RPM_STK,  NULL, RPM_PRIO,  NULL);
    xTaskCreate(aux_task,  "AUX",  AUX_STK,  NULL, AUX_PRIO,  NULL);
    xTaskCreate(master_recv_task, "Recv", MASTER_RECV_STK_SIZE, NULL, MASTER_RECV_TASK_PRIO, NULL);
    xTaskCreate(master_process_task, "Proc", MASTER_PROCESS_STK_SIZE, NULL, MASTER_PROCESS_TASK_PRIO, NULL);
    xTaskCreate(master_judge_task, "Judge", MASTER_JUDGE_STK_SIZE, NULL, MASTER_JUDGE_TASK_PRIO, NULL);
    xTaskCreate(master_poll_task, "Poll", MASTER_POLL_STK_SIZE, NULL, MASTER_POLL_TASK_PRIO, NULL);
    xTaskCreate(sim_node_task, "SIM", SIM_TASK_STACK, NULL, SIM_TASK_PRIO, NULL);
    xTaskCreate(sim_node_39bus_task, "SIM39", SIM39_TASK_STACK, NULL, SIM39_TASK_PRIO, NULL);
    xTaskCreate(sim_node_9bus_task, "SIM9", SIM9_TASK_STACK, NULL, SIM9_TASK_PRIO, NULL);
    shm_puts("D7\r\n");

    /* ═══ D12: 启动调度器 ═══ */
    shm_puts("Start sched\r\n");
    vTaskStartScheduler();

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
