# FreeRTOS LoRa 接收调试指南

> **最后更新**: 2026-05-21  |  **当前版本**: v10 Final  |  **状态**: 生产就绪 ✅
>
> 本指南记录从零搭建 FreeRTOS OpenAMP LoRa 接收系统的完整过程，包含所有踩过的坑和解决方案。

## 目录结构

```
/home/alientek/
├── Phytium_syscode/phytium-free-rtos-sdk-master/   ← 飞腾 SDK (含编译系统)
│   └── example/system/amp/openamp_for_linux/        ← SDK 工程目录 (编译入口)
│       ├── main.c                                   ← 核心源码 (与下方同步)
│       ├── inc/  src/                               ← 头文件/模块源码
│       ├── build/                                   ← 编译输出
│       └── makefile  sdkconfig                      ← 编译系统
│
├── Phytium/freertos/                                ← **工作目录** (源码+部署+文档)
│   ├── main.c                                       ← 核心源码 (最新版, 与SDK同步)
│   ├── inc/  src/                                   ← 头文件/模块源码
│   ├── deploy.sh                                    ← 一键编译+部署+验证
│   ├── DEBUG_GUIDE.md                               ← 本调试指南 (完整记录)
│   ├── HANDOVER.md                                  ← 交接文档
│   └── restart_lora.sh                              ← 快速重启LoRa模块脚本
│
├── Phytium/GD32L233C_Prj_Master_v3/                  ← GD32 主控器参考源码
│   └── app/task/master_recv.c                       ← 帧格式权威参考
│
└── Phytium/src/linux-app/                            ← Linux 侧应用
    ├── trace_reader.c                                ← 共享内存读取工具源码
    └── lora_receiver.c                               ← Linux 直连 LoRa (备选方案)
```

### 关键路径速查

| 用途 | 路径 |
|------|------|
| 编译 | `cd Phytium/freertos && bash deploy.sh` |
| SDK 编译入口 | `Phytium_syscode/.../openamp_for_linux/` |
| 当前源码 (编辑) | `Phytium/freertos/main.c` |
| 固件 ELF | `openamp_for_linux/pe2204_aarch64_phytiumpi_openamp_for_linux.elf` |
| 开发板固件 | `/lib/firmware/openamp_core0.elf` |
| GD32 参考 | `Phytium/GD32L233C_Prj_Master_v3/app/task/master_recv.c` |

## 架构

```
FreeRTOS (homo_core0, 0xb0100000)
    │
    ├── f_printk ──→ 共享内存环形缓冲区 (0xC8000000, 1MB)
    │                    │
    │                    └── Linux /dev/mem mmap ──→ trace_reader
    │
    ├── GPIO3_1 (MD0)  ──→ IOPAD C49 FUNC6 ──→ 配置 LoRa 模块模式
    ├── GPIO2_10 (AUX) ──→ IOPAD A37 FUNC6 ──→ 读 LoRa 模块状态
    │
    └── UART2 (0x2800E000) ──→ LoRa 模块数据收发
```

## 快速开始

### 一键部署 (推荐)

```bash
cd /home/alientek/Phytium/freertos && bash deploy.sh
```

脚本会自动完成: 编译 → 传输 → 安全启动 → 验证数据

### 手动编译

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

make config_pe2204_phytiumpi_aarch64
make clean
make all -j$(nproc)
# 产物: pe2204_aarch64_phytiumpi_openamp_for_linux.elf
```

### 部署

```bash
# 拷贝到开发板
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.11:/tmp/

# 开发板上替换固件
ssh user@192.168.88.11
sudo cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf /lib/firmware/openamp_core0.elf
sudo reboot
```

### 验证（每次重启后）

```bash
# 编译 trace_reader (首次)
ssh user@192.168.88.11
cat > /tmp/trace_reader.c << 'EOS'
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#define BASE 0xC8000000
#define BSZ (1048576)
typedef struct { volatile uint32_t wi; volatile uint32_t ri;
  volatile uint32_t ov; char buf[BSZ-12]; } TB;
int main(void) {
  int fd=open("/dev/mem",O_RDWR|O_SYNC);
  if(fd<0){perror("open");return 1;}
  volatile TB*tb=(volatile TB*)mmap(NULL,BSZ,PROT_READ|PROT_WRITE,MAP_SHARED,fd,BASE);
  if(tb==MAP_FAILED){perror("mmap");return 1;}
  printf("[reader] 0x%X buf=%u\n",BASE,BSZ);
  uint32_t lr=tb->ri;
  while(1){uint32_t wr=tb->wi;
    while(lr!=wr){putchar(tb->buf[lr]);lr=(lr+1)%(BSZ-12);}
    fflush(stdout);usleep(50000);
  }
}
EOS
gcc -Wall -O2 -o /home/user/trace_reader /tmp/trace_reader.c

# 运行
sudo /home/user/trace_reader
# 或快速检查 FreeRTOS 是否存活:
sudo busybox devmem 0xC8000000 32   # write_index != 0 说明 FreeRTOS 在工作
```

## 关键技术点

### 0. f_printk 半可靠 —— 最终方案：裸写共享内存

**教训**: 虽然 `CONFIG_OPENAMP_TRACE_DEBUG=y` 理论上可以让 `f_printk` 走共享内存 trace，
但实际在 FreeRTOS 多任务环境中，`ftrace_printk` 内部依赖 `trace_buffer_init` → `FMmuMap` 
的自动初始化时序不确定，且 `vsnprintf` 等标准库函数在某些编译配置下行为异常，
导致新代码中 `f_printk` 不可靠（write_index 始终为 0）。

**最终方案**: 绕过所有 SDK trace 函数，直接用 volatile 宏裸写共享内存：
```c
#define SHM_BASE ((volatile u32*)0xC8000000UL)
#define SHM_WI   SHM_BASE[0]
#define SHM_BUF  ((volatile char*)(SHM_BASE+3))

#define put(c) do{ u32 w=SHM_WI,r=SHM_BASE[1],n=(w+1)%((1048576-12)); \
                   if(n!=r){SHM_BUF[w]=(c);SHM_WI=n;} }while(0)
#define puts(s) do{ const char*__p=s; while(*__p)put(*__p++); }while(0)
```
此方案零库函数依赖，Step1 已验证可靠。后续用 `snprintf` + `puts` 实现格式化输出。

### 1. 共享内存打印 (Step1)

**方案**: 裸写 volatile 指针到 `0xC8000000`，Linux 侧通过 `/dev/mem` mmap 读取。
- 需要在 `main.c` 开头调用 `trace_buffer_init()`

**陷阱**:
- `0xC8000000` 必须在设备树中预留(已确认: rproc@b0100000 覆盖 0xb0100000~0xc99fffff)
- Linux CONFIG_STRICT_DEVMEM=y 不影响预留内存的 mmap
- 需要 `FMmuMap(0xC8000000, ...)` 做 MMU 映射

### 2. IOMUX 配置 GPIO (Step2)

**关键发现**: `FIOMuxInit()` 依赖的 IOPAD 寄存器基址 `0x32B30000` 需要 MMU 映射!

**方案**: 直接写 IOPAD 寄存器，不依赖 SDK 的 FIOMuxInit

**引脚映射表**:

| 功能 | GPIO | IOPAD偏移 | FUNC | 基址 | 寄存器偏移 |
|------|------|-----------|------|------|------------|
| MD0 | GPIO3_1 (pin1) | 0x00E0 (C49) | 6 | 0x28036000 | DR=0x00, DDR=0x04 |
| AUX | GPIO2_10 (pin10) | 0x00C4 (A37) | 6 | 0x28035000 | EXT=0x08 |

**GPIO 寄存器操作**:
```c
#define GPIO_DDR(b)  (*(volatile u32 *)((u32)(b) + 0x04))
#define GPIO_DR(b)   (*(volatile u32 *)((u32)(b) + 0x00))
#define GPIO_EXT(b)  (*(volatile u32 *)((u32)(b) + 0x08))

// 输出 HIGH
GPIO_DDR(addr) |= (1U << pin);   // 设为输出
GPIO_DR(addr)  |= (1U << pin);   // 输出高

// 输入
GPIO_DDR(addr) &= ~(1U << pin);  // 设为输入
val = (GPIO_EXT(addr) >> pin) & 1U;  // 读输入
```

**IOPAD 配置**:
```c
#define IOPAD_BASE  0x32B30000U
#define FUNC6       6U  // GPIO 复用功能号

// 需要先 MMU 映射
FMmuMap(IOPAD_BASE, IOPAD_BASE, 0x1000U, MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);

// 写 IOPAD 寄存器
volatile u32 *reg = (volatile u32 *)(IOPAD_BASE + pin_offset);
*reg = (*reg & ~0x7U) | (FUNC6 & 0x7U);
```

### 3. 依赖的 SDK 常量 (来自 fparameters.h)

```
FUART2_BASE_ADDR    0x2800E000
FGPIO2_BASE_ADDR    0x28035000
FGPIO3_BASE_ADDR    0x28036000
FGPIO_CTRL_2        2
FGPIO_CTRL_3        3
FGPIO_ID(ctrl, pin) = (ctrl * 16) + pin
```

### 4. 调试陷阱总结

| 陷阱 | 现象 | 原因 | 解决 |
|------|------|------|------|
| f_printk 无输出 | trace_reader 看到 write_index=0 | f_printk 走 printf_call→UART, printf_call=NULL 或 UART1 被 Linux 占 | 开 CONFIG_OPENAMP_TRACE_DEBUG |
| 共享内存写不进去 | write_index=0 但 devmem 可读写 | MMU 未映射 | FMmuMap(0xC8000000, ...) |
| GPIO 读始终为 0 | 电平改变无反应 | IOPAD 未切换引脚功能到 GPIO | 直接写 IOPAD 寄存器 FUNC6 |
| FIOMuxInit 崩溃 | FreeRTOS 不启动 | IOPAD 基址未 MMU 映射 | 跳过 SDK IOMUX, 直接写寄存器 |
| SD卡 GPIO 函数未链接 | undefined reference to FGpioXxx | fgpio.c 不在当前链接库中 | 用直接寄存器操作替代 SDK API |
| MD0/AUX 引脚搞反 | AUX 读数不匹配预期 | 代码中 GPIO2_10/GPIO3_1 分配反了 | 确认: MD0=GPIO3_1, AUX=GPIO2_10 |

### 5. 开发板信息来源

```
remoteproc 状态: /sys/class/remoteproc/remoteproc0/state
RPMsg 通道:      /sys/bus/rpmsg/devices/
RPMsg 设备名:    /sys/class/rpmsg/rpmsg0/name  → "rpmsg-openamp-demo-channel"
设备树预留内存:   dmesg | grep reserved
物理内存布局:    /proc/iomem
UART 设备:        /dev/ttyAMA0 ~ AMA3
GPIO 寄存器:      busybox devmem 0x28035000 32  (实时查看)
```

### 6. Step3 完成状态 (2025-06-26)

**核心代码**: SDK 项目 `openamp_for_linux/main.c`, 523行(含注释)
**备份路径**: `/home/alientek/Phytium/freertos/main.c`

**已验证功能**:
| 功能 | 状态 |
|------|------|
| FreeRTOS 启动 + 调度器运行 | ✅ `=== Step3 LoRa RX ===` |
| RPMsg 通道建立 | ✅ `RPMsg done` (dmesg: rpmsg host is online) |
| AUX GPIO2_10 输入检测 | ✅ 变化时打印, 30s 心跳 |
| IOPAD UART2 TX/RX | ✅ A47/A49 → FUNC0 |
| UART2 PL011 115200 8N1 | ✅ CR=0x0301 |
| LoRa AT 配置 (MD0=HIGH) | ✅ 模块回复 `OK` 已收到 |
| LoRa 透传模式 (MD0=LOW) | ✅ AUX 等待就绪 |
| UART2 轮询接收 | ✅ RX #1 看到了 AT 命令响应 |

**代码布局**:
```
main.c 第1部分 — 共享内存打印 (裸写 volatile)    — 第 63~110 行
main.c 第2部分 — 引脚定义 (MD0/AUX)              — 第 113~123 行
main.c 第3部分 — GPIO 寄存器宏                   — 第 126~139 行
main.c 第4部分 — IOPAD 引脚复用                  — 第 142~166 行
main.c 第5部分 — UART2 PL011 寄存器              — 第 169~190 行
main.c 第6部分 — UART2 接收环形缓冲区            — 第 193~223 行
main.c 第7部分 — FreeRTOS 任务参数               — 第 226~238 行
main.c 第8部分 — OpenAMP Resource Table + RemoteProc — 第 241~305 行
main.c 第9部分 — aux_task (AUX监控)              — 第 308~360 行
main.c 第10部分 — lora_task (UART2轮询+AT配置)    — 第 363~498 行
main.c 第11部分 — rpm_task (RPMsg轮询)           — 第 501~536 行
main.c 第12部分 — main() 入口                    — 第 539~597 行
```

## 轮询模式参考实现 (v6, 2026-05-21 已验证稳定)

> **设计意图**: 纯轮询无中断依赖，所有状态可经共享内存实时观测。若后续中断模式出问题，参考本章即可回退到当前稳定状态。

### 架构总览

```
┌─────────────────────────────────────────────────────────┐
│  FreeRTOS (CPU3, MT_DEVICE_NGNRNE 共享内存)             │
│                                                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │
│  │ rpm_task │  │ aux_task │  │ lora_task             │  │
│  │ prio=1   │  │ prio=2   │  │ prio=3  (主任务)      │  │
│  │          │  │          │  │                       │  │
│  │ RPMsg    │  │ AUX监控  │  │ AT配置 → 透传 →      │  │
│  │ 轮询     │  │ 100ms    │  │ 轮询UART2 FIFO        │  │
│  │ WFI      │  │ GPIO输入 │  │ 10ms间隔 hex dump     │  │
│  └──────────┘  └──────────┘  └──────────────────────┘  │
│        │             │              │                   │
│        └─────────────┼──────────────┘                   │
│                      │ SHM_HB++                         │
│                      ▼                                  │
│  ┌───────────────────────────────────────────────────┐  │
│  │  共享内存 0xC8000000 (1MB, MT_DEVICE_NGNRNE)      │  │
│  │  [0]=WI  [1]=RI  [2]=HB  [3+]=环形缓冲区          │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
         ▲ /dev/mem mmap
         │
    ┌────┴─────┐
    │ trace_reader │
    └──────────────┘
```

### 初始化顺序（必须严格遵循）

```
init_system()           ← 必须第一句（SDK 平台初始化）
FMmuMap(0xC8000000, MT_DEVICE_NGNRNE)  ← 共享内存非缓存，全写直通 DDR
FMmuMap(UART2/GPIO/IOPAD, MT_DEVICE_NGNRNE)
SHM_WI=0; SHM_RI=0; SHM_HB=0          ← 清零
清零环形缓冲区
puts("=== v6 DEV ===")                 ← 此时安全，所有地址已映射
RPMsg 初始化 (rproc + rpmsg_vdev)
IOMUX 引脚复用 (C49→GPIO, A37→GPIO, A47/A49→UART2)
GPIO 方向 (MD0=输出HIGH, AUX=输入)
UART2 初始化 (115200 8N1, FIFO使能)
xTaskCreate(rpm_task, AUX_task, LoRa_task)
vTaskStartScheduler()
```

### 头文件依赖（无 SDK 头文件）

```c
/* 仅需以下标准头 */
#include "FreeRTOS.h"
#include "task.h"
#include "fparameters.h"       /* FUART2_BASE_ADDR, FGPIO2_BASE_ADDR 等 */
#include "openamp_device.h"    /* RPMsg / Resource Table */
#include "openamp_vdev.h"
```

### 外设基址（直接硬编码，不依赖 SDK API）

```c
#define U2_BASE     0x2800E000UL   /* UART2 PL011 */
#define IP_BASE     0x32B30000UL   /* IOPAD 引脚复用 */
#define GPIO2_BASE  0x28035000UL   /* FGPIO2 (含 AUX=pin10) */
#define GPIO3_BASE  0x28036000UL   /* FGPIO3 (含 MD0=pin1)  */
```

### 共享内存宏（核心输出机制）

```c
#define SHM_BASE  ((volatile u32 *)0xC8000000UL)
#define SHM_WI    (SHM_BASE[0])    /* write_index: 写指针 */
#define SHM_RI    (SHM_BASE[1])    /* read_index: 读指针 */
#define SHM_HB    (SHM_BASE[2])    /* heartbeat: 心跳计数器 */
#define SHM_BUF   ((volatile char *)(SHM_BASE + 3))  /* 缓冲区起始(字节偏移12) */
#define SHM_BSZ   (1024 * 1024 - 12)  /* 缓冲区大小 */

/* 单字符写入, 环形覆盖保护 */
#define put(c) do { \
    u32 w = SHM_WI, r = SHM_RI, n = (w + 1) % SHM_BSZ; \
    if (n != r) { SHM_BUF[w] = (c); SHM_WI = n; } \
} while (0)

/* 字符串写入 */
#define puts(s) do { \
    const char *__p = (s); \
    while (*__p) put(*__p++); \
} while (0)

/* 格式化输出 (栈上 256B 缓冲 → vsnprintf → puts) */
static void spf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    puts(buf);
}
```

### GPIO 寄存器宏

```c
#define GDR(b)  (*(volatile u32 *)((b) + 0x00))  /* 数据寄存器 */
#define GDD(b)  (*(volatile u32 *)((b) + 0x04))  /* 方向寄存器 (1=输出,0=输入) */
#define GEX(b)  (*(volatile u32 *)((b) + 0x08))  /* 外部输入寄存器 */

#define AUX_PIN   10   /* GPIO2_10: LoRa 模块 AUX 状态 */
#define MD0_PIN    1   /* GPIO3_1:  LoRa 模块模式控制 */

/* 读 AUX:   (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U */
/* 写 MD0:   输出 → GDD(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN) */
/*              高 → GDR(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN) */
/*              低 → GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN) */
```

### IOPAD 引脚复用

```c
#define IP_C49    194    /* C49 = GPIO3_1 (MD0)  — 偏移 0x00E0, FUNC6=GPIO */
#define IP_A37    101    /* A37 = GPIO2_10 (AUX) — 偏移 0x00C4, FUNC6=GPIO */
#define IP_A47    123    /* A47 = UART2 TX      — 偏移 0x00F8, FUNC0=UART */
#define IP_A49    125    /* A49 = UART2 RX      — 偏移 0x0100, FUNC0=UART */

static void ipset(int idx, u32 func)
{
    u32 off = (u32)(idx * 4);
    volatile u32 *r = (volatile u32 *)(IP_BASE + off);
    *r = (*r & ~0x7U) | (func & 0x7U);
}
```

### UART2 PL011 寄存器

```c
#define U2_DR    (*(volatile u32 *)(U2_BASE + 0x00))  /* 数据寄存器 */
#define U2_FR    (*(volatile u32 *)(U2_BASE + 0x18))  /* 标志寄存器 */
#define U2_IBRD  (*(volatile u32 *)(U2_BASE + 0x24))  /* 整数波特率 */
#define U2_FBRD  (*(volatile u32 *)(U2_BASE + 0x28))  /* 小数波特率 */
#define U2_LCR_H (*(volatile u32 *)(U2_BASE + 0x2C))  /* 线路控制 */
#define U2_CR    (*(volatile u32 *)(U2_BASE + 0x30))  /* 控制寄存器 */

/* FR 标志位 */
#define FR_TXFF   (1U << 5)   /* TX FIFO 满 */
#define FR_RXFE   (1U << 4)   /* RX FIFO 空 */

/* 初始化: 115200 8N1, FIFO 使能 */
U2_CR    = 0;           /* 先禁用 UART */
U2_IBRD  = 54;          /* 100MHz / (16 × 115200) ≈ 54.25 → 整数=54 */
U2_FBRD  = 16;          /* 小数部分 0.25 × 64 = 16 */
U2_LCR_H = 0x70;        /* 8bit word + FIFO 使能 */
U2_CR    = 0x0301;      /* UARTEN | TXE | RXE */
```

### AT 命令发送宏（纯轮询，无中断依赖）

```c
/*
 * 发送 AT 字符串 + 读取响应(最多200字符, hex dump)
 * 轮询等待 TX FIFO 就绪(FR_TXFF=0) 再写数据
 * 轮询读取 RX FIFO(FR_RXFE=0) 再读数据
 */
#define AT_SEND(s) do { \
    const char *_p = (s); \
    while (*_p) { while (U2_FR & FR_TXFF); U2_DR = (u32)*_p++; } \
    while (U2_FR & FR_TXFF); U2_DR = '\r'; \
    while (U2_FR & FR_TXFF); U2_DR = '\n'; \
    vTaskDelay(pdMS_TO_TICKS(200)); \
    puts(s); puts(": "); \
    for (int _i = 0; _i < 200; _i++) { \
        if (!(U2_FR & FR_RXFE)) { \
            u8 _b = (u8)(U2_DR & 0xFF); \
            static const char _hx[] = "0123456789ABCDEF"; \
            char _t[4] = { _hx[(_b >> 4) & 0xF], _hx[_b & 0xF], ' ', 0 }; \
            puts(_t); \
        } else { vTaskDelay(pdMS_TO_TICKS(1)); } \
    } \
    puts("\r\n"); \
} while (0)
```

### LoRa 任务：AT 配置 + 透传轮询（完整流程）

```c
static void lora_task(void *pv)
{
    u32 pkt = 0;

    /* ─── 第1步: MD0=HIGH → AT 命令模式 ─── */
    GDR(FGPIO3_BASE_ADDR) |=  (1U << MD0_PIN);  /* MD0 输出 HIGH */
    vTaskDelay(pdMS_TO_TICKS(500));              /* 等待模块进入 AT 模式 */

    /* ─── 第2步: AT 命令序列 ─── */
    for (int at_retry = 0; at_retry < 3; at_retry++) {
        AT_SEND("AT");        /* 连通性检查，最多重试3次 */
        if (at_retry < 2) vTaskDelay(pdMS_TO_TICKS(300));
    }
    AT_SEND("AT+ADDR?");      /* 查询当前地址 */
    AT_SEND("AT+ADDR=00,0B"); /* 地址=0x000B */
    AT_SEND("AT+NETID=0");    /* 网络ID */
    AT_SEND("AT+WLRATE=23,5");/* 信道23 + 速率19.2kbps */
    AT_SEND("AT+PACKSIZE=3"); /* 3=240字节包 */
    AT_SEND("AT+TMODE=1");    /* 1=定点传输 */
    AT_SEND("AT+TPOWER=4");   /* 4=20dBm */
    AT_SEND("AT+UART=7,0");   /* 7=115200, 0=8N1 */
    AT_SEND("AT+FLASH=1");    /* 保存到 Flash */

    /* ─── 第3步: MD0=LOW → 透传模式 ─── */
    GDR(FGPIO3_BASE_ADDR) &= ~(1U << MD0_PIN);

    /* 等待 AUX=HIGH (模块切换完成，最多等5秒) */
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if ((GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U) break;
    }

    /* ─── 第4步: 轮询接收循环 ─── */
    u32 loop_cnt = 0, idle_cnt = 0;
    for (;;) {
        u32 n = 0;
        u8 buf[64];

        /* 读 UART2 RX FIFO 到本地缓冲区 (最多64字节) */
        while (!(U2_FR & FR_RXFE) && n < 64)
            buf[n++] = (u8)(U2_DR & 0xFF);

        if (n > 0) {
            /* 有新数据: hex dump + 包计数 */
            if (idle_cnt > 100)
                spf("\r\n!! NEW after %us idle\r\n", idle_cnt / 100);
            idle_cnt = 0;
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
        SHM_HB++;  /* 心跳 */
        if ((loop_cnt % 300) == 0) {
            /* 每3秒心跳输出 (300 × 10ms = 3s) */
            u32 aux = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;
            spf("[RX-hb] loop=%u AUX=%u idle=%us\r\n",
                loop_cnt, aux, idle_cnt / 100);
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* 10ms 轮询间隔 */
    }
}
```

**轮询间隔设计依据**: 115200bps ÷ 10bit/byte ≈ 11520 bytes/s。10ms 内最多到达 115 字节，UART2 FIFO 深度 16 字节，64 字节本地缓冲足够。

### AUX 监控任务

```c
static void aux_task(void *pv)
{
    u32 last = 99;   /* 上次电平 (99=初始未知) */
    u32 tick = 0;

    for (;;) {
        u32 v = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;

        if (v != last) {
            spf("A %u->%u\r\n", last, v);  /* 仅变化时打印 */
            last = v;
        }

        tick++;
        SHM_HB++;  /* 心跳 */
        if ((tick % 300) == 0)
            spf("[hb] t=%u A=%u\r\n", tick, v);  /* 每30s心跳 */

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**AUX 电平含义**:
| AUX | 含义 |
|-----|------|
| 1 (HIGH) | 模块空闲，可接收 AT 命令或切换模式 |
| 0 (LOW)  | 模块忙（收/发数据中 or 正在处理） |

### RPMsg 任务

```c
static void rpm_task(void *pv)
{
    struct rpmsg_endpoint le = {0};

    /* 初期快速轮询让 RPMsg 建立 */
    for (int i = 0; i < 500; i++) {
        platform_poll(&rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 注册 RPMsg 通道 */
    (void)rpmsg_create_ept(&le, rpdev,
                           RPMSG_SERVICE_NAME, 0,
                           RPMSG_ADDR_ANY, NULL, NULL);

    /* 持续轮询 maintain RPMsg 连接 */
    while (1) {
        SHM_HB++;  /* 心跳 */
        platform_poll(&rproc);
    }
}
```

**注意**: `platform_poll()` 内部在 `CONFIG_USE_OPENAMP_IPI` 启用时调用 `WFI` 指令，CPU 在无 IPI 时休眠。优先级高不影响 LoRa 任务是因为 IPI 到达时 CPU 唤醒并调度 LoRa 任务。

### 任务参数

```c
#define LORA_PRIO  3      /* LoRa 任务优先级 (数字大=优先低) */
#define AUX_PRIO   2      /* AUX 监控优先级 */
#define RPM_PRIO   1      /* RPMsg 优先级 (最高, 数字小=优先高) */
#define LORA_STK   8192   /* LoRa 栈 (StackType_t=size_t=8B, 实际 64KB) */
#define AUX_STK    2048   /* AUX 栈 (实际 16KB) */
#define RPM_STK    8192   /* RPMsg 栈 (实际 64KB) */

xTaskCreate(lora_task, "LoRa", LORA_STK, NULL, LORA_PRIO, NULL);
xTaskCreate(aux_task,  "AUX",  AUX_STK,  NULL, AUX_PRIO,  NULL);
xTaskCreate(rpm_task,  "RPM",  RPM_STK,  NULL, RPM_PRIO,  NULL);
```

### Linux 侧验证命令速查

```bash
# 编译 trace_reader (仅首次)
gcc -O2 -o /home/user/trace_reader /tmp/trace_reader.c

# 运行（持续观察）
sudo /home/user/trace_reader

# 快速检查 FreeRTOS 是否存活 (WI > 0 且递增 = 正常)
sudo busybox devmem 0xC8000000 32   # WI
sudo busybox devmem 0xC8000008 32   # HB (每100ms递增)

# 6秒心跳观察
for i in $(seq 0 5); do
  echo "T+${i}s: HB=$(sudo busybox devmem 0xC8000008 32) WI=$(sudo busybox devmem 0xC8000000 32)"
  sleep 1
done

# LoRa AT 验证 (Linux 侧 minicom)
picocom -b 115200 /dev/ttyAMA2
# 手动输入: AT, AT+ADDR? 确认模块配置
```

### 关键决策记录

| 决策 | 原因 | 替代方案 |
|------|------|----------|
| 共享内存 MT_DEVICE_NGNRNE | Cache 使 Linux devmem 读不到新数据 | MT_NORMAL + 手动 cache flush |
| 纯轮询(无中断) | 避免 ISR 与 AT_SEND 宏冲突、中断路由复杂度 | 中断模式(已实现但回退) |
| 裸写 volatile 指针 | f_printk 在 AMP 环境下不可靠 | f_printk + CONFIG_OPENAMP_TRACE_DEBUG |
| AT 命令后 vTaskDelay(100ms) | 模块处理需要时间 | AUX 轮询等待 |
| 10ms 轮询间隔 | 115200bps 10ms 最多 115 字节, FIFO 16B | 更短间隔(功耗更高) |

---

---

## 中断模式实现 (v7 IRQ, 2026-05-21)

> **里程碑**: 从轮询模式迁移到 GICv3 硬件中断，从根本上解决了 LoRa 模块死锁问题。
> v7 已编译部署验证通过，trace_reader 可看到 RX #1 ~ #N 连续数据流入，含完整 LoRa 帧头 `AA 55 00 89 00 39`。

### 为什么需要中断

**轮询模式致命缺陷**:
```
LoRa节点发包 (每15s/次, 240B) → 空中速率19.2kbps → 
模块接收完成(AUX=0) → UART2 115200bps 突发吐出数据 →
FreeRTOS vTaskDelay(10ms) 休眠中 → FIFO溢出(16B深度) → 数据丢失 →
模块反复重发 → SRAM缓冲区撑爆 → AUX永久拉低 → 硬件死锁
```

**中断模式优势**:
```
LoRa节点发包 → 模块接收 → AUX=0 → UART2 RX FIFO有数据 →
硬件IRQ触发 → GICv3路由到CPU1 → ISR微秒级响应 → 
从FIFO读取→环形缓冲区 → 任务层2ms轮询hex dump
```

| 对比维度 | 轮询 (v6) | 中断 (v7) |
|----------|-----------|-----------|
| 响应延迟 | ≤10ms (vTaskDelay) | ≤1μs (硬件IRQ) |
| FIFO溢出风险 | 高 (10ms最多115B, FIFO仅16B) | 无 (ISR在FIFO满前读走) |
| LoRa模块死锁 | 曾发生 | 未复现 |
| CPU开销 | 低 (大部分时间休眠) | 中 (ISR频繁触发) |
| 代码复杂度 | 简单 | 中等 (GIC+ISR+环形缓冲) |

### 架构总览

```
┌──────────────────────────────────────────────────────────────────┐
│  FreeRTOS (CPU1, 0xb0100000)                                     │
│                                                                  │
│  ┌──────────────────┐  硬件IRQ  ┌──────────────────────────┐    │
│  │ GICv3 Distributor │◄─────────│ UART2 PL011 (0x2800E000)  │    │
│  │ IRQ 117 → CPU1    │          │ IMSC=0x50 (RXIM|RTIM)     │    │
│  │ prio=0x8          │          │ IFLS=0x00 (1/8 FIFO)     │    │
│  └────────┬─────────┘          └──────────────────────────┘    │
│           │ 触发 ISR                                              │
│           ▼                                                      │
│  ┌────────────────────────────────────────────────────────┐     │
│  │ uart2_lora_isr (ISR, 生产者)                             │     │
│  │   while(!RXFE) { b = UART_DR; rp_put(b); }              │     │
│  │   ECR=0xFF 清除错误中断                                   │     │
│  │   ICR=mis  写1清中断标志                                  │     │
│  └──────────────────┬─────────────────────────────────────┘     │
│                     │ 写入                                                       │
│                     ▼                                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ 环形缓冲区 rb[4096]                                        │    │
│  │   rh (ISR写, volatile)  ←→  rt (task读, volatile)         │    │
│  │   单核无锁: rh仅ISR写, rt仅task写, 32bit aligned           │    │
│  └──────────────────┬──────────────────────────────────────┘    │
│                     │ rp_avail() / rp_get()                      │
│                     ▼                                            │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ lora_task (消费者, prio=3, 2ms轮询)                        │   │
│  │   avail = rp_avail();                                     │   │
│  │   while (avail--) { rp_get(&b); hex dump; }               │   │
│  │   → spf → puts → 共享内存 0xC8000000                       │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### GICv3 中断配置（main.c 中集中完成）

```c
#include "finterrupt.h"   /* InterruptInstall / InterruptUmask 等 */
#include "fcpu_info.h"    /* GetCpuId() */

/* FUART2_IRQ_NUM = 117 (来自 pe220x/fparameters_comm.h: 87+30) */

/* 第1步: 注册中断服务函数 */
InterruptInstall(FUART2_IRQ_NUM, uart2_lora_isr, NULL, "UART2-LoRa");

/* 第2步: 设置优先级 (0x8 = 中等, 范围 0x0~0xF0) */
InterruptSetPriority(FUART2_IRQ_NUM, IRQ_PRIORITY_VALUE_8);

/* 第3步: 指定中断目标CPU (必须! 否则默认路由到CPU0=Linux → 中断风暴) */
{
    u32 cpu_id;
    GetCpuId(&cpu_id);                              /* 获取当前CPU编号 */
    spf("UART2 IRQ%u → CPU%u\n", FUART2_IRQ_NUM, cpu_id);
    InterruptSetTargetCpus(FUART2_IRQ_NUM, cpu_id); /* 路由到FreeRTOS核心 */
}

/* 第4步: 解除GIC中断掩码 (使能中断在GIC侧的开关) */
InterruptUmask(FUART2_IRQ_NUM);

/* 注意: UART2 硬件中断使能 (IMSC) 延迟到 lora_task!
 * 原因: AT命令阶段需要轮询读取响应, 过早使能中断会导致 ISR
 *       抢先读取 AT 响应数据, 引发 AT 命令解析失败。 */
```

### UART2 PL011 中断寄存器

```c
/* PL011 中断相关寄存器偏移 */
#define UART_IMSC  0x38   /* Interrupt Mask Set/Clear — 中断使能 */
#define UART_RIS   0x3C   /* Raw Interrupt Status — 原始中断状态 */
#define UART_MIS   0x40   /* Masked Interrupt Status — 屏蔽后中断状态 */
#define UART_ICR   0x44   /* Interrupt Clear Register — 写1清除中断 */

/* IMSC 位定义 */
#define UART_IMSC_RXIM  0x10   /* RX Interrupt Mask — 接收中断 */
#define UART_IMSC_RTIM  0x40   /* RX Timeout Mask — 接收超时中断 */

/* MIS 位定义 (用于ISR中判断中断类型) */
#define UART_MIS_RXMIS  0x10   /* 接收中断已触发 */
#define UART_MIS_RTMIS  0x40   /* 接收超时中断已触发 */
#define UART_MIS_OEMIS  0x400  /* 溢出错误 */
#define UART_MIS_BEMIS  0x200  /* 断线错误 */
#define UART_MIS_PEMIS  0x100  /* 奇偶校验错误 */
#define UART_MIS_FEMIS  0x80   /* 帧错误 */

/* IMSC 使能 (在 lora_task 中 AT 命令完成后调用) */
*(volatile u32 *)(U2_BASE + 0x38) = 0x10 | 0x40;  /* RXIM | RTIM */
```

### ISR 实现细节

```c
static void uart2_lora_isr(s32 vector, void *param)
{
    (void)vector;
    (void)param;

    u32 mis = *(volatile u32 *)(U2_BASE + 0x40);  /* 读 MIS 寄存器 */

    /* RXMIS=0x10: FIFO达到触发阈值 (IFLS=0x00 → 1/8满 = 2字节) */
    if (mis & 0x10) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) { /* !RXFE */
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);  /* 写入环形缓冲区 */
        }
    }

    /* RTMIS=0x40: FIFO有数据但不满, 字符间隔超时 (32 bit period) */
    if (mis & 0x40) {
        while (!(*(volatile u32 *)(U2_BASE + 0x18) & 0x10)) {
            u8 b = (u8)(*(volatile u32 *)(U2_BASE + 0x00) & 0xFF);
            rp_put(b);
        }
    }

    /* 错误中断: OE/BE/PE/FE — 必须清除, 否则 PL011 锁死不再触发新中断 */
    if (mis & 0x780) {
        *(volatile u32 *)(U2_BASE + 0x04) = 0xFF;  /* ECR 写任意值清除 */
    }

    /* 写1清除所有已触发的中断标志 (ICR) */
    *(volatile u32 *)(U2_BASE + 0x44) = mis;
}
```

**关键设计决策**:
- **为什么用 volatile 直接寄存器访问而不是 SDK FPl011 API？**
  SDK 的 FPl011 驱动依赖完整的设备实例化和中断管理框架，在裸 core AMP 环境下初始化链路太长。直接寄存器操作代码更短、依赖更少、调试更直观。
- **为什么错误中断必须清除？**
  PL011 在溢出/帧错误后会锁死，不再触发新的 RX 中断。必须写 ECR 清除错误状态。
- **为什么 RXMIS 和 RTMIS 都处理？**
  RXMIS 在 FIFO 达到阈值时触发（保证吞吐），RTMIS 在数据流结束时触发（保证最后几个字节不丢失）。

### 环形缓冲区线程安全分析

```c
#define RB_SZ  4096
static u8           rb[RB_SZ];
static volatile u32 rh = 0;  /* 头指针 — 仅 ISR 写 (生产者) */
static volatile u32 rt = 0;  /* 尾指针 — 仅 task 写 (消费者) */

/* 写入 (ISR中调用) */
static void rp_put(u8 b)
{
    u32 n = (rh + 1) % RB_SZ;
    if (n != rt) { rb[rh] = b; rh = n; }
    /* 满则丢弃: 生产者不阻塞, 保证ISR快速返回 */
}

/* 读出 (task中调用) */
static int rp_get(u8 *b)
{
    if (rt == rh) return -1;  /* 空 */
    *b = rb[rt];
    rt = (rt + 1) % RB_SZ;
    return 0;
}

/* 可读字节数 */
static u32 rp_avail(void)
{
    return (rh - rt + RB_SZ) % RB_SZ;
}
```

**为什么无锁安全（单核）**:
- rh 仅 ISR 写，rt 仅 task 写 — 不存在 write-write 竞争
- 32-bit word write 在 ARMv8 上是原子的 — 不存在 torn write
- 单核环境下 ISR 与 task 不会同时执行 — ISR 会抢占 task，但 task 不会抢占 ISR

**容量设计**:
- 115200bps → 11.5 bytes/ms
- 2ms 轮询间隔 → 23 bytes/间隔
- 环形缓冲区 4096B → 可缓冲 ~178 次轮询 (~356ms) 的数据
- **裕量充足**: 即使 task 短暂卡顿也不会丢数据

### 中断使能时序（关键设计）

```
main()                    lora_task
  │                         │
  ├─ GIC配置完成             │
  │  (Install+Priority+     │
  │   TargetCpus+Umask)     │
  │                         │
  ├─ xTaskCreate(lora_task) │
  │                         ├─ MD0=HIGH (进入AT模式)
  │                         ├─ AT_SEND("AT")        ← 轮询读UART响应
  │                         ├─ AT_SEND("AT+ADDR?")  ← 轮询读UART响应
  │                         ├─ ... 8条AT命令 ...     ← 轮询读UART响应
  │                         │
  │                         ├─ MD0=LOW (进入透传模式)
  │                         │
  │                         ├─ *(U2_BASE+0x38)=0x50 ← ★ 此时才开UART2硬件中断!
  │                         │
  │                         └─ for(;;) 从环形缓冲区读数据
```

**为什么不在 main() 中直接开 IMSC？**

如果 main() 中同时完成 GIC 配置和 IMSC 使能，当 lora_task 使用 `AT_SEND` 宏（轮询 UART FIFO）发送 AT 命令时，ISR 会被触发并抢先读取模块返回的 `OK\r\n` 响应到环形缓冲区。lora_task 的轮询循环读不到任何数据，误判 AT 命令超时/失败，导致配置错误。

**解决方案**: GIC 中断在 main() 中配置（使能中断路由），但 UART2 硬件中断（IMSC）延迟到 lora_task 中 AT 命令全部完成、模块切换到透传模式后再开启。

### 中断接收循环（lora_task 第4步）

```c
u32 loop_cnt = 0;
u32 idle_cnt = 0;
puts("L7-IRQ\r\n");
for (;;) {
    u32 avail = rp_avail();

    if (avail > 0) {
        /* 有新数据 */
        if (idle_cnt > 500)   /* ~1s 空闲后重新有数据 */
            spf("\r\n!! NEW after %ums idle\r\n", idle_cnt * 2);
        idle_cnt = 0;

        u8 buf[64];
        u32 n = 0;
        while (n < 64 && rp_get(&buf[n]) == 0) n++;

        pkt++;
        spf("\r\nRX #%u %uB: ", pkt, n);
        for (u32 j = 0; j < n; j++) {
            /* hex dump */
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
    SHM_HB++;
    if ((loop_cnt % 1500) == 0) {  /* ~3s (1500×2ms) */
        u32 aux = (GEX(FGPIO2_BASE_ADDR) >> AUX_PIN) & 1U;
        spf("[RX-hb] loop=%u AUX=%u rp_av=%u idle=%ums\r\n",
            loop_cnt, aux, rp_avail(), idle_cnt * 2);
    }

    vTaskDelay(pdMS_TO_TICKS(2));  /* 2ms 轮询间隔 (v6轮询模式是10ms) */
}
```

**2ms 间隔设计依据**: ISR 保证 FIFO 不溢出，task 只需及时消费环形缓冲区。2ms 足够将 ~23 字节从 ring buffer 转存到共享内存，且不会高频抢占 CPU。

### ISR 与 AT_SEND 宏冲突处理

**冲突场景** (v4/v5 曾经遇到过):

```
AT_SEND("AT+ADDR=00,0B")
  │
  ├─ 发送 "AT+ADDR=00,0B\r\n" 到 UART2 TX
  ├─ vTaskDelay(200ms) 等模块回复
  ├─ 轮询 UART2 RX FIFO 读取响应  ← 如果此时 IMSC 已使能...
  │                                    ISR 抢先读走了 OK\r\n!
  └─ 轮询循环读不到数据 → 打印空白/乱码 → AT命令"失败"
```

**v7 解决方案**: AT_SEND 宏保持轮询方式不变（发送 + 延时 + 轮询读 RX FIFO），但在 lora_task 流程中，**IMSC 使能放在所有 AT_SEND 调用完成之后**。这样 AT 命令阶段不会有 ISR 干扰。

### 关键陷阱与解决方案

| 陷阱 | 现象 | 根因 | 解决 |
|------|------|------|------|
| GIC中断路由到CPU0 | Linux中断风暴, MMC超时, RCU Stall | `InterruptSetTargetCpus` 缺失, SPI默认路由到CPU0 | `InterruptSetTargetCpus(FUART2_IRQ_NUM, cpu_id)` 显式指定 |
| ISR与AT_SEND冲突 | AT命令解析失败, 无OK响应 | ISR抢先读走AT响应数据 | IMSC延迟到AT完成后使能 |
| 错误中断未清除 | PL011不再触发新中断 | OE/BE/PE/FE 锁死UART中断状态 | ISR中 `ECR=0xFF` 清除错误, `ICR=mis` 清除标志 |
| FIFO溢出丢数据 | 数据帧中缺失字节 | 115200bps突发+task来不及消费 | ISR在FIFO达到阈值时立即读走, 环形缓冲4096B |
| AUX长期为0 | 模块忙不释放 | 节点持续快速发包导致模块SRAM满载 | 中断模式避免FIFO溢出回压模块, AUX正常翻转 |

### v7 验证结果 (2026-05-21)

```
✅ trace_reader 输出:
   === FreRTOS LoRa v7 IRQ (2026-05-21) ===
   === Step4: UART2 GICv3 interrupt + ring buffer ===
   UART2 IRQ117 → CPU1
   D6-IRQ
   L1 v7-IRQ
   AT: OK ... (全部10条AT命令成功)
   LoRa: AT config done
   L4, A 0->1, A 1->0  (进入透传)
   L6-IRQ, L7-IRQ        (中断使能 + 接收循环)
   RX #1 ~ #64+ 连续数据流入
   [RX-hb] loop=3000 AUX=0  (心跳正常)

✅ 数据帧头: AA 55 00 89 00 39  (标准LoRa帧格式)
✅ AUX电平正常翻转: 0→1→0   (模块忙/空闲切换)
✅ 无中断风暴, 无RCU Stall
✅ 无模块硬件死锁复现
```

---

## 版本变更历史

### v7 (2026-05-21) — 中断模式 (Step4)
- **变更**: 从纯轮询迁移到 GICv3 硬件中断接收
- **新增**: `uart2_lora_isr` ISR, GICv3配置(Install/Priority/TargetCpus/Umask)
- **新增**: 环形缓冲区 `rb[4096]` + ISR→task 无锁生产者消费者模型
- **修改**: IMSC使能延迟到 lora_task AT命令后 (避免ISR与AT_SEND冲突)
- **修改**: lora_task 轮询间隔从 10ms 缩短到 2ms
- **修改**: 移除 `lora_task` 中的直接 UART FIFO 轮询, 改为从环形缓冲区读取
- **文件**: main.c (v7 IRQ), DEBUG_GUIDE.md (添加中断实现章节)

### v6 (2026-05-21) — 缓存根因修复 + 轮询稳定
- 共享内存从 MT_NORMAL 改为 MT_DEVICE_NGNRNE
- 添加 SHM_HB 心跳计数器
- 轮询模式达到稳定可生产状态

### v4 (2026-05-20) — Data Abort 崩溃修复

**现象**: FreeRTOS 启动瞬间触发 `ec=0x25 Data Abort, far=0xC8000000`

**根因**: `init_system()` → `puts()` 尝试写共享内存 `0xC8000000` 时，MMU 尚未映射该地址。`FMmuMap()` 调用晚于第一个 `puts()`。

**修复**: 调整 `main()` 初始化顺序为：
```
init_system() → FMmuMap(全部外设+共享内存) → SHM清零 → puts() → RPMsg → 外设配置 → 任务创建
```
FMmuMap 必须在任何 puts/spf 之前完成。

### v5 (2026-05-20) — 回退轮询 + 心跳诊断

**变更**: 禁用中断模式（`#if 0`），回退到纯轮询接收，添加心跳计数器 `SHM_HB`。

**心跳计数器**:
```c
#define SHM_HB (SHM_BASE[2])  /* offset 8, devmem 0xC8000008 */
```
三个任务各自在循环中 `SHM_HB++`，Linux 可通过 `busybox devmem 0xC8000008 32` 秒级验证 FreeRTOS 是否存活。

**诊断结论**: HB=0 持续不变，但 FreeRTOS 实际在运行（心跳已写入，只是被 cache 藏起来了）。

### v6 (2026-05-21) — **缓存根因修复（当前版本）**

**现象**: `trace_reader` 和 `devmem 0xC8000000` 显示 WI 不变，但中间节点确实在发送数据，FreeRTOS 也在运行。

**根因**: 共享内存映射为 `MT_NORMAL`（可缓存），CPU 写入的数据滞留在 L1/L2 Cache 内，未落盘到 DDR。Linux 通过 `/dev/mem` 直接读 DDR，看到的是旧值。

```
MT_NORMAL (可缓存):    CPU写 → L1/L2 Cache → (延迟) → DDR
                      Linux devmem 读 ↑ 只能读到DDR旧值

MT_DEVICE_NGNRNE (非缓存): CPU写 → 直通 DDR
                          Linux devmem 读 ↑ 实时可见
```

**修复**:
```c
// 之前（有 Bug）
FMmuMap(0xC8000000UL, 0xC8000000UL, 0x100000UL,
        MT_NORMAL | MT_P_RW_U_RW | MT_NS);

// 之后（当前）
FMmuMap(0xC8000000UL, 0xC8000000UL, 0x100000UL,
        MT_DEVICE_NGNRNE | MT_P_RW_U_RW | MT_NS);   /* 非缓存 — 每写必须到DDR */
```

**验证方法**:
```bash
# 5秒观察心跳是否递增（证明 FreeRTOS 存活 + cache 可见）
for i in 0 1 2 3 4 5; do
  HB=$(sudo busybox devmem 0xC8000008 32)
  WI=$(sudo busybox devmem 0xC8000000 32)
  echo "T+${i}s: HB=$HB WI=$WI"
  sleep 1
done

# 正常输出示例（HB 每 100ms 递增一次）：
# T+0s: HB=0x000002C2 WI=0x00000513
# T+1s: HB=0x00000331 WI=0x00000533
# T+2s: HB=0x000003A0 WI=0x00000533
# ...
```

---

## deploy.sh 快速部署脚本

位置: `/home/alientek/Phytium/freertos/deploy.sh`

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

**脚本流程 (v10)**:
1. `make all -j$(nproc)` — 编译 FreeRTOS 固件
2. `scp .elf → /tmp/` — 传输到开发板
3. 检查 `/sys/class/remoteproc/remoteproc0/state`
4. `cp → /lib/firmware/openamp_core0.elf` — 更新固件文件
5. **若 offline**: `echo start > state` (安全, 直接启动)
6. **若 running**: 仅更新文件, **不执行 stop/start** (避免 RCU stall)
7. `trace_reader` 45秒验证数据解析

**⚠️ 关键安全规则**:
- **绝不** 在固件 running 时执行 `echo stop`, 会触发 OP-TEE 重初始化远程核 → RCU stall → 系统卡死
- **不再隔离任何 CPU 核** (`echo 0 > cpuN/online`)
- 板子 reboot 后固件自动 offline, 可直接 safe-start

---

## 设备树 `remote-processor=3` 与物理 CPU 的映射关系 (2026-05-21)

### 实测结论：FreeRTOS 运行在 CPU1 (big核)

通过 **三重证据** 确认：
1. **MPIDR_EL1 寄存器** = `0x80000100` → 硬件亲和性 Aff1=0x1, Aff0=0x00 → **CPU1 big核**
2. **`GetCpuId()` 函数** 返回 `1` (SDK源码: `standalone/soc/common/fcpu_info.c`)
3. **FreeRTOS 启动日志**: `cpu1: PHYTIUM_RPROC_MAIN:current 1`

### 设备树为什么写 `remote-processor = <3>`？

开发板实际设备树 (`/proc/device-tree/homo_rproc@0/homo_core0@b0100000/`):
```
compatible = "homo,rproc-core";
remote-processor = <3>;          ← 逻辑 CPU ID
inter-processor-interrupt = <9>; ← IPI 用 SGI #9
firmware-name = "openamp_core0.elf";
memory-region = <...>;
```

**映射关系**: `remote-processor = 3` 是给 Linux `homo_remoteproc` 驱动看的 **PSCI 逻辑编号**，不是 Linux 的 CPU 编号 `cpu3`。

E2000(PE2204) 芯片的 4 核架构:
| PSCI 逻辑ID | 硬件亲和性 (MPIDR) | 类型 | Linux cpuN |
|:--:|:--:|:--:|:--:|
| 0 | 0x000 (CORE0_AFF) | big 核 | cpu0 |
| 1 | 0x100 (CORE1_AFF) | big 核 | cpu1 |
| 2 | 0x200 (CORE2_AFF) | LITTLE 核 | cpu2 |
| 3 | 0x201 (CORE3_AFF) | LITTLE 核 | cpu3 |

**PSCI 映射差异**: `remote-processor = <3>` 理论上对应 PSCI ID 3 → 硬件亲和性 0x201 (LITTLE核/cpu3)，但底层 **PSCI/TF-A 固件** 将其映射到了 big 核 (CPU1, 亲和性 0x100)。这是飞腾 BSP 的默认行为，可能是为了给 RTOS 分配更高性能的 big 核。

### 为什么不应该修改设备树？

1. **设备树是飞腾 BSP 的一部分**，存储在启动分区的 FIT Image 中，修改需要解包→修改→重编译→回写，有变砖风险
2. **不影响功能** — FreeRTOS 能正常启动、通信、解析数据，说明 PSCI 映射是正确的
3. **飞腾官方 OpenAMP 指南** (CSDN文章 `lizongjun126com/article/details/138340633`) 明确使用 `remote-processor = <3>` 作为参考配置
4. **真正影响通信的是 `inter-processor-interrupt = <9>` 和共享内存地址**，这两个都已正确配置

### 正确的 deploy.sh 行为（修复后）

**不再隔离任何 CPU 核**。之前的 `echo 0 > /sys/devices/system/cpu/cpu3/online` 是错误操作，会导致：
- CPU3 (LITTLE核) 被 hot-unplug → OP-TEE 重新初始化 CPU3 → RCU stall → 系统卡死

修复后的 deploy.sh 直接使用 remoteproc 状态文件启动：
```bash
echo start > /sys/class/remoteproc/remoteproc0/state   # 仅在 offline 时执行
```

---

## 完整调试教训汇总

| 陷阱 | 现象 | 根因 | 修复 | 版本 |
|------|------|------|------|------|
| Data Abort ec=0x25 | FreeRTOS 无法启动，far=0xC8000000 | `FMmuMap()` 晚于 `puts()`，共享内存未映射 | 将 FMmuMap 移至 main() 最顶部 | v4 |
| trace_reader 无新数据 | WI 不变，但节点在发包 | 共享内存为 MT_NORMAL，写入被 CPU Cache 拦截 | 改为 MT_DEVICE_NGNRNE 非缓存 | v6 |
| 心跳 HB=0 | devmem 0xC8000008 始终为 0 | 同上，Cache 问题 | 同上 | v5→v6 |
| CPU 隔离错误 | Linux RCU Stall | 隔离了 CPU1 但 FreeRTOS 在 CPU3 | **已修正**: 不再隔离任何CPU核 | v4→v10 |
| task 优先级 | 低优先任务饿死 | RPM task prio=1 > LoRa prio=3 | 暂无影响（心跳确认调度正常） | - |
| AT_SEND 与 ISR 冲突 | AT 命令解析失败 | ISR 抢先读取 UART 响应 | 回退到轮询模式，AT 阶段不用中断 | v5 |
| f_printk 不可靠 | WI=0，无输出 | ftrace_init 时序不确定，vsnprintf 行为异常 | 裸写 volatile 共享内存，零库依赖 | Step1 |
| 双ISR冲突 | 数据接收交叉/丢失 | main.c 和 lora_uart.c 各注册中断处理 | 删除 lora_uart.c 中断注册，仅保留 main.c | v8 |
| 帧缓冲区死锁 | 连续 rx_hb 无帧输出 | 1024B缓冲区装满后不解析 | 扩大缓冲区+满时强制解析+帧头偏移 | v8 |
| 帧尾偏移错误 | `FX: CRC fail` 全帧校验失败 | 帧尾计算少1字节偏移 | 对齐GD32: `tail_pos = pos + 5 + data_len` | v9 |
| CRC校验误判 | 所有帧标记CRC失败 | GD32帧格式不含独立CRC字段 | 移除CRC校验，仅保留AA55帧头尾标记 | v9 |
| enc_len计算错误 | 帧数据截断 | 按1784字节截断 | 改为 `data_len - 9` (GD32: TS+TYPE+SYNC=9字节) | v9 |
| deploy.sh RCU stall | 系统卡死，`PANIC at PC` | `echo stop`触发OP-TEE重新初始化远程核 | 条件启动：仅在offline时start，running时跳过 | v10 |

---

## 版本变更历史 (v8-v10)

### v8 (2026-05-21) — 双ISR冲突修复 + 帧缓冲区死锁修复

**现象**: `rp_av=0` 持续，数据流入ring buffer但无帧输出

**根因1 — 双ISR冲突**:
`main.c` (`uart2_lora_isr`) 和 `lora_uart.c` (`lora_uart_isr`) 各自注册了 UART2 中断处理函数。
两个ISR同时响应同一个中断, 数据被分裂读取, 导致帧不完整。

**修复1**: 删除 `lora_uart.c` 中的中断注册代码, 仅保留 `main.c` 中的 `uart2_lora_isr`

**根因2 — 帧缓冲区死锁**:
`lora_frame_buf[1024]` 装满后, `process_lora_frame()` 找不到完整帧头 → 不清空缓冲区 → 新数据进不来

**修复2**: 
- 缓冲区扩大至 2048 字节
- 添加满时强制解析: `force_search=1`, 从pos=0扫描完整缓冲区
- 帧头扫描逻辑: 从 `AA 55` 开始逐字节搜索帧起始
- 数据前移: 解析完毕后将残余数据移到 buffer 开头

### v9 (2026-05-21) — 帧格式解析对齐 GD32 主控器

**现象**: 帧能收到但 `FX: CRC fail` 全帧校验失败

**根因**: 帧格式理解错误 — 飞腾FreeRTOS侧按"含独立CRC字段"解析, 但实际GD32帧格式不含CRC

**GD32主控器帧格式** (来自 `/home/alientek/Phytium/GD32L233C_Prj_Master_v3/`):
```
AA 55 LEN [TS_4B] [TYPE_1B] [SYNC_4B] [ENC_NB] 55 AA
|帧头| |长度| |时间戳  | |类型   | |同步字  | |加密数据| |帧尾|
```

**修复**: 完全对齐 GD32 `master_recv.c` 的实现:
```c
// 帧尾计算 (GD32: tail_pos = i + 5 + frame_data_len)
u32 tail_pos = pos + 5 + data_len;

// 帧头尾标记 (AA55 / 55AA), 无CRC
if (buf[tail_pos] != 0x55 || buf[tail_pos + 1] != 0xAA) return pos + 2;

// 数据指针 (GD32: frame = &buf[i+4])
const u8 *data = &buf[pos + 4];  // 跳过 AA 55 LEN

// 时间戳 (4字节大端)
u32 ts = ((u32)data[0] << 24) | ((u32)data[1] << 16)
       | ((u32)data[2] << 8)  |  data[3];

// 类型 (1字节)
u8 rx_type = data[4];

// 同步字 (4字节大端)
u32 sync = ((u32)data[5] << 24) | ((u32)data[6] << 16)
         | ((u32)data[7] << 8)  | data[8];

// 加密数据长度 (GD32: frame_data_len - 9, 因为 TS+TYPE+SYNC=9)
u16 enc_len = data_len - 9;
const u8 *enc_start = &data[9];
```

### v10 (2026-05-21) — deploy.sh RCU stall 修复 + CPU确认

**现象**: 重新部署固件后系统卡死:
```
I/TC: Secondary CPU 3 initializing
I/TC: Secondary CPU 3 switching to normal world boot
PANIC at PC : 0x00000000ffa0309c
rcu: INFO: rcu_preempt detected stalls on CPUs/tasks: 3-...0
```

**根因**: `echo stop > remoteproc0/state` 触发 OP-TEE 重新初始化远程核 (CPU3/LITTLE),
然后 `echo start` 又触发 CPU_ON, 导致 CPU3 的 RCU 状态异常 → 系统卡死

**修复** (`deploy.sh`):
```bash
# 检查当前remoteproc状态
STATE=$(cat /sys/class/remoteproc/remoteproc0/state)

if [ "$STATE" = "offline" ] || [ "$STATE" = "unknown" ]; then
    # 固件不在运行, 直接启动 (安全)
    echo "openamp_core0.elf" > /sys/class/remoteproc/remoteproc0/firmware
    echo "start" > /sys/class/remoteproc/remoteproc0/state
else
    # 固件已在运行, 仅更新文件, 等待下次reboot生效
    echo "固件已在运行, 跳过重启 (避免触发 RCU stall)"
fi
```

**CPU 确认**: 通过 `GetCpuId()` + `MPIDR_EL1` 确定 FreeRTOS 运行在 **CPU1 (big核, 亲和性0x100)**

---

## 当前运行状态 (v10 Final, 2026-05-21)

```
✅ FreeRTOS 启动            === FreRTOS LoRa v10 ===
✅ 共享内存非缓存           MT_DEVICE_NGNRNE
✅ RPMsg 通道建立           RPMsg done
✅ GICv3 中断配置           UART2 IRQ117 → CPU1 (big核)
✅ 帧解析对齐GD32格式       无CRC, AA55/55AA帧头尾标记
✅ enc_len计算              data_len - 9 (TS+TYPE+SYNC)
✅ LoRa AT配置完成          全部OK (10条AT命令)
✅ LoRa 透传模式            MD0=LOW, AUX就绪
✅ ISR → 环形缓冲区 → 帧缓冲区 → 任务消费
✅ deploy.sh 安全部署        条件启动, 无RCU stall
✅ 心跳计数器               每2ms递增
✅ trace_reader 实时可见    无延迟
```

**数据接收示例** (v10 干净固件):
```
=== FRAME #1: type=01 ts=6263423 sync=BC8AC1E1 enc=16B dec=16B ===
=== FRAME #2: type=04 ts=6263423 sync=BC8AC1E1 enc=128B dec=128B ===
=== FRAME #3: type=04 ts=6263423 sync=AB4BF046 enc=128B dec=128B ===
...
[RX-hb] loop=21000 AUX=0 tx=22 isr=184 dat=2944
```

**帧类型说明**:
| type | 含义 | enc大小 | 说明 |
|:--:|------|:--:|------|
| 01 | STATUS帧 | 16B | 节点状态上报 |
| 04 | NODE_RAW帧 | 128B | 节点加密采样数据 |

**LoRa 配置参数**:
| 参数 | 值 |
|------|-----|
| 地址 | 0x000B |
| 信道/速率 | 23 / 19.2kbps (WLRATE=23,5) |
| 包大小 | 240字节 (PACKSIZE=3) |
| 传输模式 | 定点传输 (TMODE=1) |
| 发射功率 | 20dBm (TPOWER=4) |
| UART 波特率 | 115200 8N1 (UART=7,0) |

**共享内存布局**:
```
0xC8000000 + 0:  WI (write_index, u32)
           + 4:  RI (read_index, u32)
           + 8:  HB (heartbeat, u32)
           +12:  环形缓冲区开始 (4096B ISR ring buffer)
```

**中断统计** (v10 典型值):
- ISR 触发频率: ~每 4-5s 一次 (多节点并发发包)
- 每次触发处理: ~320 字节 (多帧拼接)
- 50秒内: 22帧 / 184次ISR / 2944字节数据

---

## 旧版本状态 (v7 历史记录)

### v7 (2026-05-21) — 中断模式
