# OpenAMP 异构多核通信流程详解

> **更新**: 2026-06-24 | **当前架构**: FreeRTOS 实际 CPU1（设备树写 CPU3），9 任务并行，Task 1 LoRa 主控 + Task 2 多节点 UKF

## 1. 通信架构总览

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              Phytium PE2204 SoC                                      │
│                                                                                      │
│   Linux 主核侧 (CPU0/CPU2)              FreeRTOS 从核侧（实际 CPU1）                  │
│   ┌─────────────────────────────┐      ┌───────────────────────────────────────┐    │
│   │  task2_linux/               │      │  main()                               │    │
│   │  ukf_pipeline_5bus/9bus/39  │      │  ├── UART2 / GPIO2_10 / MD0 初始化    │    │
│   │  launch_ukf_multi.py        │      │  ├── OpenAMP / RPMsg / VirtIO 初始化  │    │
│   │  dashboard_server_v2.py     │      │  └── 创建 9 个任务                     │    │
│   └─────────────────────────────┘      │                                       │    │
│   ┌─────────────────────────────┐      │  rpm_task          (Prio=1)           │    │
│   │  src/linux-app/             │      │  aux_task          (Prio=2)           │    │
│   │  rpmsg_recv.c               │      │  master_recv_task  (Prio=5)           │    │
│   │  lora_receiver.c            │      │  master_process_task (Prio=4)         │    │
│   │  aux_check.c / at_test.c    │      │  master_judge_task (Prio=3)           │    │
│   └─────────────────────────────┘      │  master_poll_task  (Prio=2)           │    │
│                                         │  sim_node_task     (Prio=8)  5bus     │    │
│   ┌─────────────────────────────┐      │  sim_node_39bus_task (Prio=6)         │    │
│   │  src/openamp-demo/          │      │  sim_node_9bus_task  (Prio=4) 9bus    │    │
│   │  master_receiver.c          │      └───────────────────────────────────────┘    │
│   │  lora_ctrl.c                │              │                                     │
│   │  rpmsg_test.c / rpmsg_ping  │              │ RPMsg over VirtIO                   │
│   └─────────────────────────────┘              │ 共享内存 0xB0100000 (409MB)         │
│                                                 │ GICv3 SGI 9                         │
│   ┌─────────────────────────────┐              │                                     │
│   │  /dev/rpmsg0                │◄─────────────┤ 通用通道                            │
│   │  /dev/rpmsg-sim-{5,9,39}bus │◄─────────────┤ 仿真控制通道                        │
│   └─────────────────────────────┘              │                                     │
│   ┌─────────────────────────────┐              │ SHM 数据区                          │
│   │  trace_reader (/dev/mem)    │◄─────────────┤ 0xC8000000 调试打印区               │
│   └─────────────────────────────┘              │ 0xC8100000 5bus 数据                │
│                                                 │ 0xC8140000 39bus 数据               │
│                                                 │ 0xC81C0000 9bus 数据                │
│                                                 └─────────────────────────────────────┘
│   外部接口:
│     GD32 终端节点 ←──LoRa无线──→ ATK-MWCC68D 模块 ←──UART2──→ FreeRTOS
│
└─────────────────────────────────────────────────────────────────────────────────────┘
```

## 2. 数据流

### 2.1 Task 1 — LoRa 主控链路（当前实际路径）

```
GD32 终端节点
  → LoRa 无线
  → ATK-MWCC68D 模块
  → UART2 (FreeRTOS)
  → lora_uart.c ISR → 环形缓冲区
  → master_recv_task → frame_parse()
  → master_process_task → chaos_decrypt_packet() / 按类型分流
  → SHM 调试打印 (0xC8000000)
  → Linux trace_reader (通过 /dev/mem)
```

**关键说明**:
- 当前 LoRa 业务数据通过 **SHM 调试打印** 输出到 Linux 终端，由 `trace_reader` 读取。
- `main.c` 中已实现 `rpmsg_send_lora_raw()` 并定义了 `CMD_LORA_RAW 0x0023`，但**尚未被任何任务调用**。
- 因此 `src/linux-app/rpmsg_recv.c` 和 `src/openamp-demo/linux-master/master_receiver.c` 目前主要接收心跳/echo，不接收业务 LoRa 数据。
- 历史 `rpmsg-echo_os.c` 中的 `rpmsg_send_lora_recv_log()` 与 `DEVICE_LORA_DATA` 已废弃，不再接入 `main.c`。

### 2.2 Task 1 — LoRa 主控链路（预留 RPMsg 路径）

```
GD32 终端节点 → LoRa 无线 → ATK-MWCC68D → UART2 → FreeRTOS
  → master_recv_task
  → (待接入) rpmsg_send_lora_raw()
  → RPMsg CMD_LORA_RAW (0x0023)
  → Linux src/linux-app/rpmsg_recv.c
  → printf hex / 进一步解析
```

**接入点**: 在 `master_recv_task` 或 `master_process_task` 中调用 `rpmsg_send_lora_raw()` 即可启用该路径。

### 2.3 Task 2 — 多节点 UKF 状态估计链路

```
FreeRTOS
  ├── sim_node_task       → RK4 + h(x) → SHM 0xC8100000
  ├── sim_node_39bus_task → RK4 + h(x) → SHM 0xC8140000
  └── sim_node_9bus_task  → RK4 + h(x) → SHM 0xC81C0000

Linux
  ├── ukf_pipeline_5bus   ← SHM 0xC8100000 → CPU0
  ├── ukf_pipeline_9bus   ← SHM 0xC81C0000 → CPU0
  └── ukf_pipeline_39bus_ft ← SHM 0xC8140000 → CPU2

控制通道: RPMsg rpmsg-sim-{5,9,39}bus (START/STOP/RESET/SPEED)
```

**注意**: 5bus 仿真任务除 SHM 外，还会通过 RPMsg 批量发送 `SimDataBatch`；39bus/9bus 仿真任务在结束时通过 RPMsg 发送 `CMD_SIM_DONE`。

## 3. RPMsg 协议定义

### 3.1 通用通道 (`rpmsg-openamp-demo-channel`)

所有消息遵循统一的 `RpmsgPkt` 格式:

```
[4B command][2B length][nB data]
```

| 命令 | 值 | 方向 | 说明 |
|------|-----|------|------|
| `CMD_LORA_RAW` | `0x0023` | RTOS → Linux | **LoRa 原始帧透传（已准备，未接线）** |
| `CMD_LORA_PARSED` | `0x0024` | RTOS → Linux | 预留：解析后数据 |
| `CMD_NODE_STATUS` | `0x0025` | RTOS → Linux | 预留：节点在线/故障状态 |
| `CMD_HEARTBEAT` | `0x0030` | RTOS → Linux | 周期性心跳（rpm_task 每 ~10s） |
| `CMD_ECHO_REQ` | `0x0040` | Linux → RTOS | 端点绑定/测试请求 |
| `CMD_ECHO_RESP` | `0x0041` | RTOS → Linux | Echo 响应 |

定义位置:
- FreeRTOS: [freertos/inc/rpmsg_proto.h](../freertos/inc/rpmsg_proto.h)
- Linux: [src/linux-app/rpmsg_recv.c](../src/linux-app/rpmsg_recv.c)

### 3.2 仿真控制通道

| 通道名 | 命令 | 方向 | 说明 |
|--------|------|------|------|
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | Linux → RTOS | 5bus 仿真控制 |
| `rpmsg-sim-9bus` | `0x0070` | Linux → RTOS | 9bus 仿真控制（硬编码，建议统一） |
| `rpmsg-sim-39bus` | `0x0060` | Linux → RTOS | 39bus 仿真控制（硬编码，建议统一） |

控制子命令:
- `0x00` = STOP
- `0x01` = START
- `0x02` = RESET
- `0x03` = SPEED (data 字段带速度值)

定义位置:
- FreeRTOS: [freertos/src/sim_node_task.h](../freertos/src/sim_node_task.h)
- Linux: [task2_linux/start_sim_nodes.c](../task2_linux/start_sim_nodes.c)

### 3.3 LoRa 帧格式

当前 FreeRTOS 侧解析的 LoRa 帧格式（与 GD32 终端一致）:

```
[0xAA][0x55][LEN_H][LEN_L][ DATA(N字节) ][CRC8][0x55][0xAA]
帧总长 = 7 + N
```

定义位置: [freertos/src/data_frame.c](../freertos/src/data_frame.c) / [freertos/inc/data_frame.h](../freertos/inc/data_frame.h)

## 4. 各环节对应文件速查

| 环节 | 文件路径 | 核心函数 |
|------|---------|---------|
| UART2 中断接收 | [freertos/src/lora_uart.c](../freertos/src/lora_uart.c) | `lora_uart_init()`, `lora_uart_isr()`, `lora_uart_read_frame()` |
| LoRa 帧解析 | [freertos/src/data_frame.c](../freertos/src/data_frame.c) | `frame_parse()`, CRC8 校验 |
| 数据接收任务 | [freertos/src/master_recv.c](../freertos/src/master_recv.c) | `master_recv_task()` |
| 数据处理任务 | [freertos/src/master_recv.c](../freertos/src/master_recv.c) | `master_process_task()`, `chaos_decrypt_packet()` |
| 节点状态判定 | [freertos/src/master_judge.c](../freertos/src/master_judge.c) | `master_judge_task()` |
| 轮询/命令下发 | [freertos/src/master_poll_task.c](../freertos/src/master_poll_task.c) | `master_poll_task()` |
| RPMsg 通信任务 | [freertos/main.c](../freertos/main.c) | `rpm_task()`, `rpmsg_endpoint_cb()`, `rpmsg_send_lora_raw()` |
| 5bus 仿真任务 | [freertos/src/sim_node_task.c](../freertos/src/sim_node_task.c) | `sim_node_task()` |
| 9bus 仿真任务 | [freertos/src/sim_node_9bus.c](../freertos/src/sim_node_9bus.c) | `sim_node_9bus_task()` |
| 39bus 仿真任务 | [freertos/src/sim_node_39bus.c](../freertos/src/sim_node_39bus.c) | `sim_node_39bus_task()` |
| Linux LoRa 接收器 | [src/linux-app/rpmsg_recv.c](../src/linux-app/rpmsg_recv.c) | `main()` |
| Linux 仿真控制 | [task2_linux/start_sim_nodes.c](../task2_linux/start_sim_nodes.c) | `main()` |
| Linux UKF Pipeline | [task2_linux/ukf_pipeline_online.c](../task2_linux/ukf_pipeline_online.c) | `main()` |
| 共享内存调试打印读取 | [task2_linux/shm_print_dump.c](../task2_linux/shm_print_dump.c) | `main()` |

## 5. 启动流程

```
Step 1: Linux 加载驱动
  sudo modprobe rpmsg_char rpmsg_ctrl

Step 2: 启动 FreeRTOS 从核
  echo start > /sys/class/remoteproc/remoteproc0/state
  → homo_rproc_start()
    → remove_cpu(3) / 加载固件到 0xB0100000（设备树口径）
    → PSCI CPU_ON → FreeRTOS 实际在 CPU1 执行（设备树仍写 CPU3）

Step 3: FreeRTOS 初始化
  main() [freertos/main.c]
    → init_system()
    → FMmuMap() 映射 SHM / UART2 / GPIO / IOPAD
    → platform_create_proc() / platform_setup_src_table() / platform_setup_share_mems()
    → platform_create_rpmsg_vdev() → g_rpdev
    → IOMUX / GPIO / UART2 初始化
    → lora_uart_init() 启用 UART2 中断接收
    → AT 命令配置 LoRa 模块
    → 创建 9 个 FreeRTOS 任务
    → vTaskStartScheduler()

Step 4: RPMsg 通道建立
  rpm_task → rpmsg_create_ept("rpmsg-openamp-demo-channel") → g_ept
  sim_node_* → 各自创建 rpmsg-sim-*bus endpoint
  virtio_rpmsg_bus 检测到端点 → 创建 /sys/bus/rpmsg/devices/

Step 5: 绑定用户驱动（如需要）
  echo rpmsg_chrdev > .../driver_override
  echo virtio0... > .../bind
  → /dev/rpmsg0 创建

Step 6: Linux 应用连接
  rpmsg_recv.c
    → open("/dev/rpmsg_ctrl0")
    → ioctl(CREATE_EPT, "rpmsg-openamp-demo-channel")
    → open("/dev/rpmsg0")
```

## 6. 停止流程

```bash
# 停止 FreeRTOS 从核
sshpass -p 'user' ssh user@192.168.88.10 \
  "echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'"
```

停止后：
- RPMsg 端点销毁
- SHM 数据区内容保留（除非被覆盖）
- 重新 `echo start` 可再次启动从核

## 7. 异核通信实现位置

| 层 | 位置 | 说明 |
|-----|------|------|
| FreeRTOS 应用层 | [freertos/main.c](../freertos/main.c) | `rpm_task`, `rpmsg_send_lora_raw()`, `rpmsg_endpoint_cb()` |
| FreeRTOS 仿真层 | [freertos/src/sim_node_*.c](../freertos/src/sim_node_task.c) | 仿真端点创建与控制回调 |
| FreeRTOS OpenAMP 库 | SDK 内置 | `rpmsg_create_ept()`, `rpmsg_send()`, `platform_create_proc()` |
| 共享内存初始化 | [freertos/main.c](../freertos/main.c) `resources` + 设备树 | vring 地址、大小定义 |
| Linux 内核驱动 | `drivers/remoteproc/homo_remoteproc.c`（飞腾定制） | `homo_rproc_kick()` 写 GICv3 SGI 寄存器 |
| Linux 应用层 | [src/linux-app/rpmsg_recv.c](../src/linux-app/rpmsg_recv.c) | `/dev/rpmsg0` read/write |
| Linux UKF 层 | [task2_linux/ukf_pipeline_online.c](../task2_linux/ukf_pipeline_online.c) | SHM ring buffer 读取 + UKF 计算 |

## 8. 当前状态与遗留问题

### 8.1 已废弃/遗留代码

| 文件 | 状态 | 说明 |
|------|------|------|
| [freertos/src/rpmsg-echo_os.c](../freertos/src/rpmsg-echo_os.c) | 废弃 | 不再被 `main.c` 调用，存在未定义符号 `master_recv_inject_data()` |
| `DEVICE_LORA_DATA` (0x0023) | 废弃 | 旧命名，当前代码使用 `CMD_LORA_RAW` |
| `DEVICE_LORA_CTRL` (0x0022) | 废弃 | 旧 LoRa RX 开关控制命令，当前 `main.c` 未处理 |
| [src/openamp-demo/linux-master/master_receiver.c](../src/openamp-demo/linux-master/master_receiver.c) | 遗留 | 仍监听 `DEVICE_LORA_DATA` 旧命名，建议改用 `CMD_LORA_RAW` 或统一使用 `rpmsg_recv.c` |

### 8.2 待同步/待完成

1. **Task 1 RPMsg 数据路径未接线**: `rpmsg_send_lora_raw()` 已准备但未被调用，需要决定是在 `master_recv_task` 还是 `master_process_task` 中接入。
2. **9/39bus 控制码统一**: 当前硬编码为 `0x0070` / `0x0060`，建议统一到 `CMD_SIM_CTRL 0x51`。
3. **Linux 侧接收程序统一**: `src/linux-app/rpmsg_recv.c` 为当前推荐，`src/openamp-demo/linux-master/master_receiver.c` 与 `lora_ctrl.c` 使用旧命名。

---

**版本**: v3.0 | **更新**: 2026-06-24 | **状态**: 已同步当前 9 任务架构与 RPMsg 协议
