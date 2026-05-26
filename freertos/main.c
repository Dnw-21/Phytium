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
#include "fgeneric_timer.h"     /* GenericTimerRead — 忙等延时 */
#include <string.h>
#include <stdio.h>
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "shm_print.h"
#include "rpmsg_proto.h"
#include "master.h"
#include "tasks.h"
#include "wave_decode.h"

static int rpmsg_send_lora_raw(const u8 *frame, u32 frame_len);

static volatile int g_lora_rx_enabled = 1;
static volatile u32 g_real_frame_cnt = 0;
void master_lora_rx_ctrl(int enable) { g_lora_rx_enabled = enable; }
int  master_lora_rx_is_enabled(void) { return g_lora_rx_enabled; }

static u32 process_lora_frame(const u8 *buf, u32 buf_len, u32 *total_p);

/* --- 临时统计: FLASH_WAVE 波形包连续性 --- */
static u32 fw_pkt_cnt = 0;
static u32 fw_wave_id = 0;
static u32 fw_seq = 0;
static u32 fw_lost = 0;
static u32 fw_first_ts = 0;
static u32 fw_last_ts = 0;
static u32 fw_points = 0;
static u32 fw_total_bytes = 0;
static u32 fw_min_val = 0xFFFF;
static u32 fw_max_val = 0;
static u8  fw_in_wave = 0;

static u32 process_lora_frame(const u8 *buf, u32 buf_len, u32 *total_p)
{
    if (buf_len < 13) return 0;

    uint8_t  rx_type;
    uint32_t sync_code;
    uint16_t enc_len;
    uint8_t *enc_start;
    uint8_t *payload;
    uint16_t payload_len;

    FrameParseResult_t frame_result;
    frame_parse(buf, buf_len, &frame_result);
    rx_type   = frame_result.rx_type;
    sync_code = frame_result.sync_code;
    enc_len   = frame_result.enc_len;
    enc_start = frame_result.enc_start;

    (*total_p)++;
    g_real_frame_cnt++;
    u8  dec_buf[128];
    u16 dec_len = 0;
    /* 新版终端不再混沌加密, 直接使用 enc_start 作为 payload */
    if (enc_len <= sizeof(dec_buf)) { memcpy(dec_buf, enc_start, enc_len); dec_len = enc_len; }
    shm_spf("\r\n=== FRAME #%u: type=%02X sync=%08X enc=%uB ===\r\n",
        *total_p, rx_type, sync_code, dec_len);

    /* hex dump: payload 原始数据 */
    {
        u32 dump_len = dec_len;
        if (dump_len > 256) dump_len = 256;
        for (u32 i = 0; i < dump_len; i++) {
            static const char hx[] = "0123456789ABCDEF";
            char h[4] = { hx[(dec_buf[i] >> 4) & 0xF], hx[dec_buf[i] & 0xF], ' ', 0 };
            shm_puts(h);
            if ((i + 1) % 16 == 0) shm_puts("\r\n");
        }
        if (dump_len % 16 != 0) shm_puts("\r\n");
    }

    /* 帧长度合理性校验: 过滤 CRC 碰巧匹配的假帧 */
    {
        int valid_len = 0;
        switch (rx_type) {
        case DATA_TYPE_NODE_HEAD:  valid_len = (dec_len >= (int)sizeof(NodeUploadHeader_t)); break;
        case DATA_TYPE_WAVE:       valid_len = (dec_len >= (int)sizeof(WaveChunkHeader_t));  break;
        case DATA_TYPE_NODE_RAW:   valid_len = (dec_len >= (int)sizeof(NodeSample_t));       break;
        case DATA_TYPE_FLASH_WAVE: valid_len = (dec_len >= 2);                               break;
        case DATA_TYPE_POWER:      valid_len = 1;                                            break;
        default:                   valid_len = 0;                                            break;
        }
        if (!valid_len) {
            shm_spf("  SKIP: type=%02X enc=%uB (too small, buflen=%u)\r\n", rx_type, dec_len, buf_len);
            return frame_result.consumed;
        }
    }

    MasterDownloadBuf_t *dl = master_get_download_buf();
    uint8_t node_id;
    if (dl->active && (rx_type == DATA_TYPE_NODE_RAW || rx_type == DATA_TYPE_FLASH_WAVE)) {
        node_id = dl->node_id;
    } else {
        node_id = 0;
        if (rx_type == DATA_TYPE_NODE_HEAD && dec_len >= sizeof(NodeUploadHeader_t)) {
            NodeUploadHeader_t hdr;
            memcpy(&hdr, dec_buf, sizeof(hdr));
            node_id = hdr.node_index;
        } else if (rx_type == DATA_TYPE_WAVE && dec_len >= sizeof(WaveChunkHeader_t)) {
            WaveChunkHeader_t hdr;
            memcpy(&hdr, dec_buf, sizeof(hdr));
            node_id = hdr.node_index;
        }
        if (node_id >= MASTER_MAX_NODES) node_id = 0;
    }
    uint16_t src_addr = SLAVE_ADDR_BASE + node_id;
    MasterNodeInfo_t *node = master_get_node_info(node_id);
    if (node) {
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    switch (rx_type) {
    case DATA_TYPE_NODE_HEAD: /* 0x01: 节点状态头 (轮询/故障统一) */
        if (dl) dl->active = 0;
        if (node) process_node_header(dec_buf, dec_len, dl, node);
        break;
    case DATA_TYPE_WAVE: /* 0x02: 波形数据头 */
        if (dl) dl->active = 0;
        if (node) process_wave_header(dec_buf, dec_len, dl, node, src_addr);
        fw_wave_id++;
        fw_seq = 0; fw_lost = 0;
        fw_first_ts = 0; fw_last_ts = 0;
        fw_points = 0; fw_total_bytes = 0;
        fw_min_val = 0xFFFF; fw_max_val = 0;
        fw_in_wave = 1;
        shm_spf("[FW_BEG] wave#%u\r\n", fw_wave_id);
        break;
    case DATA_TYPE_POWER: /* 0x03: 预留 */
        shm_spf("  POWER: %uB (reserved)\r\n", dec_len);
        break;
    case DATA_TYPE_NODE_RAW: /* 0x04: 节点原始数据 */
        if (node) process_node_raw(dec_buf, dec_len, dl, node);
        {
            u16 n_s = dec_len / sizeof(NodeSample_t);
            for (u16 i = 0; i < n_s && i < 8; i++) {
                NodeSample_t s;
                memcpy(&s, &dec_buf[i * sizeof(NodeSample_t)], sizeof(s));
                shm_spf("  [RAW] s%u: p=%d q=%d ts=%u\r\n",
                    i, s.active_power, s.reactive_power, s.timestamp);
            }
        }
        break;
    case DATA_TYPE_FLASH_WAVE: /* 0x05: 波形原始数据 (差分编码) */
        if (node) process_flash_wave(dec_buf, dec_len, dl, node);
        {
            fw_pkt_cnt++;
            if (!fw_in_wave) {
                fw_wave_id++;
                fw_seq = 0; fw_lost = 0;
                fw_first_ts = 0; fw_points = 0; fw_total_bytes = 0;
                fw_min_val = 0xFFFF; fw_max_val = 0;
                fw_in_wave = 1;
                shm_spf("[FW_BEG] wave#%u\r\n", fw_wave_id);
            }
            shm_spf("[FW_DAT p=%u len=%u]\r\n", fw_seq, dec_len);
            {
                static const char hx[] = "0123456789ABCDEF";
                for (u32 i = 0; i < dec_len; i++) {
                    char h[3] = { hx[(dec_buf[i] >> 4) & 0xF], hx[dec_buf[i] & 0xF], 0 };
                    shm_puts(h);
                }
            }
            shm_puts("\r\n");
            fw_total_bytes += dec_len;
            if (fw_seq == 0) {
                fw_first_ts = 0;
            }
            fw_last_ts = 0;
            u16 pts_in_pkt = dec_len / 2;
            fw_points += pts_in_pkt;
            for (u16 i = 0; i < pts_in_pkt; i++) {
                s16 v = (s16)((dec_buf[i * 2 + 1] << 8) | dec_buf[i * 2]);
                u16 av = (v < 0) ? (u16)(-v) : (u16)v;
                if (av < fw_min_val) fw_min_val = av;
                if (av > fw_max_val) fw_max_val = av;
            }
            shm_spf("  [WAVE-STAT] wave#%u pkt#%u pts=%u total=%u val=[%u..%u]\r\n",
                fw_wave_id, fw_seq, pts_in_pkt, fw_points, fw_min_val, fw_max_val);
            fw_seq++;
            if (dl && !dl->active) {
                shm_spf("[FW_END] wave#%u pkts=%u bytes=%u pts=%u lost=%u val=[%u..%u]\r\n",
                    fw_wave_id, fw_seq, fw_total_bytes, fw_points, fw_lost,
                    fw_min_val, fw_max_val);
                fw_in_wave = 0;
            }
        }
        break;
    default:
        shm_spf("  UNKNOWN(%02X): %uB\r\n", rx_type, dec_len);
        break;
    }

    /* 延迟 Flash/共享内存 操作 */
    if (dl->flash_erase_pending) {
        master_flash_erase_wave(dl->node_id);
        dl->flash_erase_pending = 0;
    }
    if (dl->flash_wave_pending) {
        master_flash_save_wave_data(dl->node_id, dl->wave_chunk,
                                    dl->wave_chunk_len, dl->wave_byte_offset);
        dl->flash_wave_pending = 0;
    }
    if (dl->flash_wave_done) {
        master_recv_wave_data(dl->node_id, dl->received_points);
        dl->flash_wave_done = 0;
    }
    if (dl->flash_save_pending) {
        master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
        dl->flash_save_pending = 0;
    }
    return frame_result.consumed;
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

uint32_t lora_rx_avail(void) { return rp_avail(); }
int lora_rx_read_byte(uint8_t *b) { return rp_get(b); }

/* ==========================================================================
 *  ring_recv_frame: 状态机逐字节提取完整 LoRa 帧
 *
 *  帧格式: [AA][55][len_H][len_L][inner_data:len B][CRC8][55][AA]
 *
 *  状态机逐字节推进, 读取指定长度后校验 55AA 帧尾。
 *  不依赖 AA55 搜索, 彻底消除负载内假帧头问题。
 * ========================================================================== */
static int ring_recv_frame(uint8_t *buf, uint16_t max_len)
{
    enum { S_IDLE, S_LEN_H, S_LEN_L, S_DATA, S_CRC, S_TAIL1, S_TAIL2 } s = S_IDLE;
    uint16_t data_len = 0, pos = 0, remain = 0;
    uint8_t b;

    while (rp_avail() > 0 && pos < max_len) {
        if (rp_get(&b) != 0) break;
        buf[pos++] = b;

        switch (s) {
        case S_IDLE:
            if (b == 0xAA) { pos = 1; buf[0] = 0xAA; s = S_LEN_H; }
            break;
        case S_LEN_H:
            data_len = (uint16_t)b << 8;
            s = S_LEN_L;
            break;
        case S_LEN_L:
            data_len |= b;
            if (data_len > 250) { s = S_IDLE; pos = 0; break; }
            remain = data_len + 1;  /* +1 CRC8 */
            s = S_DATA;
            break;
        case S_DATA:
            if (--remain == 0) s = S_CRC;
            break;
        case S_CRC:
            /* CRC byte consumed, expect tail */
            s = S_TAIL1;
            break;
        case S_TAIL1:
            if (b == 0x55) s = S_TAIL2;
            else { s = S_IDLE; pos = 0; }
            break;
        case S_TAIL2:
            if (b == 0xAA) return pos;  /* complete frame */
            s = S_IDLE; pos = 0;
            break;
        }
    }
    return 0;  /* incomplete or buffer full */
}

void lora_tx_send_byte(uint8_t b)
{
    while (*(volatile u32 *)(U2_BASE + 0x18) & (1U << 5));  /* wait !TXFF */
    *(volatile u32 *)(U2_BASE + 0x00) = (u32)b;
    while (*(volatile u32 *)(U2_BASE + 0x18) & (1U << 5));  /* wait !TXFF */
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
    shm_puts(tag);
    for (u32 j = 0; j < pos; j++) {
        static const char hex[] = "0123456789ABCDEF";
        char h[4] = { hex[(buf[j] >> 4) & 0xF], hex[buf[j] & 0xF], ' ', 0 };
        shm_puts(h);
    }
    shm_puts("\r\n");
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
static volatile u32 isr_cnt = 0;
static volatile u32 isr_data = 0;       /* ISR reads from FIFO (bytes) */
static volatile u32 isr_no_data = 0;    /* ISR calls with MIS=0 (no data) */

static void uart2_lora_isr(s32 vector, void *param)
{
    (void)vector;
    (void)param;

    isr_cnt++;

    u32 mis = *(volatile u32 *)(U2_BASE + 0x40); /* FPL011MIS_OFFSET */

    u32 reads = 0;

    /* RX 数据中断: RXMIS = 0x10 */
    if (mis & 0x10) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) { /* !RXFE */
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);
            reads++;
        }
    }

    /* RX 超时中断: RTMIS = 0x40 (FIFO有数据但不满, 字符间隔超时触发) */
    if (mis & 0x40) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) {
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);
            reads++;
        }
    }

    /* 错误中断: OE=0x400, BE=0x200, PE=0x100, FE=0x80 — 清除防止锁死 */
    if (mis & 0x780) {
        *(volatile u32 *)(U2_BASE + 0x04) = 0xFF; /* ECR 清除 */
    }

    if (reads == 0) isr_no_data++;
    isr_data += reads;

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
    /* AT配置已在main()中完成, 本任务仅负责接收 */
    shm_puts("LoRa RX task start\r\n");

    /* 清空 ring buffer */
    rh = 0; rt = 0;
    shm_puts("RX ready\r\n");

    {
        u32 loop_cnt = 0, frame_cnt = 0;
        u8  frame_buf[280];  /* max frame: 3B prefix + 2B hdr + 250B + CRC + 2B tail = 260 */
        int frame_len;

        for (;;) {
            frame_len = ring_recv_frame(frame_buf, sizeof(frame_buf));
            if (frame_len < 13) {
                vTaskDelay(pdMS_TO_TICKS(2));
                loop_cnt++;
                shm_hb_inc();
                if ((loop_cnt % 1500) == 0) {
                    u32 aux = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;
                    shm_spf("[RX-hb] loop=%u AUX=%u tx=%u isr=%u dat=%u\r\n",
                        loop_cnt, aux, frame_cnt, isr_cnt, isr_data);
                }
                continue;
            }

            frame_cnt++;
            g_real_frame_cnt++;

            /* frame_buf layout: [AA][55][LEN_H][LEN_L][inner_data:frame_data_len][CRC][55][AA]
             * frame[8]=rx_type, frame[9-12]=sync_code, &frame[13]=payload_start
             * data_len (from bytes 2-3) = inner_data length = 4(ts)+1(type)+4(sync)+N(payload) */
            uint16_t frame_data_len = ((uint16_t)frame_buf[2] << 8) | frame_buf[3];
            uint8_t  rx_type   = frame_buf[8];
            uint32_t sync_code = ((uint32_t)frame_buf[9] << 24) | ((uint32_t)frame_buf[10] << 16)
                               | ((uint32_t)frame_buf[11] << 8) | frame_buf[12];
            uint16_t enc_len   = frame_data_len - 9;
            uint8_t *enc_start = &frame_buf[13];

            shm_spf("\r\n=== FRAME #%u: type=%02X sync=%08X enc=%uB ===\r\n",
                frame_cnt, rx_type, sync_code, enc_len);

            /* hex dump: 最多64字节 */
            for (u32 i = 0; i < enc_len && i < 64; i++) {
                static const char hx[] = "0123456789ABCDEF";
                char h[4] = { hx[(enc_start[i] >> 4) & 0xF], hx[enc_start[i] & 0xF], ' ', 0 };
                shm_puts(h);
                if ((i + 1) % 16 == 0) shm_puts("\r\n");
            }
            if (enc_len > 64) shm_spf("  ... (%uB total)\r\n", enc_len);
            if (enc_len > 0 && (enc_len % 16) != 0 && enc_len <= 64) shm_puts("\r\n");

            MasterDownloadBuf_t *dl = master_get_download_buf();
            uint8_t node_id = 0;

            if (dl->active && (rx_type == DATA_TYPE_NODE_RAW || rx_type == DATA_TYPE_FLASH_WAVE)) {
                node_id = dl->node_id;
            } else if (rx_type == DATA_TYPE_NODE_HEAD && enc_len >= (int)sizeof(NodeUploadHeader_t)) {
                NodeUploadHeader_t hdr;
                memcpy(&hdr, enc_start, sizeof(hdr));
                node_id = hdr.node_index;
            } else if (rx_type == DATA_TYPE_WAVE && enc_len >= (int)sizeof(WaveChunkHeader_t)) {
                WaveChunkHeader_t hdr;
                memcpy(&hdr, enc_start, sizeof(hdr));
                node_id = hdr.node_index;
            }
            if (node_id >= MASTER_MAX_NODES) node_id = 0;
            uint16_t src_addr = SLAVE_ADDR_BASE + node_id;
            MasterNodeInfo_t *node = master_get_node_info(node_id);

            if (node) {
                node->is_online = 1;
                node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            }

            switch (rx_type) {
            case DATA_TYPE_NODE_HEAD:
                if (dl) dl->active = 0;
                if (node) process_node_header(enc_start, enc_len, dl, node);
                break;
            case DATA_TYPE_WAVE:
                if (dl) dl->active = 0;
                if (node) process_wave_header(enc_start, enc_len, dl, node, src_addr);
                break;
            case DATA_TYPE_NODE_RAW:
                if (node) process_node_raw(enc_start, enc_len, dl, node);
                break;
            case DATA_TYPE_FLASH_WAVE:
                if (node) process_flash_wave(enc_start, enc_len, dl, node);
                /* 差分编码解码 */
                {
                    int16_t wave_samples[256];
                    uint16_t decoded = wave_decode_packet(enc_start, enc_len, wave_samples, 256);
                    if (decoded > 0) {
                        shm_spf("  [WAVE] decoded %u samples: ", decoded);
                        for (int w = 0; w < (int)decoded && w < 8; w++)
                            shm_spf("%d ", wave_samples[w]);
                        if (decoded > 8) shm_puts("...");
                        shm_puts("\r\n");
                    }
                }
                break;
            default:
                shm_spf("  UNKNOWN type=%02X\r\n", rx_type);
                break;
            }

            /* 延迟 Flash/共享内存 操作 */
            if (dl->flash_erase_pending) {
                master_flash_erase_wave(dl->node_id);
                dl->flash_erase_pending = 0;
            }
            if (dl->flash_wave_pending) {
                master_flash_save_wave_data(dl->node_id, dl->wave_chunk,
                                            dl->wave_chunk_len, dl->wave_byte_offset);
                dl->flash_wave_pending = 0;
            }
            if (dl->flash_wave_done) {
                master_recv_wave_data(dl->node_id, dl->received_points);
                dl->flash_wave_done = 0;
            }
            if (dl->flash_save_pending) {
                master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
                shm_spf("  NODE%d: saved %d pts\r\n", dl->node_id, dl->received_points);
                dl->flash_save_pending = 0;
            }
        }
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
        platform_poll_nonblocking(&rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    shm_puts("RPM: poll done\r\n");

    shm_spf("RPM: rpdev=%p\r\n", (void *)rpdev);
    int ret = rpmsg_create_ept(&le, rpdev,
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
        int poll_ret = platform_poll_nonblocking(&rproc);
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
    shm_clear();

    shm_print_init();

    shm_puts("\r\n=== FreeRTOS LoRa v7-S2 (rpmsg-pipe) ===\r\n");
    shm_puts("=== Step4: UART2 GICv3 interrupt + ring buffer ===\r\n");

    /* ═══ 第5步: RPMsg + 外设 + 任务 ═══ */
    if (!platform_create_proc(&rproc, &dp, &kd))
        shm_puts("FATAL: proc\r\n");
    else {
        rproc.rsc_table = &resources;
        platform_setup_src_table(&rproc, rproc.rsc_table);
        platform_setup_share_mems(&rproc);
        rpdev = platform_create_rpmsg_vdev(&rproc, 0,
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
        u64 raw_mpidr = 0;
        __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(raw_mpidr));
        GetCpuId(&cpu_id);
        shm_spf("MPIDR=0x%llX GetCpuId=%u dts=rproc=3\n", raw_mpidr, cpu_id);
        InterruptSetTargetCpus(FUART2_IRQ_NUM, cpu_id);
    }
    InterruptUmask(FUART2_IRQ_NUM);
    /* UART2 IMSC 将在 AT 配置完成后使能 */
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

        /* 使能 UART2 硬件中断 */
        *(volatile u32 *)(U2_BASE + 0x38) = 0x10 | 0x40;  /* RXIM | RTIM */
        shm_puts("RX IRQ enabled\r\n");

        #undef AT_CMD
        #undef BUSY_DELAY_MS
    }

    /* ═══ D12: 创建任务 ═══ */
    master_init();
    xTaskCreate(rpm_task,  "RPM",  RPM_STK,  NULL, RPM_PRIO,  NULL);
    xTaskCreate(aux_task,  "AUX",  AUX_STK,  NULL, AUX_PRIO,  NULL);
    xTaskCreate(lora_task, "LoRa", LORA_STK, NULL, LORA_PRIO, NULL);
    xTaskCreate(master_judge_task, "Judge", MASTER_JUDGE_STK_SIZE, NULL, MASTER_JUDGE_TASK_PRIO, NULL);
    xTaskCreate(master_poll_task, "Poll", MASTER_POLL_STK_SIZE, NULL, MASTER_POLL_TASK_PRIO, NULL);
    shm_puts("D7\r\n");

    /* ═══ D12: 启动调度器 ═══ */
    shm_puts("Start sched\r\n");
    vTaskStartScheduler();

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
