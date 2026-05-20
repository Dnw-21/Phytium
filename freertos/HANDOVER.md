# FreeRTOS LoRa 接收项目 — 交接文档

> 编写日期: 2026-05-26
> 状态: Step3 完成，等待进入 Step4

---

## 一、最终目标

**将 GD32L233C 节点主控项目的完整业务逻辑，移植到飞腾派 FreeRTOS 侧运行。**

### 源项目（待移植）

| 项目 | 路径 |
|------|------|
| GD32 主控源码 | `/home/alientek/Phytium/GD32L233C_Prj_Master_v3/GD32L233C_Prj_Master/` |
| 目标移植目录 | `/home/alientek/Phytium/freertos/`（已放置部分早期尝试代码） |

### GD32 项目核心模块

```
GD32L233C_Prj_Master/app/
├── main.c / main.h                        # 入口, task_create()
├── task/
│   ├── tasks.h                            # 协议定义: NODE_MAX_COUNT=10, LORA_BUFFER_SIZE=256
│   ├── master_cmd.c                       # 命令下发
│   ├── master_recv.c                      # ★ 核心 — LoRa接收, 解密, 状态机存储
│   ├── master_judge.c                     # 故障判断
│   └── master_sys.c                       # 系统管理
├── chaos_encrypt.c / chaos_encrypt.h      # 混沌加密算法
├── data_frame.c / data_frame.h            # 帧格式定义
├── log.c / log.h                          # 日志系统
└── data_monitor.h                         # 数据监控
```

业务逻辑简述：
- 主控通过 LoRa 无线接收从节点数据
- 支持 3 种数据类型：周期性上传(NodeUploadData_t)、故障上传(FaultUploadHeader_t)、波形数据
- 混沌加密解密
- 接收状态机：IDLE → RECV_NODE_RAW / RECV_FLASH_WAVE
- 帧格式：`AA 55 [len:2] [dev_id:2] [timestamp:2] [type:1] [sync:4] [payload:n]`

---

## 二、移植路线图

移植分两个阶段：

### 阶段一：基础设施（当前进行中）

```
Step1 ✅  共享内存打印（FreeRTOS → Linux 可见）
Step2 ✅  GPIO 控制（MD0/AUX）
Step3 ✅  UART2 轮询接收 LoRa 原始字节
Step4 ⬜  UART2 中断接收（替代轮询）
Step5 ⬜  帧格式解析 + hex dump
```

### 阶段二：业务移植（基础设施完成后）

```
Step6 ⬜  将 GD32 的 data_frame.h / chaos_encrypt 等模块移植到 FreeRTOS
Step7 ⬜  移植 master_recv.c 的接收状态机
Step8 ⬜  移植 master_cmd.c 的命令下发
Step9 ⬜  移植 master_judge.c 的故障判断
Step10 ⬜ 移植 master_sys.c 的系统管理
Step11 ⬜ RPMsg 通道打通：FreeRTOS LoRa 数据 → Linux
Step12 ⬜ 集成测试, 替换 Linux 侧 lora_receiver.c
```

### 数据流全景

```
GD32从节点 ──LoRa无线──> ATK-MWCC68D模块 ──UART2──> FreeRTOS
                                                       │
                                              ┌────────┴────────┐
                                              │  master_recv.c  │
                                              │  chaos_encrypt  │
                                              │  状态机 + 存储   │
                                              └────────┬────────┘
                                                       │ RPMsg
                                                       ▼
                                                     Linux
```

---

## 三、当前进度：Step3 完成

### 3.1 已实现

| 功能 | 状态 | 验证 |
|------|------|------|
| FreeRTOS 启动 + 调度器 | ✅ | `=== Step3 LoRa RX ===` |
| RPMsg 通道建立 | ✅ | `RPMsg done`，dmesg: rpmsg host is online |
| 共享内存打印(裸写 volatile) | ✅ | trace_reader 可读 |
| AUX(GPIO2_10) 输入检测 | ✅ | 接 3.3V 变 HIGH，接 GND 变 LOW |
| MD0(GPIO3_1) 输出控制 | ✅ | HIGH=AT模式，LOW=透传模式 |
| IOPAD 引脚复用 | ✅ | GPIO FUNC6, UART FUNC0 |
| UART2 PL011 115200 8N1 | ✅ | 收发正常 |
| LoRa AT 命令配置 | ✅ | 模块回复 `OK` 已收到 |
| LoRa 透传模式 | ✅ | MD0 LOW + AUX 等待 |
| UART2 轮询接收 | ✅ | 收到 `AT\r\nOK` 等 AT 响应 |

### 3.2 已验证输出

```
=== Step3 LoRa RX ===
RPMsg done
GPIO: AUX=IN
LoRa: AT config start
LoRa: AT config done
LoRa: RX ready (poll)
Start sched
RX #1 32B: 41 54 0D 0A 0D 0A 4F 4B 0D    ← AT..OK (模块的AT回复)
[hb] t=300 A=0                              ← 30s 心跳
```

---

## 四、核心文件速查

### 4.1 当前开发文件

| 文件 | 路径 | 说明 |
|------|------|------|
| **主程序** | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/main.c` | 唯一需改的文件, 547行 |
| 编译配置 | `同上目录/configs/pe2204_aarch64_phytiumpi_openamp_for_linux.config` | 含 `CONFIG_OPENAMP_TRACE_DEBUG=y` |
| 调试指南 | `/home/alientek/Phytium/freertos/DEBUG_GUIDE.md` | 完整调试过程记录 |
| Linux 参考 | `/home/alientek/Phytium/src/linux-app/lora_receiver.c` | Linux 侧已验证的 LoRa 接收代码 |
| GD32 参考 | `/home/alientek/Phytium/GD32L233C_Prj_Master_v3/GD32L233C_Prj_Master/` | 待移植的业务代码 |

### 4.2 归档/早期代码

| 文件 | 说明 |
|------|------|
| `/home/alientek/Phytium/freertos/main.c` | 当前 main.c 的备份 |
| `/home/alientek/Phytium/freertos/src/*.c` | 早期 FreeRTOS 移植尝试, 当前未使用 |
| `/home/alientek/Phytium/freertos/inc/*.h` | 早期头文件, 当前未使用 |

---

## 五、main.c 架构（547 行，12 部分）

```
第 1~61 行     文件头 + #include（每个头文件均有用途注释）
第 63~110 行   第1部分: 共享内存打印 — put/puts/spf 宏
第 113~123 行  第2部分: 引脚定义 — MD0=GPIO3_1, AUX=GPIO2_10
第 126~139 行  第3部分: GPIO 寄存器宏 — GDR/GDD/GEX
第 142~166 行  第4部分: IOPAD 引脚复用 — ipset() 函数
第 169~190 行  第5部分: UART2 PL011 寄存器 — 波特率计算公式
第 193~223 行  第6部分: 接收环形缓冲区 — rp_put/rp_get/rp_avail
第 226~238 行  第7部分: FreeRTOS 任务参数 — 栈大小/优先级
第 241~305 行  第8部分: Resource Table + RemoteProc — OpenAMP 固件描述
第 308~334 行  第9部分: aux_task — AUX 监控（纯业务,18行）
第 337~423 行  第10部分: lora_task — AT配置 + 轮询接收（70行）
第 426~454 行  第11部分: rpm_task — RPMsg 通道轮询
第 457~547 行  第12部分: main() — 集中初始化 + 调度启动
```

**重构后关键特性**：所有硬件初始化（5个 FMmuMap、4个 ipset、GPIO 方向、UART2）集中在 `main()` 完成，任务函数不再自己初始化。

---

## 六、关键技术决策

### 6.1 打印：裸写共享内存（不用 f_printk）

```c
#define SHM_BASE  ((volatile u32 *)0xC8000000UL)
#define SHM_WI    (SHM_BASE[0])     // write_index
#define SHM_RI    (SHM_BASE[1])     // read_index
#define SHM_BUF   ((volatile char *)(SHM_BASE + 3))

// 宏裸写 volatile, 零函数调用
#define put(c) do { u32 w=SHM_WI, r=SHM_RI, n=(w+1)%SHM_BSZ; \
                    if(n!=r){SHM_BUF[w]=(c);SHM_WI=n;} }while(0)
#define puts(s) do { const char *p=(s); while(*p) put(*p++); } while(0)
```

**为什么不直接调库函数？**

`f_printk` → `ftrace_printk` 在多任务环境下不可靠（write_index 始终为 0），因为 SDK trace 初始化时序不确定 + `vsnprintf` 等标准库行为异常。裸写 volatile 是唯一已验证可靠的方式。

**Linux 侧查看**：
```bash
sudo /home/user/trace_reader   # 持续轮询程序
```

### 6.2 MMU 映射铁律

**所有外设基址必须先 FMmuMap 再访问，否则 CPU 取指令异常 → FreeRTOS 崩溃 → write_index=0。**

当前在 `main()` 中一次性映射：
```c
FMmuMap(0xC8000000, ...)  // 共享内存 (MT_NORMAL)
FMmuMap(U2_BASE, ...)     // UART2
FMmuMap(IP_BASE, ...)     // IOPAD (0x32B30000)
FMmuMap(FGPIO2, ...)      // GPIO2 (0x28035000)
FMmuMap(FGPIO3, ...)      // GPIO3 (0x28036000)
```

### 6.3 寄存器直写（暂不用 SDK API）

因链接问题（`FGpioLookupConfig` 等函数未编入库），当前所有 GPIO/UART/IOPAD 操作均用宏直写，与 SDK `fgpio_hw.h` 偏移一致。

---

## 七、硬件端口速查

| 功能 | GPIO | 飞腾派 J1 排针 | IOPAD 偏移 | FUNC | 基址 |
|------|------|---------------|-----------|------|------|
| MD0  | GPIO3_1 | Pin 11 | 0x00E0 (C49) | 6=GPIO | 0x28036000 |
| AUX  | GPIO2_10 | Pin 12 | 0x00C4 (A37) | 6=GPIO | 0x28035000 |
| UART2 TX | — | Pin 8 | 0x00D8 (A47) | 0=UART | 0x2800E000 |
| UART2 RX | — | Pin 10 | 0x00DC (A49) | 0=UART | 0x2800E000 |
| 共享内存 | — | — | — | — | 0xC8000000 |
| IOPAD 基址 | — | — | — | — | 0x32B30000 |

**UART2 PL011 寄存器**：DR=0x00, FR=0x18, IBRD=0x24, FBRD=0x28, LCR_H=0x2C, CR=0x30  
**GPIO 寄存器**：DR=0x00, DDR=0x04, EXT=0x08  
**波特率**：115200 ← 100MHz 时钟, IBRD=54, FBRD=16

---

## 八、编译和部署

```bash
# 编译
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make config_pe2204_phytiumpi_aarch64 && make clean && make all -j$(nproc)

# 部署到开发板
sshpass -p 'user' scp -o StrictHostKeyChecking=no \
  pe2204_aarch64_phytiumpi_openamp_for_linux.elf \
  user@192.168.88.11:/tmp/

sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo 'user' | sudo -S cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf /lib/firmware/openamp_core0.elf"

# 用户手动重启
# sudo reboot
# 等 90-120s 上线后:
# sudo /home/user/trace_reader
```

---

## 九、待做任务

### 阶段一：基础设施（短期）

| # | 任务 | 说明 |
|---|------|------|
| **Step4** | UART2 中断接收 | IRQ=117, 需配置 GIC 路由到 FreeRTOS core, ISR 中往环形缓冲区写 |
| **Step5** | 帧格式解析 | 识别 `AA 55 [len:2] [data] 55 AA`, 剥离 LoRa 定点传输地址头 |

### 阶段二：业务移植（中期）

| # | 任务 | 说明 |
|---|------|------|
| **Step6** | 移植基础模块 | data_frame.h, chaos_encrypt.c/h, log.c/h 从 GD32 移植到 FreeRTOS |
| **Step7** | 移植 master_recv.c | 接收状态机: IDLE → RECV_NODE_RAW → 解密 → 存储 |
| **Step8** | 移植 master_cmd.c | 命令下发逻辑 |
| **Step9** | 移植 master_judge.c | 故障判断 |
| **Step10** | 移植 master_sys.c | 系统管理 |
| **Step11** | RPMsg 打通 | FreeRTOS LoRa 数据 → RPMsg → Linux |
| **Step12** | 集成测试 | 替换 Linux 侧 lora_receiver.c |

### 持续优化（长期）

| 任务 | 说明 |
|------|------|
| 输出互斥 | `spf()` 非原子, 多任务并发写入共享内存时会字符交错 |
| SDK API 化 | 用 `fgpio.h` / `FPl011.h` API 替代直写寄存器 |
| 独立编译 | main.c 从 SDK 目录迁到 `/home/alientek/Phytium/freertos/` |

---

## 十、给接手 AI 的重要规则

1. **不要自己重启开发板** — 用户来重启，你只负责编译和 scp 部署。
2. **不要一直打印** — 只在引脚状态变化、关键事件时打印，30s 心跳即可。
3. **每一步先给计划让用户审阅**，确认后再执行。
4. **不确定的不盲目试错** — 停下来让用户判断方向。
5. **代码先在 SDK 目录开发**，跑通后再迁移到 `/home/alientek/Phytium/freertos/`。
6. **飞腾派连接信息**: IP=`192.168.88.11`, 用户名=`user`, 密码=`user`, sudo密码=`user`。
7. **写共享内存前必须先 `FMmuMap(0xC8000000)`**，直接 volatile 写，别用 `f_printk`。
8. **访问任何外设寄存器前必须先 `FMmuMap` 其基址**，否则 CPU 异常 → 崩溃 → write_index=0。
9. **验证固件是否存活**: `ssh` 到开发板执行 `sudo busybox devmem 0xC8000000 32`，非零 → 存活。
10. **致命错误排查**：write_index=0 第一反应 → 缺 MMU 映射 → 检查最近新访问的基址是否映射。
11. **虚拟机**：虚拟机密码是12345。
12. **文档补充**：/home/alientek/Phytium/docs目录下有之前的飞腾派的调试和整个项目的文档，如果遇到问题可以先去这里查看。还有/home/alientek/Phytium/README.md和/home/alientek/Phytium/PROJECT_INFO.md
13. **规则补充**：12条规则模板，核心要点：
规则
“要点
1.先想后写
明确假设，不确定就问，保持简单
2.简单优先
最小代码解决已提需求，不过度抽象
3.精准修改
只改必要的，不顺手“优化”无关代码
4.目标驱动
先定义成功标准，循环迭代直到验证通过
5.模型只做判断
分类/起草/总结用模型，路由/重试/确定性转换用代码
7.暴露冲突
遇到矛盾选一种并说明原因，不混合
8.先读后写
加代码前先读导出、调用方、共享工具
9.1
测试编码意图
测试要编码“为什么"，不只是“做什么"
10.每步设检查点
总结已完成/已验证/剩余，失去跟踪就停下
11.遵循代码库约定
即使不同意也遵循，有问题提出来
12.大声失败
跳过任何东西就不能说“完成”，默认暴露不确定性
”
