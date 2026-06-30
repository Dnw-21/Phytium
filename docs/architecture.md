# Phytium PE2204 异构多核系统架构全景

> **更新**: 2026-06-24 | **版本**: v2.1 | **状态**: Task 1 + Task 2 + Dashboard 三条链路并行；当前 FreeRTOS 实际运行在 CPU1，设备树 remote-processor 仍写 CPU3；Task 1 RPMsg 路径已准备但未接线

---

## 1. 系统架构总图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             Phytium PE2204 SoC                              │
│                                                                             │
│  ┌─────────────────────────────────┐   ┌─────────────────────────────────┐  │
│  │         Linux 主核侧             │   │       FreeRTOS 从核侧            │  │
│  │                                 │   │                                 │  │
│  │  CPU0: A55 (LITTLE)  1.5 GHz    │   │  实际运行核心：CPU1 (A55)        │  │
│  │  CPU2: A76 (big)     1.8 GHz    │   │  设备树 remote-processor: CPU3   │  │
│  │  CPU3: 可用性口径不一致*         │   │                                 │  │
│  │                                 │   │  ┌───────────────────────────┐  │  │
│  │  ┌───────────────────────────┐  │   │  │ main()                    │  │  │
│  │  │ task2_linux/              │  │   │  │ ├── 初始化 MMU/OpenAMP    │  │  │
│  │  │ ukf_pipeline_5bus         │  │   │  │ ├── UART2/GPIO/LoRa AT    │  │  │
│  │  │ ukf_pipeline_9bus  ───────┼──┼───┼─→│ └── 创建 9 个任务          │  │  │
│  │  │ ukf_pipeline_39bus_ft     │  │   │  │                           │  │  │
│  │  │ launch_ukf_multi.py       │  │   │  │ rpm_task       (Prio=1)   │  │  │
│  │  │ dashboard_server_v2.py    │  │   │  │ aux_task       (Prio=2)   │  │  │
│  │  └───────────────────────────┘  │   │  │ master_recv_task    (Prio=5) │  │
│  │                                 │   │  │ master_process_task (Prio=4) │  │
│  │  ┌───────────────────────────┐  │   │  │ master_judge_task   (Prio=3) │  │
│  │  │ src/linux-app/            │  │   │  │ master_poll_task    (Prio=2) │  │
│  │  │ rpmsg_recv.c  ←───────────┼──┼───┼─→│ sim_node_task       (Prio=8) │  │
│  │  │ lora_receiver.c           │  │   │  │ sim_node_39bus_task (Prio=6) │  │
│  │  └───────────────────────────┘  │   │  │ sim_node_9bus_task  (Prio=4) │  │
│  │                                 │   │  └───────────────────────────┘  │  │
│  │  ┌───────────────────────────┐  │   │                                 │  │
│  │  │ dashboard_board/          │  │   │  外部接口：UART2 → LoRa 模块    │  │
│  │  │ server/dashboard_server.py│  │   │       GPIO2_10 → AUX/MD0        │  │
│  │  └───────────────────────────┘  │   │                                 │  │
│  └───────────────┬───────────────┘   └───────────────┬─────────────────┘  │
│                  │                                    │                    │
│                  │      RPMsg over VirtIO / GICv3 SGI 9                    │
│                  │      共享内存 0xB0100000 (409MB)                        │
│                  │      SHM 数据区：                                       │
│                  │        0xC8100000 (5bus, 256KB)                         │
│                  │        0xC8140000 (39bus, 512KB)                        │
│                  │        0xC81C0000 (9bus, 128KB)                         │
│                  │                                    │                    │
│  ┌───────────────┴────────────────────────────────────┴───────────────┐   │
│  │                         外部设备                                      │   │
│  │  GD32 终端节点 ←────LoRa无线────→ ATK-MWCC68D 模块 ←──UART2──→ FreeRTOS│   │
│  └────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

\* CPU3 可用性口径不一致：[task2_linux/README.md](../task2_linux/README.md) 称 Task 2 实测中 CPU3 离线；[底层架构与通信摸底报告.md](../底层架构与通信摸底报告.md) 曾在桌面环境下测得 CPU3 可用。需按实际运行场景复核。

---

## 2. 硬件资源分配

### 2.1 CPU 分配

| CPU编号 | 核心类型 | MPIDR | 逻辑CPU | 操作系统 | 当前用途 |
|--------|---------|-------|--------|---------|---------|
| CPU0 | A55 (LITTLE) | 0x200 | cpu0 | Linux SMP | 通用计算 / 5bus + 9bus UKF |
| **CPU1** | **A55 (LITTLE)** | **0x201** | **cpu1** | **FreeRTOS** | **FreeRTOS 实际运行核心** |
| CPU2 | A76 (big) | 0x000 | cpu2 | Linux SMP | 通用计算 / 39bus UKF |
| CPU3 | A76 (big) | 0x100 | cpu3 | 口径不一致 | Task2 实测离线；摸底报告桌面环境可用 |

> 当前调试口径：FreeRTOS 实际运行在 CPU1，但设备树/remoteproc 配置里仍写 CPU3。

### 2.2 内存布局

| 地址范围 | 大小 | 用途 |
|---------|------|------|
| 0x80000000 - 0xB0100000 | ~768MB | Linux 可用内存 |
| **0xB0100000 - 0xC9A00000** | **409MB** | **OpenAMP 共享内存 (no-map, reserved)** |
| ├─ 0xB0100000 | - | 从核固件代码入口 |
| ├─ vring0/vring1 | - | RPMsg TX/RX 队列 |
| └─ RPMsg 缓冲区 + 固件数据 | - | OpenAMP 运行时 |
| **0xC8100000** | 256KB | **5bus SHM 数据区** |
| **0xC8140000** | 512KB | **39bus SHM 数据区** |
| **0xC81C0000** | 128KB | **9bus SHM 数据区** |
| 0xC9A00000+ | ~3GB+ | Linux 可用内存 |

### 2.3 外设引脚（LoRa 模块 → FreeRTOS）

| 飞腾派接口 | PE2204 引脚 | LoRa 模块引脚 | 功能 | 连接侧 |
|-----------|------------|--------------|------|--------|
| J1 Pin 8 | UART2_TXD | LoRa RXD | 数据发送 | FreeRTOS |
| J1 Pin 10 | UART2_RXD | LoRa TXD | 数据接收 | FreeRTOS |
| J1 Pin 7 | GPIO2_10 | LoRa AUX/MD0 | 模式控制 | FreeRTOS |

当前 LoRa 串口以 UART2 为准；历史 UART3 记录不再作为当前事实。

---

## 3. 软件架构分层

```
┌─────────────────────────────────────────────────────────────┐
│  Linux 应用层                                                │
│  ├── task2_linux/ukf_pipeline_{5bus,9bus,39bus_ft}         │
│  ├── task2_linux/launch_ukf_multi.py                        │
│  ├── task2_linux/dashboard_server_v2.py                     │
│  ├── dashboard_board/server/dashboard_server.py  ← 推荐     │
│  ├── src/linux-app/rpmsg_recv.c                ← 当前 LoRa  │
│  ├── src/linux-app/lora_receiver.c / aux_check.c（调试工具） │
│  └── src/openamp-demo/linux-master/master_receiver.c        │
├─────────────────────────────────────────────────────────────┤
│  Linux 内核层                                                │
│  ├── rpmsg_char / rpmsg_ctrl                                │
│  ├── virtio_rpmsg_bus                                       │
│  └── homo_remoteproc                                        │
├─────────────────────────────────────────────────────────────┤
│  固件层 (OpenAMP/VirtIO)                                     │
│  ├── 共享内存 vring0/vring1                                 │
│  ├── RPMsg 端点                                             │
│  └── IPI 中断 (GICv3 SGI 9)                                 │
├─────────────────────────────────────────────────────────────┤
│  FreeRTOS 从核任务层                                         │
│  ├── rpm_task            (Prio=1)  RPMsg 通信 + 心跳        │
│  ├── aux_task            (Prio=2)  AUX 引脚监测             │
│  ├── master_recv_task    (Prio=5)  LoRa 帧接收              │
│  ├── master_process_task (Prio=4)  解密/分流/存储           │
│  ├── master_judge_task   (Prio=3)  节点在线/故障判定        │
│  ├── master_poll_task    (Prio=2)  轮询/命令下发            │
│  ├── sim_node_task       (Prio=8)  5bus RK4 仿真            │
│  ├── sim_node_39bus_task (Prio=6)  39bus RK4 仿真            │
│  └── sim_node_9bus_task  (Prio=4)  9bus RK4 仿真            │
├─────────────────────────────────────────────────────────────┤
│  FreeRTOS 内核 + libmetal + OpenAMP 库                      │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. 异构核间通信

### 4.1 通信媒介

| 项目 | 说明 |
|------|------|
| 传输协议 | RPMsg over VirtIO |
| 物理介质 | 共享内存 (0xB0100000, 409MB) |
| 中断通知 | GICv3 SGI 9 |

### 4.2 RPMsg 通道

| 通道名 | 端点/命令 | 方向 | 用途 |
|--------|-----------|------|------|
| `rpmsg-openamp-demo-channel` | `CMD_LORA_RAW 0x0023` | RTOS → Linux | LoRa 原始帧透传 |
| `rpmsg-openamp-demo-channel` | `CMD_HEARTBEAT 0x0030` | RTOS → Linux | 心跳 |
| `rpmsg-openamp-demo-channel` | `CMD_ECHO_REQ/RESP 0x0040/0x0041` | 双向 | 绑定/测试 |
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | Linux ↔ RTOS | 5bus 仿真控制 |
| `rpmsg-sim-9bus` | `0x0070` | Linux ↔ RTOS | 9bus 仿真控制（硬编码） |
| `rpmsg-sim-39bus` | `0x0060` | Linux ↔ RTOS | 39bus 仿真控制（硬编码） |

### 4.3 共享内存数据区

| 节点 | 基地址 | 大小 | 帧大小 | 容量 |
|------|--------|------|--------|------|
| 5bus | 0xC8100000 | 256 KB | 64 B | 4095 帧 |
| 39bus | 0xC8140000 | 512 KB | 400 B | 1310 帧 |
| 9bus | 0xC81C0000 | 128 KB | 104 B | 1260 帧 |

---

## 5. 三条数据链路

### 5.1 Task 1 — LoRa 主控链路

**当前实际路径**（业务数据通过 SHM 调试打印输出）:

```
GD32 终端节点
    → LoRa 无线
    → ATK-MWCC68D 模块
    → UART2 (FreeRTOS)
    → lora_uart.c ISR → 环形缓冲区
    → master_recv_task / master_process_task
    → SHM 调试打印区 (0xC8000000)
    → Linux trace_reader (/dev/mem)
```

**预留 RPMsg 路径**（`rpmsg_send_lora_raw()` 已准备但未接线）:

```
GD32 终端节点
    → LoRa 无线
    → ATK-MWCC68D 模块
    → UART2 (FreeRTOS)
    → master_recv_task / master_process_task
    → (待接入) rpmsg_send_lora_raw()
    → rpm_task
    → RPMsg (CMD_LORA_RAW 0x0023)
    → src/linux-app/rpmsg_recv.c
```

### 5.2 Task 2 — 多节点 UKF 状态估计链路

```
FreeRTOS
    ├── sim_node_task       → SHM 0xC8100000
    ├── sim_node_39bus_task → SHM 0xC8140000
    └── sim_node_9bus_task  → SHM 0xC81C0000
                              ↓
Linux
    ├── ukf_pipeline_5bus   → CPU0
    ├── ukf_pipeline_9bus   → CPU0
    └── ukf_pipeline_39bus_ft → CPU2
```

控制通道：RPMsg `rpmsg-sim-{5bus,9bus,39bus}`

### 5.3 Dashboard 链路

```
VM 端 prep/prepare_data.py
    → dashboard_board/data/dashboard_data.json
    → scp 到飞腾派
    → dashboard_board/server/dashboard_server.py (Flask :5000)
    → 浏览器 / VNC Firefox
```

当前 Dashboard 使用预计算数据，尚未接入 Task 1/Task 2 真实数据。

---

## 6. 核心代码文件索引

| 文件 | 功能 |
|------|------|
| [freertos/main.c](../freertos/main.c) | FreeRTOS 入口，创建全部任务 |
| [freertos/src/master_recv.c](../freertos/src/master_recv.c) | LoRa 帧接收 |
| [freertos/src/master_poll_task.c](../freertos/src/master_poll_task.c) | 主控轮询命令 |
| [freertos/src/master_judge.c](../freertos/src/master_judge.c) | 节点在线判定 |
| [freertos/src/lora_uart.c](../freertos/src/lora_uart.c) | UART2 驱动 |
| [freertos/src/data_frame.c](../freertos/src/data_frame.c) | 帧格式/CRC8 |
| [freertos/src/sim_node_task.c](../freertos/src/sim_node_task.c) | 5bus 仿真 |
| [freertos/src/sim_node_9bus.c](../freertos/src/sim_node_9bus.c) | 9bus 仿真 |
| [freertos/src/sim_node_39bus.c](../freertos/src/sim_node_39bus.c) | 39bus 仿真 |
| [task2_linux/ukf_pipeline_online.c](../task2_linux/ukf_pipeline_online.c) | UKF Pipeline 主程序 |
| [task2_linux/launch_ukf_multi.py](../task2_linux/launch_ukf_multi.py) | 多节点 UKF 启动器 |
| [dashboard_board/server/dashboard_server.py](../dashboard_board/server/dashboard_server.py) | 推荐版 Dashboard |
| [src/linux-app/rpmsg_recv.c](../src/linux-app/rpmsg_recv.c) | Linux 侧 RPMsg LoRa 接收 |

---

**版本**: v2.0 | **更新**: 2026-06-24 | **状态**: 已同步当前 Task 1 + Task 2 + Dashboard 架构
