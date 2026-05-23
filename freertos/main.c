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
#include "chaos_encrypt.h"
#include "data_frame.h"
#include "shm_print.h"
#include "rpmsg_proto.h"
#include "master.h"

static int rpmsg_send_lora_raw(const u8 *frame, u32 frame_len);

static volatile int g_lora_rx_enabled = 1;
static volatile u32 g_real_frame_cnt = 0;
void master_lora_rx_ctrl(int enable) { g_lora_rx_enabled = enable; }
int  master_lora_rx_is_enabled(void) { return g_lora_rx_enabled; }

static u32 process_lora_frame(const u8 *buf, u32 buf_len, u32 *total_p);

static u32 build_lora_frame(u8 *out, u32 out_sz, u8 type, const u8 *payload, u16 plen)
{
    if (plen > 128) return 0;
    u32 ts = 1000;
    u8 enc_buf[128];
    u32 sync;
    u16 enc_len = chaos_encrypt_packet(payload, plen, enc_buf, &sync);
    u16 data_len = 4 + 1 + 4 + enc_len;
    u32 total = 2 + 2 + data_len + 1 + 2;
    if (total > out_sz) return 0;
    shm_spf("BUILD: type=%02X plen=%u enc=%u sync=%08X dlen=%u total=%u\r\n",
        type, plen, enc_len, sync, data_len, total);
    u32 p = 0;
    out[p++] = 0xAA; out[p++] = 0x55;
    out[p++] = (u8)(data_len >> 8); out[p++] = (u8)(data_len & 0xFF);
    out[p++] = (u8)(ts >> 24); out[p++] = (u8)(ts >> 16);
    out[p++] = (u8)(ts >> 8);  out[p++] = (u8)(ts & 0xFF);
    out[p++] = type;
    out[p++] = (u8)(sync >> 24); out[p++] = (u8)(sync >> 16);
    out[p++] = (u8)(sync >> 8);  out[p++] = (u8)(sync & 0xFF);
    memcpy(&out[p], enc_buf, enc_len); p += enc_len;
    out[p++] = 0x00;
    out[p++] = 0x55; out[p++] = 0xAA;
    return p;
}

static void inject_sim_frames(u32 *frame_cnt)
{
    shm_puts("\r\n=== INJECT: sim frames ===\r\n");
    {
        FaultUploadHeader_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.data_type   = DATA_TYPE_STATUS;
        hdr.severity    = SEVERITY_WARNING;
        hdr.timestamp   = 5000;
        hdr.fault_type  = FAULT_OVER_VOLTAGE;
        hdr.node_index  = 1;
        hdr.total_points = 8;
        hdr.sample_rate  = 2000;
        u8 frame[256];
        u32 flen = build_lora_frame(frame, sizeof(frame), DATA_TYPE_STATUS,
                                     (const u8 *)&hdr, sizeof(hdr));
        if (flen > 0) {
            u32 consumed = process_lora_frame(frame, flen, frame_cnt);
            shm_spf("INJECT STATUS: %uB consumed=%u\r\n", flen, consumed);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    {
        NodeSample_t samples[8];
        memset(samples, 0, sizeof(samples));
        for (int i = 0; i < 8; i++) {
            samples[i].active_power   = 1000 + i * 100;
            samples[i].reactive_power = 200 + i * 20;
            samples[i].voltage_angle  = 30 + i * 5;
            samples[i].voltage_mag    = 22000 + i * 100;
        }
        u8 frame[256];
        u32 flen = build_lora_frame(frame, sizeof(frame), DATA_TYPE_NODE_RAW,
                                     (const u8 *)samples, sizeof(samples));
        if (flen > 0) {
            u32 consumed = process_lora_frame(frame, flen, frame_cnt);
            shm_spf("INJECT NODE_RAW: %uB consumed=%u\r\n", flen, consumed);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    {
        uint8_t fault_data[4] = { 0x01, 0x02, 0x00, 0x00 };
        u8 frame[256];
        u32 flen = build_lora_frame(frame, sizeof(frame), DATA_TYPE_FAULT_LIST,
                                     fault_data, sizeof(fault_data));
        if (flen > 0) {
            u32 consumed = process_lora_frame(frame, flen, frame_cnt);
            shm_spf("INJECT FAULT_LIST: %uB consumed=%u\r\n", flen, consumed);
        }
    }
    shm_puts("=== INJECT: done ===\r\n");
}

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
    u32 pos = 0;
    while (pos + 1 < buf_len) {
        if (buf[pos] == 0xAA && buf[pos + 1] == 0x55) break;
        pos++;
    }
    if (pos + 1 >= buf_len) { shm_spf("PLF: no AA55 in %uB\r\n", buf_len); return (buf_len > 1) ? (buf_len - 1) : 0; }
    u32 hdr = pos + 2;
    if (hdr + 2 > buf_len) { shm_spf("PLF: too short for LEN\r\n"); return 0; }
    u16 data_len = ((u16)buf[hdr] << 8) | buf[hdr + 1];
    if (data_len > 200) { shm_spf("PLF: dlen=%u >200\r\n", data_len); return pos + 2; }
    u32 tail_pos = pos + 5 + data_len;
    if (tail_pos + 2 > buf_len) { shm_spf("PLF: tail overflow dlen=%u buf=%u tail=%u\r\n", data_len, buf_len, tail_pos); return 0; }
    if (buf[tail_pos] != 0x55 || buf[tail_pos + 1] != 0xAA) { shm_spf("PLF: bad tail [%02X][%02X]@%u\r\n", buf[tail_pos], buf[tail_pos+1], tail_pos); return pos + 2; }
    const u8 *data = &buf[pos + 4];
    u32 ts = ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
    u8  rx_type = data[4];
    u32 sync = ((u32)data[5] << 24) | ((u32)data[6] << 16) | ((u32)data[7] << 8) | data[8];
    u16 enc_len = data_len - 9;
    const u8 *enc_start = &data[9];
    (*total_p)++;
    g_real_frame_cnt++;
    u8  dec_buf[128];
    u16 dec_len = 0;
    /* 新版终端不再混沌加密, 直接使用 enc_start 作为 payload (匹配 GD32 V3-2) */
    if (enc_len <= sizeof(dec_buf)) { memcpy(dec_buf, enc_start, enc_len); dec_len = enc_len; }
    shm_spf("\r\n=== FRAME #%u: type=%02X ts=%u sync=%08X raw=%uB ===\r\n",
        *total_p, rx_type, ts, sync, dec_len);

    /* hex dump: 解密后的 payload 原始数据 */
    {
        u32 dump_len = dec_len;
        if (dump_len > 256) dump_len = 256;  /* 最多显示256字节 */
        for (u32 i = 0; i < dump_len; i++) {
            static const char hx[] = "0123456789ABCDEF";
            char h[4] = { hx[(dec_buf[i] >> 4) & 0xF], hx[dec_buf[i] & 0xF], ' ', 0 };
            shm_puts(h);
            if ((i + 1) % 16 == 0) shm_puts("\r\n");
        }
        if (dump_len % 16 != 0) shm_puts("\r\n");
        if (dec_len > 256) shm_spf("  ... (%uB total, showing first 256)\r\n", dec_len);
    }

    MasterDownloadBuf_t *dl = master_get_download_buf();
    uint8_t node_id;
    if (dl->active) {
        node_id = dl->node_id;
    } else {
        node_id = 0;
        if (rx_type == DATA_TYPE_STATUS && dec_len >= 3) {
            uint8_t idx = (dec_len >= 15) ? 10 : 2;
            node_id = dec_buf[idx];
        }
        if (node_id >= MASTER_MAX_NODES) node_id = 0;
    }
    uint16_t src_addr = 0x0001 + node_id;
    MasterNodeInfo_t *node = master_get_node_info(node_id);
    if (node) {
        node->is_online = 1;
        node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    switch (rx_type) {
    case DATA_TYPE_STATUS:
        if (dl) dl->active = 0;
        if (node) process_status_header(dec_buf, dec_len, src_addr, dl, node);
        break;
    case DATA_TYPE_WAVE:
        if (dl) dl->active = 0;
        if (node) process_wave_header(dec_buf, dec_len, dl, node, src_addr);
        /* WAVE头标记新波形会话开始, 输出 [FW_BEG] 供绘图脚本识别 */
        fw_wave_id++;
        fw_seq = 0;
        fw_lost = 0;
        fw_first_ts = 0;
        fw_last_ts = 0;
        fw_points = 0;
        fw_total_bytes = 0;
        fw_min_val = 0xFFFF;
        fw_max_val = 0;
        fw_in_wave = 1;
        shm_spf("[FW_BEG] wave#%u (from WAVE header)\r\n", fw_wave_id);
        break;
    case DATA_TYPE_POWER:
        shm_spf("  POWER: %uB (reserved)\r\n", dec_len);
        break;
    case DATA_TYPE_NODE_RAW:
        if (node) process_node_raw(dec_buf, dec_len, dl, node);
        /* 临时: 显示转换后的原始物理值 (÷10000) */
        {
            u16 n_s = dec_len / 16;  /* NodeSample_t = 4×int32 = 16B */
            for (u16 i = 0; i < n_s && i < 8; i++) {
                int32_t f[4];
                memcpy(f, &dec_buf[i * 16], 16);
                shm_spf("  [RAW] s%u: p=%d q=%d ang=%d vmag=%d (÷10000)\r\n",
                    i, f[0], f[1], f[2], f[3]);
            }
        }
        break;
    case DATA_TYPE_FLASH_WAVE:
        if (node) process_flash_wave(dec_buf, dec_len, dl, node);
        /* --- 逐帧 hex dump + 统计: FLASH_WAVE 波形 --- */
        {
            fw_pkt_cnt++;

            if (!fw_in_wave) {
                /* 没有WAVE头就开始收到FLASH_WAVE, 新建一个波形会话 */
                fw_wave_id++;
                fw_seq = 0;
                fw_lost = 0;
                fw_first_ts = ts;
                fw_points = 0;
                fw_total_bytes = 0;
                fw_min_val = 0xFFFF;
                fw_max_val = 0;
                fw_in_wave = 1;
                shm_spf("[FW_BEG] wave#%u\r\n", fw_wave_id);
            }

            /* 逐帧 hex dump: 完整 payload, 不截断, 供 plot_wave.py 解析 */
            shm_spf("[FW_DAT p=%u ts=%u len=%u]\r\n", fw_seq, ts, dec_len);
            {
                static const char hx[] = "0123456789ABCDEF";
                for (u32 i = 0; i < dec_len; i++) {
                    char h[3] = { hx[(dec_buf[i] >> 4) & 0xF], hx[dec_buf[i] & 0xF], 0 };
                    shm_puts(h);
                }
            }
            shm_puts("\r\n");
            fw_total_bytes += dec_len;

            /* 检查连续性: 每包int16点, timestamp应递增 */
            if (fw_seq == 0) {
                fw_first_ts = ts;
            } else {
                u32 ts_gap = ts - fw_last_ts;
                if (ts_gap > 200) {
                    fw_lost++;
                    shm_spf("  [WAVE-STAT] GAP! ts_gap=%u ms\r\n", ts_gap);
                }
            }
            fw_last_ts = ts;

            /* 统计int16波形值范围 */
            u16 pts_in_pkt = dec_len / 2;
            fw_points += pts_in_pkt;
            for (u16 i = 0; i < pts_in_pkt; i++) {
                s16 v = (s16)((dec_buf[i * 2 + 1] << 8) | dec_buf[i * 2]);
                u16 av = (v < 0) ? (u16)(-v) : (u16)v;
                if (av < fw_min_val) fw_min_val = av;
                if (av > fw_max_val) fw_max_val = av;
            }

            shm_spf("  [WAVE-STAT] wave#%u pkt#%u pts=%u total=%u lost=%u ts=%u val=[%u..%u]\r\n",
                fw_wave_id, fw_seq, pts_in_pkt, fw_points, fw_lost, ts, fw_min_val, fw_max_val);

            fw_seq++;

            /* 波形接收完毕: dl->active 由 process_flash_wave 在 received_points>=expected_points 时清零 */
            if (dl && !dl->active) {
                shm_spf("[FW_END] wave#%u pkts=%u bytes=%u pts=%u lost=%u ts=[%u..%u] val=[%u..%u]\r\n",
                    fw_wave_id, fw_seq, fw_total_bytes, fw_points, fw_lost,
                    fw_first_ts, fw_last_ts, fw_min_val, fw_max_val);
                fw_in_wave = 0;
            }
        }
        break;
    case DATA_TYPE_FAULT_LIST:
        if (dl) dl->active = 0;
        if (node) process_fault_list(dec_buf, dec_len, src_addr, node);
        break;
    default:
        shm_spf("  UNKNOWN(%02X): %uB\r\n", rx_type, dec_len);
        break;
    }

    {
        u32 raw_len = tail_pos + 2 - pos;
        int r = rpmsg_send_lora_raw(&buf[pos], raw_len);
        if (r < 0)
            shm_spf("  RPMsg TX FAIL (%d)\r\n", r);
        else
            shm_spf("  RPMsg TX %uB ok\r\n", raw_len);
    }

    return tail_pos + 2;
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
    shm_puts("L1 v8\r\n");

    /* ─── 第1步: MD0=HIGH → AT命令模式 ─── */
    GDR(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);  /* MD0 输出 HIGH */
    vTaskDelay(pdMS_TO_TICKS(500));              /* 等 LoRa 模块进入 AT 模式 */

    shm_puts("L2\r\n");

    /*
     * AT 命令序列: Linux 侧已验证格式 + GD32_V3 mwcc68_cfg.h 参数
     * 参数: ADDR=0x0B, CHN=23, NETID=0, POWER=20dBm(TPOWER=4),
     *       RATE=19.2kbps(WLRATE=23,5), TMODE=FP(TMODE=1),
     *       PACKSIZE=240(PACKSIZE=3), BPS=115200(UART=7,0)
     */
    shm_puts("LoRa: AT init (verified format)\r\n");

    /* 辅助宏: 发送 AT 命令 + 轮询读回响应 (回退轮询模式) */
    #define AT_SEND(s) do { \
        const char *_p = (s); \
        while (*_p) { while (U2_FR & (1U << 5)); U2_DR = (u32)*_p++; } \
        while (U2_FR & (1U << 5)); U2_DR = '\r'; \
        while (U2_FR & (1U << 5)); U2_DR = '\n'; \
        vTaskDelay(pdMS_TO_TICKS(200)); \
        shm_puts(s); shm_puts(": "); \
        for (int _i = 0; _i < 200; _i++) { \
            if (!(U2_FR & (1U << 4))) { \
                u8 _b = (u8)(U2_DR & 0xFF); \
                static const char _hx[] = "0123456789ABCDEF"; \
                char _t[4] = { _hx[(_b >> 4) & 0xF], _hx[_b & 0xF], ' ', 0 }; \
                shm_puts(_t); \
            } else { vTaskDelay(pdMS_TO_TICKS(1)); } \
        } \
        shm_puts("\r\n"); \
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

    shm_puts("LoRa: AT config done\r\n");
    shm_puts("L3\r\n");

    /* ─── 第2步: MD0=LOW → 透传模式 (匹配 LoRa_ExitConfigMode) ─── */
    GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN);
    shm_puts("L4\r\n");

    /* 等 AUX 变 HIGH (模块切换完成, 最多等 5s) */
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if ((GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U) break;
    }
    shm_spf("LoRa: MD0=LOW AUX=%u (ready)\r\n",
        (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U);
    shm_puts("LoRa: RX ready (IRQ)\r\n");
    shm_puts("L5\r\n");

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
    shm_puts("L6-IRQ\r\n");

    static u8  lora_frame_buf[1024];
    static u32 lora_pos   = 0;
    static u32 stable_cnt = 0;
    static u32 frame_cnt  = 0;
    u32 loop_cnt = 0;
    u32 sim_injected = 0;
    shm_puts("L7-IRQ-v4\r\n");
    for (;;) {
        u32 avail = rp_avail();
        if (avail > 0) {
            while (lora_pos < sizeof(lora_frame_buf)) {
                u8 b;
                if (rp_get(&b) != 0) break;
                lora_frame_buf[lora_pos++] = b;
            }
            if (lora_pos >= sizeof(lora_frame_buf))
                goto force_parse;
            stable_cnt = 0;
        } else {
            if (lora_pos > 0) {
                stable_cnt++;
                if (stable_cnt >= 5) {
force_parse:
                    u32 consumed = process_lora_frame(lora_frame_buf, lora_pos, &frame_cnt);
                    if (consumed > 0) {
                        if (consumed < lora_pos) {
                            memmove(lora_frame_buf, lora_frame_buf + consumed, lora_pos - consumed);
                        }
                        lora_pos -= consumed;
                    } else {
                        if (lora_pos >= sizeof(lora_frame_buf)) {
                            for (u32 i = 0; i + 1 < lora_pos; i++) {
                                if (lora_frame_buf[i] == 0xAA && lora_frame_buf[i+1] == 0x55) {
                                    if (i > 0) {
                                        memmove(lora_frame_buf, lora_frame_buf + i, lora_pos - i);
                                        lora_pos -= i;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    stable_cnt = 0;
                }
            }
        }
        loop_cnt++;
        shm_hb_inc();
        if ((loop_cnt % 1500) == 0) {
            u32 aux = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;
            shm_spf("[RX-hb] loop=%u AUX=%u tx=%u real=%u isr=%u dat=%u rpmsg=%u/%u\r\n",
                loop_cnt, aux, frame_cnt, g_real_frame_cnt, isr_cnt, isr_data,
                g_rpmsg_tx_cnt, g_rpmsg_tx_err);
        }
        if (sim_injected == 0 && loop_cnt > 1500 && g_real_frame_cnt == 0) {
            inject_sim_frames(&frame_cnt);
            sim_injected = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
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
    /* UART2 IMSC 将在 lora_task 透传模式下使能, 避免 AT 阶段冲突 */
    shm_puts("D6-IRQ\r\n");

    /* ═══ D11: 创建任务 ═══ */
    master_init();
    xTaskCreate(rpm_task,  "RPM",  RPM_STK,  NULL, RPM_PRIO,  NULL);
    xTaskCreate(aux_task,  "AUX",  AUX_STK,  NULL, AUX_PRIO,  NULL);
    xTaskCreate(lora_task, "LoRa", LORA_STK, NULL, LORA_PRIO, NULL);
    xTaskCreate(master_judge_task, "Judge", MASTER_JUDGE_STK_SIZE, NULL, MASTER_JUDGE_TASK_PRIO, NULL);
    xTaskCreate(master_cmd_task, "Cmd", MASTER_CMD_STK_SIZE, NULL, MASTER_CMD_TASK_PRIO, NULL);
    shm_puts("D7\r\n");

    /* ═══ D12: 启动调度器 ═══ */
    shm_puts("Start sched\r\n");
    vTaskStartScheduler();

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
