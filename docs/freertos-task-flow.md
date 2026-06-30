# FreeRTOS 从核任务流程详解

> **更新**: 2026-06-24 | **源文件**: [freertos/main.c](../freertos/main.c), [freertos/src/*.c](../freertos/src/), [freertos/inc/*.h](../freertos/inc/) | **当前口径**: FreeRTOS 实际 CPU1（设备树写 CPU3），UART2 真实硬件链路，同时运行 Task 1 LoRa 主控与 Task 2 多节点仿真

---

## 1. 任务总览

FreeRTOS 主控侧实际运行在 **CPU1**（设备树/remoteproc 仍写 CPU3）上，当前创建 **9 个任务**：

```
main() 启动流程:
  ├── 平台初始化
  │   ├── MMU 映射
  │   ├── OpenAMP / RPMsg / VirtIO 初始化
  │   ├── UART2 (PL011) 初始化
  │   ├── GPIO / IOMUX 初始化
  │   └── LoRa 模块 AT 配置
  │
  ├── 创建任务
  │   ├── rpm_task            (Prio=1, 8KB)
  │   ├── aux_task            (Prio=2, 4KB)
  │   ├── master_recv_task    (Prio=5, 512 words)
  │   ├── master_process_task (Prio=4, 2KB)
  │   ├── master_judge_task   (Prio=3, 512 words)
  │   ├── master_poll_task    (Prio=2, 2KB)
  │   ├── sim_node_task       (Prio=8, 16KB)
  │   ├── sim_node_39bus_task (Prio=6, 32KB)
  │   └── sim_node_9bus_task  (Prio=4, 16KB)
  │
  └── vTaskStartScheduler()   启动调度器
```

> 优先级数字越小优先级越高。`rpm_task` 优先级最高，确保 RPMsg 消息及时处理；`sim_node_*` 优先级最低，避免挤压 LoRa 主控实时性。

---

## 2. 任务详情

### 2.1 rpm_task — RPMsg 通信任务

| 属性 | 值 |
|------|-----|
| **优先级** | 1 |
| **栈大小** | 8KB |
| **源文件** | [freertos/main.c](../freertos/main.c) |
| **功能** | OpenAMP/RPMsg 通信核心，管理通用端点 + 三个仿真端点 |

**执行流程**:

```
rpm_task()
  ├── 等待 OpenAMP vring 激活
  ├── rpmsg_create_ept(g_ept, "rpmsg-openamp-demo-channel")
  ├── rpmsg_create_ept(sim_ept,   "rpmsg-sim-5bus")
  ├── rpmsg_create_ept(sim9_ept,  "rpmsg-sim-9bus")
  ├── rpmsg_create_ept(sim39_ept, "rpmsg-sim-39bus")
  └── while(1):
      ├── platform_poll_nonblocking(&rproc)  处理 vring 消息
      ├── 周期性发送 CMD_HEARTBEAT
      └── 必要时发送 CMD_LORA_RAW / CMD_NODE_STATUS
```

**通用端点消息处理** (`rpmsg-openamp-demo-channel`):

| 消息类型 | command值 | 方向 | 说明 |
|----------|-----------|------|------|
| LoRa 原始帧透传 | `CMD_LORA_RAW 0x0023` | RTOS → Linux | **已准备但未接线**：`main.c` 已实现 `rpmsg_send_lora_raw()`，但尚未被业务任务调用 |
| 节点状态 | `CMD_NODE_STATUS 0x0025` | RTOS → Linux | 节点在线/故障状态（预留） |
| 心跳 | `CMD_HEARTBEAT 0x0030` | RTOS → Linux | 周期性心跳（每 ~10s） |
| Echo 请求/响应 | `CMD_ECHO_REQ/RESP 0x0040/0x0041` | 双向 | 端点绑定与测试 |

**仿真端点消息处理**:

| 通道名 | 控制命令 | 用途 |
|--------|----------|------|
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | 5bus 仿真 START/STOP/RESET/SPEED |
| `rpmsg-sim-9bus` | `0x0070` | 9bus 仿真控制（当前硬编码，建议统一到 `CMD_SIM_CTRL`） |
| `rpmsg-sim-39bus` | `0x0060` | 39bus 仿真控制（当前硬编码，建议统一到 `CMD_SIM_CTRL`） |

> 遗留文件 [freertos/src/rpmsg-echo_os.c](../freertos/src/rpmsg-echo_os.c) 当前未被 `main.c` 调用，且引用了未实现的 `master_recv_inject_data()` 等符号。

---

### 2.2 aux_task — AUX 引脚监测任务

| 属性 | 值 |
|------|-----|
| **优先级** | 2 |
| **栈大小** | 4KB |
| **源文件** | [freertos/main.c](../freertos/main.c) |
| **功能** | 每 100ms 读取 GPIO2_10（AUX）电平，变化时打印并递增 SHM 心跳计数器 `SHM_HB` |

---

### 2.3 master_recv_task — LoRa 数据接收任务

| 属性 | 值 |
|------|-----|
| **优先级** | 5 |
| **栈大小** | 512 words |
| **源文件** | [freertos/src/master_recv.c](../freertos/src/master_recv.c) |
| **功能** | 从 UART2 环形缓冲区读取原始字节，解析 LoRa 帧，投递到接收队列 |

**执行流程**:

```
master_recv_task()
  └── while(1):
      ├── 等待 UART 数据稳定
      ├── lora_uart_mark_frame()     标记帧边界
      ├── lora_uart_read_frame()     读取一帧原始字节
      ├── frame_parse()              解析多帧
      │   └── 提取 sync_code / rx_type / enc_len / enc_data
      └── xQueueSend(g_recv_queue, RecvPacket_t)
```

**数据来源**: UART2 中断接收，[freertos/src/lora_uart.c](../freertos/src/lora_uart.c) 管理 4096B 环形缓冲区。

---

### 2.4 master_process_task — 数据解密与处理任务

| 属性 | 值 |
|------|-----|
| **优先级** | 4 |
| **栈大小** | 2KB |
| **源文件** | [freertos/src/master_recv.c](../freertos/src/master_recv.c) |
| **功能** | 从队列取包、解密、按类型分流、保存到模拟 Flash、发送 ACK |

**执行流程**:

```
master_process_task()
  └── while(1):
      ├── xQueueReceive(g_recv_queue, &pkt)
      ├── chaos_decrypt_packet(pkt.enc_data, ...)   // 当前策略与文档存在矛盾
      ├── 按 rx_type 分流:
      │   ├── DATA_TYPE_NODE_HEAD  (0x01)  → 解析节点头
      │   ├── DATA_TYPE_NODE_RAW   (0x04)  → 存储采样数据
      │   └── DATA_TYPE_FAULT_HEAD (0x07)  → 解析故障头
      ├── master_flash_save_node_data()  → 模拟 Flash 存储
      └── 发送 ACK
```

> **注意**: 当前 `OPERATIONS.md` 称 FreeRTOS 侧不做解密（仅透传密文），但 `master_recv.c` 仍调用 `chaos_decrypt_packet()`，策略需统一。

---

### 2.5 master_judge_task — 节点在线判定任务

| 属性 | 值 |
|------|-----|
| **优先级** | 3 |
| **栈大小** | 512 words |
| **源文件** | [freertos/src/master_judge.c](../freertos/src/master_judge.c) |
| **执行周期** | 1000ms |

**执行流程**:

```
master_judge_task()
  └── while(1):
      ├── 遍历 MASTER_MAX_NODES (当前 3)
      │   ├── elapsed > 15000ms → is_online=0
      │   └── severity/fault_type 更新
      └── vTaskDelayUntil(1000ms)
```

---

### 2.6 master_poll_task — 主控轮询任务

| 属性 | 值 |
|------|-----|
| **优先级** | 2 |
| **栈大小** | 2KB |
| **源文件** | [freertos/src/master_poll_task.c](../freertos/src/master_poll_task.c) |
| **功能** | 周期性发送 `CMD_POLL_STATUS`，故障时请求 `CMD_REQUEST_FAULT_DATA` |

**执行流程**:

```
master_poll_task()
  └── while(1):
      ├── 向节点 0 发送 CMD_POLL_STATUS (约 5s 周期)
      ├── 若 fault_pending 置位 → 发送 CMD_REQUEST_FAULT_DATA
      ├── wait_download_done() 等待下载完成
      └── vTaskDelayUntil()
```

> 该任务替代了旧版 `master_cmd_task`（已不存在 `master_cmd.c`）。

---

### 2.7 sim_node_task — 5bus 仿真任务

| 属性 | 值 |
|------|-----|
| **优先级** | 8 |
| **栈大小** | 16KB |
| **源文件** | [freertos/src/sim_node_task.c](../freertos/src/sim_node_task.c) |
| **功能** | IEEE 5-Bus / 2-gen 电力系统 RK4 仿真，输出到 SHM 0xC8100000 |

**参数**:

| 参数 | 值 |
|------|-----|
| 状态维 ns | 4 |
| 测量维 nm | 14 |
| 步长 DT | 0.0005s |
| 目标频率 | 2000Hz |
| SHM 基地址 | 0xC8100000 |
| SHM 大小 | 256KB |

**执行流程**:

```
sim_node_task()
  ├── 创建 RPMsg endpoint "rpmsg-sim-5bus"
  ├── 等待 Linux 绑定
  ├── 等待 START 命令
  └── while(step < NUM_STEPS):
      ├── rk4_step() 更新状态
      ├── h_measurement() 计算 Z[14]
      ├── 缓冲 8 帧
      ├── SHM 背压检查：满则等待
      ├── 写入 SHM 0xC8100000
      └── 每 8 帧批量发送 RPMsg（可选）
```

---

### 2.8 sim_node_9bus_task — 9bus 仿真任务

| 属性 | 值 |
|------|-----|
| **优先级** | 4 |
| **栈大小** | 16KB |
| **源文件** | [freertos/src/sim_node_9bus.c](../freertos/src/sim_node_9bus.c) |
| **功能** | IEEE 9-Bus / 3-gen 仿真，SHM 0xC81C0000 |

| 参数 | 值 |
|------|-----|
| 状态维 ns | 6 |
| 测量维 nm | 24 |
| 目标频率 | 2000Hz（上限） |
| SHM 基地址 | 0xC81C0000 |
| SHM 大小 | 128KB |

---

### 2.9 sim_node_39bus_task — 39bus 仿真任务

| 属性 | 值 |
|------|-----|
| **优先级** | 6 |
| **栈大小** | 32KB |
| **源文件** | [freertos/src/sim_node_39bus.c](../freertos/src/sim_node_39bus.c) |
| **功能** | IEEE 39-Bus / 10-gen 仿真，SHM 0xC8140000 |

| 参数 | 值 |
|------|-----|
| 状态维 ns | 20 |
| 测量维 nm | 98 |
| 目标频率 | 受 UKF 消费能力限制，约 250~300Hz |
| SHM 基地址 | 0xC8140000 |
| SHM 大小 | 512KB |

> 39bus 仿真在写入 SHM 前执行背压检查，自动匹配 Linux UKF 处理速度。

---

## 3. 任务间交互

```
┌─────────────────────────────────────────────────────────────────┐
│                         FreeRTOS 任务交互                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  UART2 ISR ──→ lora_uart.c ringbuf ──→ master_recv_task         │
│                                           ↓                      │
│                                    g_recv_queue (16 slots)       │
│                                           ↓                      │
│                                    master_process_task           │
│                                           ↓                      │
│                                    master_sys.c (模拟 Flash)     │
│                                           ↓                      │
│                                    master_poll_task              │
│                                           ↓                      │
│                                    rpm_task ──→ RPMsg → Linux    │
│                                                                  │
│  sim_node_task    ──→ SHM 0xC8100000 ──→ Linux ukf_pipeline_5bus │
│  sim_node_9bus_task ──→ SHM 0xC81C0000 ──→ Linux ukf_pipeline_9bus│
│  sim_node_39bus_task ──→ SHM 0xC8140000 ──→ Linux ukf_pipeline_39bus_ft│
│                                                                  │
│  aux_task ──→ GPIO2_10 ──→ SHM_HB 心跳计数器                     │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 4. 关键数据类型

### 4.1 LoRa 数据类型

```c
// freertos/inc/data_frame.h
typedef enum {
    DATA_TYPE_NODE_HEAD  = 0x01,
    DATA_TYPE_POWER      = 0x03,
    DATA_TYPE_NODE_RAW   = 0x04,
    DATA_TYPE_FAULT_HEAD = 0x07,
} DataType_t;
```

> 旧版 `DATA_TYPE_FLASH_WAVE = 0x05` 与 `DATA_TYPE_WAVE = 0x02` 在当前代码中未定义/未使用。

### 4.2 主控命令码

```c
// freertos/inc/master.h
#define CMD_REQUEST_WAVEFORM     0x10
#define CMD_CLEAR_FLASH          0x12
#define CMD_POLL_STATUS          0x14
#define CMD_REQUEST_FAULT_DATA   0x15
```

### 4.3 RPMsg 通用协议

```c
// freertos/inc/rpmsg_proto.h
typedef struct __attribute__((packed)) {
    u32 command;            // 4 B
    u16 length;             // 2 B
    u8  data[489];          // payload
} RpmsgPkt;

#define CMD_LORA_RAW    0x0023
#define CMD_NODE_STATUS 0x0025
#define CMD_HEARTBEAT   0x0030
#define CMD_ECHO_REQ    0x0040
#define CMD_ECHO_RESP   0x0041
```

---

## 5. 遗留与待同步点

| 项目 | 说明 |
|------|------|
| `rpmsg-echo_os.c` | 当前未接入 `main.c`，且存在未定义符号；需决定废弃或补全 |
| `master_cmd_task` | 已不存在；功能迁移到 `master_poll_task` |
| `RpmsgEchoTask` | 旧任务名；当前对应 `rpm_task` |
| 加解密策略 | 代码与 `freertos/OPERATIONS.md` 口径不一致 |
| Task 1 RPMsg 数据路径 | `rpmsg_send_lora_raw()` 已准备但未被调用，业务数据当前走 SHM 调试打印 |
| 9/39bus 控制码 | 当前硬编码为 `0x0070`/`0x0060`，建议统一到 `CMD_SIM_CTRL 0x51` |
| Linux 接收程序 | `src/openamp-demo/linux-master/master_receiver.c` 使用旧命名 `DEVICE_LORA_DATA`，建议统一 |

---

**版本**: v2.1 | **更新**: 2026-06-24 | **状态**: 已同步当前 9 任务架构与 RPMsg 协议状态
