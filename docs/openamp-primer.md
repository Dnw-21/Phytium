# OpenAMP 基础知识与项目实践指南

> **更新**: 2026-07-08 | **项目**: Phytium PE2204 飞腾派异构多核系统

---

## 目录

1. [OpenAMP 是什么](#1-openamp-是什么)
2. [核心组件](#2-核心组件)
3. [运行机制](#3-运行机制)
4. [项目中的内存布局](#4-项目中的内存布局)
5. [FreeRTOS 侧实现](#5-freertos-侧实现)
6. [Linux 侧实现](#6-linux-侧实现)
7. [RPMsg 协议定义](#7-rpmsg-协议定义)
8. [端点创建与回调模式](#8-端点创建与回调模式)
9. [启动流程](#9-启动流程)
10. [通信通道总览](#10-通信通道总览)
11. [调试与排错](#11-调试与排错)
12. [常见问题](#12-常见问题)

---

## 1. OpenAMP 是什么

**OpenAMP** (Open Asymmetric Multi-Processing) 是一个软件框架，用于在**异构多核 SoC** 上实现不同操作系统之间的核间通信。它提供了一套标准组件：

```
┌─────────────────────────────────────────────────────┐
│                    应用层                            │
├─────────────────────────────────────────────────────┤
│  RPMsg (Remote Processor Messaging)                 │
│  → 基于通道(channel)的消息收发 API                   │
├─────────────────────────────────────────────────────┤
│  VirtIO (Virtual I/O)                               │
│  → 标准化的虚拟设备框架，用 vring 做数据搬运          │
├─────────────────────────────────────────────────────┤
│  remoteproc (Remote Processor)                      │
│  → 远程核心的生命周期管理 (加载/启动/停止固件)        │
├─────────────────────────────────────────────────────┤
│  物理层: 共享内存 + 核间中断 (IPI/SGI)               │
└─────────────────────────────────────────────────────┘
```

**本项目场景**: 飞腾 PE2204 的 Linux (主核) 与 FreeRTOS (从核) 通过 OpenAMP 通信。

---

## 2. 核心组件

### 2.1 remoteproc — 远程核心管理

负责从核固件的**加载、启动、停止**。

| 组件 | 位置 | 说明 |
|------|------|------|
| Linux 驱动 | `drivers/remoteproc/homo_remoteproc.c` (飞腾定制) | 解析 resource table、加载 ELF、CPU hotplug |
| 设备树节点 | [device-tree/openamp.dtso](file:///e:/飞腾派/Phytium/device-tree/openamp.dtso) | 定义预留内存、中断号、固件路径 |
| Resource Table | [freertos/main.c 第 131-148 行](file:///e:/飞腾派/Phytium/freertos/main.c#L131-L148) | 从核端描述 vring 和共享内存布局 |

### 2.2 VirtIO / vring — 数据传输通道

VirtIO 用 **vring**（virtqueue ring buffer）在共享内存中搬运数据：

```
┌──────────────┐         ┌──────────────┐
│  vring0 (TX)  │  ────→  │  vring1 (RX) │
│  FreeRTOS→Linux│        │  Linux→FreeRTOS│
│  notifyid=1   │        │  notifyid=2   │
└──────────────┘         └──────────────┘
```

- 两个 vring：**vring0** 用于 FreeRTOS→Linux (TX)，**vring1** 用于 Linux→FreeRTOS (RX)
- 每个 vring 包含 descriptor ring、available ring、used ring 三个部分
- 数据收发不需要拷贝，直接在共享内存中操作描述符

### 2.3 RPMsg — 远程消息传递

RPMsg 是构建在 VirtIO 之上的**消息协议**，提供了基于**通道名字**的寻址机制：

```
Linux 应用                      FreeRTOS 任务
┌──────────┐                     ┌──────────┐
│ rpmsg_recv│ ──"openamp-demo"── │ rpm_task │
│           │                    │          │
│ start_sim │ ──"rpmsg-sim-5bus"─│ sim_node │
└──────────┘                     └──────────┘
```

- 每个通道有唯一的**名字**（字符串）和**端点地址**（32bit）
- 发送方通过 `(通道名, 目标地址)` 定位接收方
- 内置 **name service** 自动完成名字到地址的解析

### 2.4 核间中断 — IPI / SGI

当一端写入 vring 后，需要通过**核间中断**通知另一端：

| 参数 | 值 | 说明 |
|------|-----|------|
| 中断类型 | GICv3 SGI (Software Generated Interrupt) | 软件触发中断 |
| 中断号 | SGI 9 | 设备树中 `inter-processor-interrupt = <9>` |
| Kick 寄存器 | `DEVICE00_KICK_IO_ADDR` | GICv3 寄存器地址 |

---

## 3. 运行机制

### 3.1 资源表 (Resource Table)

从核固件中必须内嵌一个 **resource table**，放在 `.resource_table` 段中，Linux remoteproc 驱动启动从核前会解析此表：

```c
// [freertos/main.c](file:///e:/飞腾派/Phytium/freertos/main.c#L131-L148)
struct remote_resource_table resources
    __attribute__((section(".resource_table"), used)) = {
    // 版本号 + 条目数
    1, NUM_TABLE_ENTRIES, {0, 0},
    // 指向 vdev 的偏移
    { offsetof(struct remote_resource_table, rpmsg_vdev) },
    // vdev 描述: 类型=RSC_VDEV, ID=VIRTIO_ID_RPMSG_, 2 个 vring
    { RSC_VDEV, VIRTIO_ID_RPMSG_, VDEV_NOTIFYID, RPMSG_IPU_C0_FEATURES,
      0, 0, 0, NUM_VRINGS, {0, 0} },
    // vring0 (TX): 设备地址、对齐、大小、通知ID
    { DEVICE00_TX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 1, 0 },
    // vring1 (RX)
    { DEVICE00_RX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 2, 0 },
};
```

### 3.2 Kick 机制

```c
// [freertos/main.c](file:///e:/飞腾派/Phytium/freertos/main.c#L151-L165)
static metal_phys_addr_t pa = DEVICE00_KICK_IO_ADDR;
struct metal_device kd = {
    .name       = DEVICE_00_KICK_DEV_NAME,
    .num_regions = 1,
    .regions    = { { .virt = (void *)DEVICE00_KICK_IO_ADDR,
                      .physmap = &pa,
                      .size    = 0x1000, ... } },
    .irq_num    = 1,
    .irq_info   = (void *)DEVICE_00_SGI,    // GICv3 SGI 9
};
```

### 3.3 name service 地址绑定流程

RPMsg 端点之间的地址绑定通过 **name service 广播** 自动完成：

```
1. FreeRTOS 创建端点: rpmsg_create_ept(..., "rpmsg-openamp-demo-channel", src=0x66, dest=ANY)
2. FreeRTOS 广播 NS 消息: "我是通道 rpmsg-openamp-demo-channel，地址=0x66"
3. Linux 创建端点: ioctl(CREATE_EPT, name="rpmsg-openamp-demo-channel", dst=0x66)
4. Linux 发送数据到 dst=0x66
5. FreeRTOS 收到消息 → ept->dest_addr = src(Linux)  ← 绑定完成
```

**关键**: `ept->dest_addr` 只有在**收到对方消息**时才会被更新为非 ANY 值。在此之前发送失败或不发送。

---

## 4. 项目中的内存布局

### 4.1 物理内存分配

```
0x80000000 ─────────────────────────────  Linux 可用 (~768MB)
0xB0100000 ─┬───────────────────────────  OpenAMP 共享内存开始 (409MB, no-map)
            │  vring0 (TX) + vring1 (RX)
            │  RPMsg 缓冲区
            │  从核固件数据
0xC8000000  ├─ SHM 调试打印区 (2MB)      ← trace_reader 读取
0xC8100000  ├─ SHM 5bus 数据区 (256KB)
0xC8140000  ├─ SHM 39bus 数据区 (512KB)
0xC81C0000  ├─ SHM 9bus 数据区 (128KB)
0xC9A00000 ─┴───────────────────────────
            Linux 可用 (~3GB+)
```

### 4.2 设备树配置

```dts
// [device-tree/openamp.dtso](file:///e:/飞腾派/Phytium/device-tree/openamp.dtso)
rproc@b0100000 {
    no-map;
    reg = <0x0 0xb0100000 0x0 0x19900000>;  // 409MB
};

homo_rproc {
    compatible = "homo,rproc";
    remote-processor = <3>;                   // 从核 CPU 编号 (实际运行在 CPU1)
    inter-processor-interrupt = <9>;          // GICv3 SGI 9
    firmware-name = "openamp_core0.elf";      // 从核固件路径
};
```

---

## 5. FreeRTOS 侧实现

### 5.1 OpenAMP 初始化流程

在 `main()` 中按以下顺序执行（[freertos/main.c 第 383-417 行](file:///e:/飞腾派/Phytium/freertos/main.c#L383-L417)）：

```c
// Step 1: SDK 系统初始化 (必须第一句)
init_system();

// Step 2: MMU 映射所有物理地址
FMmuMap(0xC8000000, ...);  // SHM 调试打印区
FMmuMap(U2_BASE, ...);     // UART2
// ... 其他映射

// Step 3: 创建 remoteproc 实例
platform_create_proc(&g_rproc, &dp, &kd);

// Step 4: 设置资源表 + 共享内存
g_rproc.rsc_table = &resources;
platform_setup_src_table(&g_rproc, g_rproc.rsc_table);
platform_setup_share_mems(&g_rproc);

// Step 5: 创建 RPMsg virtio 设备
g_rpdev = platform_create_rpmsg_vdev(&g_rproc, 0,
                                     VIRTIO_DEV_DEVICE, NULL, NULL);
// g_rpdev 是全局 RPMsg 设备句柄，所有 endpoint 创建都依赖它
```

### 5.2 编译配置

```ini
# [freertos/openamp_for_linux.config](file:///e:/飞腾派/Phytium/freertos/openamp_for_linux.config)
CONFIG_USE_AMP=y
CONFIG_USE_OPENAMP=y
CONFIG_USE_OPENAMP_IPI=y
CONFIG_SKIP_SHBUF_IO_WRITE=y
CONFIG_USE_CACHE_COHERENCY=y
```

### 5.3 端点创建

项目中各任务创建的 RPMsg 端点：

| 任务 | 通道名 | src 地址 | 回调函数 | 文件 |
|------|--------|---------|---------|------|
| rpm_task | `rpmsg-openamp-demo-channel` | `0x66` | `rpmsg_endpoint_cb` | [main.c:310](file:///e:/飞腾派/Phytium/freertos/main.c#L310) |
| sim_node_task (5bus) | `rpmsg-sim-5bus` | `0` | `sim_ept_cb` | [sim_node_task.c:113](file:///e:/飞腾派/Phytium/freertos/src/sim_node_task.c#L113) |
| sim_node_9bus_task | `rpmsg-sim-9bus` | `0x14` | `sim9_cb` | [sim_node_9bus.c:118](file:///e:/飞腾派/Phytium/freertos/src/sim_node_9bus.c#L118) |
| sim_node_39bus_task | `rpmsg-sim-39bus` | `0x28` | `sim39_cb` | sim_node_39bus.c |

> **重要**: 之前 `rpm_task` 和 `sim_node_task` 都使用 `src=0`，导致 Linux 侧 `dst=0` 的消息被路由到错误的端点，造成 `ept->dest_addr` 无法更新。已修复为 `src=0x66`。

### 5.4 消息接收回调

```c
// [freertos/main.c](file:///e:/飞腾派/Phytium/freertos/main.c#L188-L220)
static int rpmsg_endpoint_cb(struct rpmsg_endpoint *ept, void *data,
                              size_t len, u32 src, void *priv)
{
    ept->dest_addr = src;  // ← 收到消息时自动记录发送方地址
    //  在此之前 dest_addr = 0xFFFFFFFF (RPMSG_ADDR_ANY)

    switch (pkt.command) {
    case CMD_ECHO_REQ:
        // 回复 ECHO_RESP，用于验证双向通信
        int ret = rpmsg_send(ept, &resp, ...);
        if (ret < 0) g_rpmsg_tx_err++;
        else g_rpmsg_tx_cnt++;
        break;
    default:
        shm_spf("RPMsg rx: cmd=0x%04X len=%u\r\n", pkt.command, pkt.length);
        break;
    }
    return RPMSG_SUCCESS;
}
```

---

## 6. Linux 侧实现

### 6.1 驱动加载

```bash
# Systemd 服务自动执行 (见 config/openamp.service)
modprobe rpmsg_char      # RPMsg 字符设备驱动
modprobe rpmsg_ctrl      # RPMsg 控制接口
echo start > /sys/class/remoteproc/remoteproc0/state  # 启动从核
```

### 6.2 应用层端点创建（ioctl 方式）

```c
// [freertos/rpmsg_bind.c](file:///e:/飞腾派/Phytium/freertos/rpmsg_bind.c)  — 简化绑定工具
// [src/linux-app/rpmsg_recv.c](file:///e:/飞腾派/Phytium/src/linux-app/rpmsg_recv.c)  — 完整接收器

// 1. 打开控制设备
ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);

// 2. 创建端点
struct rpmsg_endpoint_info eptinfo;
strncpy(eptinfo.name, "rpmsg-openamp-demo-channel", ...);
eptinfo.src = RPMSG_ADDR_ANY;    // 让内核分配源地址
eptinfo.dst = 0x66;              // 目标 FreeRTOS 端点地址
ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);

// 3. 打开生成的 /dev/rpmsgX 设备
ept_fd = open("/dev/rpmsg0", O_RDWR);

// 4. 发送消息绑定 dest_addr
RpmsgPkt pkt;
pkt.command = CMD_ECHO_REQ;
pkt.length = 5;
memcpy(pkt.data, "HELLO", 5);
write(ept_fd, &pkt, RPMSG_PKT_HDR_SIZE + 5);  // 触发 FreeRTOS 回调

// 5. 后续正常收发
read(ept_fd, rbuf, sizeof(rbuf));   // 接收
write(ept_fd, &pkt, len);           // 发送
```

### 6.3 Linux 应用文件索引

| 文件 | 用途 |
|------|------|
| [src/linux-app/rpmsg_recv.c](file:///e:/飞腾派/Phytium/src/linux-app/rpmsg_recv.c) | 当前主接收器，处理 LoRa 数据/心跳/Echo |
| [freertos/rpmsg_bind.c](file:///e:/飞腾派/Phytium/freertos/rpmsg_bind.c) | 轻量绑定工具，发送 ECHO_REQ 后保持连接 |
| [task2_linux/start_sim_nodes.c](file:///e:/飞腾派/Phytium/task2_linux/start_sim_nodes.c) | 多节点仿真启动器，控制 5/9/39bus 仿真 |
| [src/openamp-demo/linux-master/rpmsg_master.c](file:///e:/飞腾派/Phytium/src/openamp-demo/linux-master/rpmsg_master.c) | 参考实现，Echo 主控 |
| [demo/rpmsg-demo-single.c](file:///e:/飞腾派/Phytium/demo/rpmsg-demo-single.c) | 单次 RPMsg Echo 演示 |

---

## 7. RPMsg 协议定义

### 7.1 统一数据包格式

```c
// [freertos/inc/rpmsg_proto.h](file:///e:/飞腾派/Phytium/freertos/inc/rpmsg_proto.h)

#define RPMSG_MAX_PAYLOAD  489    // 单帧最大 payload
#define RPMSG_PKT_HDR_SIZE 6     // 包头大小

typedef struct __attribute__((packed)) {
    u32 command;                      // 4 bytes: 命令码
    u16 length;                       // 2 bytes: payload 长度
    u8  data[RPMSG_MAX_PAYLOAD];      // 0~489 bytes: 数据
} RpmsgPkt;
// 总大小: 6 + 489 = 495 bytes
```

**FreeRTOS 侧和 Linux 侧必须使用完全一致的 `RpmsgPkt` 结构体定义**，包括字节序和对齐方式。

### 7.2 命令码定义

#### 通用通道 (`rpmsg-openamp-demo-channel`)

| 命令 | 值 | 方向 | 说明 |
|------|-----|------|------|
| `CMD_LORA_RAW` | `0x0023` | RTOS → Linux | LoRa 原始帧透传（已实现，待接入） |
| `CMD_LORA_PARSED` | `0x0024` | RTOS → Linux | LoRa 解析后数据（预留） |
| `CMD_NODE_STATUS` | `0x0025` | RTOS → Linux | 节点在线/故障状态（预留） |
| `CMD_HEARTBEAT` | `0x0030` | RTOS → Linux | 心跳（rpm_task 每 5000 poll 周期 ≈ 10s 发一次） |
| `CMD_ECHO_REQ` | `0x0040` | Linux → RTOS | 端点绑定测试请求 |
| `CMD_ECHO_RESP` | `0x0041` | RTOS → Linux | Echo 响应 |

#### 仿真控制通道

| 通道名 | 控制命令码 | 说明 |
|--------|-----------|------|
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | START/STOP/RESET/SPEED |
| `rpmsg-sim-9bus` | `0x0070` | 9bus 控制（硬编码） |
| `rpmsg-sim-39bus` | `0x0060` | 39bus 控制（硬编码） |

仿真控制子命令:
- `0x00` = STOP
- `0x01` = START
- `0x02` = RESET
- `0x03` = SPEED (data 字段带速度值)

#### 历史遗留命令码（已废弃）

```c
// 来自 rpmsg-echo_os.c，不再使用
#define DEVICE_CORE_CHECK    0x0003U
#define DEVICE_SENSOR_DATA   0x0010U
#define DEVICE_MASTER_CMD    0x0021U
#define DEVICE_LORA_CTRL     0x0022U
```

---

## 8. 端点创建与回调模式

### 8.1 FreeRTOS 侧（OpenAMP 原生 API）

```c
// 创建端点
int ret = rpmsg_create_ept(
    &ept,                    // 端点结构体（需为持久存储，不能是局部变量）
    g_rpdev,                 // RPMsg 设备句柄
    "通道名",                // 服务名称
    src_addr,                // 本地地址（如 0x66，同一设备上各端点必须唯一）
    RPMSG_ADDR_ANY,          // 目标地址（初始为 ANY，收到消息后自动更新）
    ept_cb,                  // 消息接收回调
    ept_unbind_cb            // 解绑回调
);

// 发送消息
rpmsg_send(&ept, data, len);

// 轮询处理消息（必须在任务循环中周期性调用）
platform_poll_nonblocking(&g_rproc);
```

### 8.2 Linux 侧（ioctl + read/write）

```c
// 创建端点
struct rpmsg_endpoint_info eptinfo = {
    .name = "通道名",
    .src  = RPMSG_ADDR_ANY,    // 内核自动分配
    .dst  = 目标地址            // FreeRTOS 端点地址
};
ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);

// 收发
write(ept_fd, data, len);    // 发送
read(ept_fd, buf, size);     // 接收（阻塞或非阻塞）
```

---

## 9. 启动流程

```
┌──────────────────────────────────────────────────────────────┐
│  Linux 启动                                                   │
│  ├── systemd 加载 openamp.service                             │
│  │   ├── modprobe rpmsg_char rpmsg_ctrl                      │
│  │   └── echo start > /sys/class/remoteproc/remoteproc0/state│
│  │       ├── 解析 resource table (vring 地址/大小)            │
│  │       ├── 加载 openamp_core0.elf 到 0xB0100000            │
│  │       ├── PSCI CPU_ON → 从核启动                           │
│  │       └── 等待从核 VirtIO 握手                             │
│  │                                                            │
│  │   从核 (FreeRTOS) 启动                                     │
│  │   ├── init_system()                                       │
│  │   ├── MMU 映射所有物理地址                                  │
│  │   ├── platform_create_proc() + setup tables               │
│  │   ├── platform_create_rpmsg_vdev() → g_rpdev              │
│  │   ├── UART2/LoRa AT 配置                                   │
│  │   ├── 创建 9 个 FreeRTOS 任务                               │
│  │   └── vTaskStartScheduler()                               │
│  │                                                            │
│  │   各任务启动                                                │
│  │   ├── rpm_task: 轮询 2s → 创建端点 → 进入 poll 循环        │
│  │   ├── sim_node_task: 创建端点 → 等待 Linux bind            │
│  │   └── 其他任务: 各自初始化并开始工作                         │
│  │                                                            │
│  │   Linux 检测到端点                                          │
│  │   └── virtio_rpmsg_bus 创建 /sys/bus/rpmsg/devices/       │
│  │       └── 绑定 rpmsg_chrdev → 生成 /dev/rpmsg0            │
│  │                                                            │
│  │   用户启动应用                                              │
│  │   ├── ./rpmsg_bind  → 绑定通用通道 dest_addr               │
│  │   ├── ./rpmsg_recv  → 接收 LoRa 数据                       │
│  │   └── ./start_sim_nodes → 控制仿真任务                      │
└──────────────────────────────────────────────────────────────┘
```

---

## 10. 通信通道总览

```
                          Linux 侧                     FreeRTOS 侧
                     ┌─────────────────┐         ┌─────────────────────┐
LoRa 数据 (预留) ───│ rpmsg_recv       │◄────────│ rpm_task            │
心跳/Echo ─────────│ (通用通道)        │◄───────→│ (CMD_HEARTBEAT/ECHO)│
                     │                 │         │                     │
5bus 仿真控制 ──────│ start_sim_nodes  │────────→│ sim_node_task       │
5bus 数据 ─────────│ (SHM 读取)       │◄────────│ (RPMsg 批量 + SHM)  │
                     │                 │         │                     │
9bus 仿真控制 ──────│ start_sim_nodes  │────────→│ sim_node_9bus_task  │
9bus 数据 ─────────│ (SHM 读取)       │◄────────│ (SHM 写入)          │
                     │                 │         │                     │
39bus 仿真控制 ─────│ start_sim_nodes  │────────→│ sim_node_39bus_task │
39bus 数据 ────────│ (SHM 读取)       │◄────────│ (SHM 写入)          │
                     │                 │         │                     │
调试打印 ──────────│ trace_reader     │◄────────│ 所有任务            │
                     │ (/dev/mem)      │         │ (SHM 0xC8000000)    │
                     └─────────────────┘         └─────────────────────┘
```

**通道名一览:**

| 通道名 | 端点地址 (FreeRTOS) | 数据类型 | 传输方式 |
|--------|---------------------|---------|---------|
| `rpmsg-openamp-demo-channel` | `src=0x66` | LoRa/心跳/Echo | RPMsg |
| `rpmsg-sim-5bus` | `src=0` | 仿真控制 + 批量数据 | RPMsg + SHM |
| `rpmsg-sim-9bus` | `src=0x14` | 仿真控制 | RPMsg (数据走 SHM) |
| `rpmsg-sim-39bus` | `src=0x28` | 仿真控制 | RPMsg (数据走 SHM) |
| (调试) | `0xC8000000` | 调试打印 | 共享内存直接写 |

---

## 11. 调试与排错

### 11.1 常用调试命令

```bash
# 查看 remoteproc 状态
cat /sys/class/remoteproc/remoteproc0/state

# 查看已创建的 RPMsg 设备
ls /sys/bus/rpmsg/devices/

# 查看 /dev 下的 RPMsg 设备节点
ls -la /dev/rpmsg*

# 查看 dmesg 中的 OpenAMP 相关日志
dmesg | grep -i "rpmsg\|remoteproc\|virtio\|openamp"

# 手动启动/停止从核
echo start > /sys/class/remoteproc/remoteproc0/state
echo stop  > /sys/class/remoteproc/remoteproc0/state

# 查看 FreeRTOS 调试打印
sudo /home/user/trace_reader
```

### 11.2 FreeRTOS 侧诊断

观察 trace_reader 中的关键日志行：

```text
// 确认 OpenAMP 初始化成功
RPMsg done                      ← platform_create_rpmsg_vdev 成功

// 确认端点创建成功
RPM: ept ret=0 addr=0x66       ← ret=0 表示成功，addr 印自己的地址

// 确认绑定状态（重要！）
[RPM-hb] ept=0x... dest=0x66 tx=1 err=0   ← dest 不再是 0xFFFFFFFF 就表示绑定成功
```

### 11.3 常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| `g_rpdev` 为 NULL | RPMsg 初始化失败 | 检查资源表是否正确，共享内存是否映射 |
| `rpmsg_create_ept` 返回非 0 | 端点地址冲突或设备未就绪 | 检查是否有其他端点使用相同 src 地址 |
| `dest_addr` 始终为 0xFFFFFFFF | Linux 侧未发送消息或消息路由错误 | 确认 Linux 侧 `eptinfo.dst` 与 FreeRTOS 侧 `src` 匹配 |
| `rpmsg_send` 返回负数 | 目标地址未绑定 (dest=ANY) | 需先收到 Linux 消息才能发送 |
| `write` 成功但 FreeRTOS 收不到 | vring 中断未触发或 polling 未执行 | 确认 SGI 9 配置和 `platform_poll_nonblocking` 调用频率 |
| trace_reader 无输出 | SHM 映射地址错误 | 确认 `0xC8000000` 在 MMU 映射范围内 |

### 11.4 端点地址冲突排查

本项目经历过的典型问题：**多个端点使用相同 src=0**。

```text
// 问题:
rpm_task:     src=0,  channel="rpmsg-openamp-demo-channel"
sim_node_task: src=0,  channel="rpmsg-sim-5bus"
// Linux 侧 dst=0 → 消息被路由到 sim_node_task 而不是 rpm_task

// 解决:
rpm_task:     src=0x66  ← 改为唯一地址
// Linux 侧 dst=0x66 → 正确路由到 rpm_task
```

每个 `g_rpdev` 设备上的所有端点 **src 地址必须唯一**，建议手动分配大于 0 的值。

---

## 12. 常见问题

### Q1: `ept->dest_addr` 如何更新？

**答**: 只有**收到**对方发来的消息时，回调函数中的 `ept->dest_addr = src` 才会执行。在此之前 `dest_addr = RPMSG_ADDR_ANY (0xFFFFFFFF)`，此时无法向外发送。需要用 `rpmsg_bind` 先发一条消息过来触发更新。

### Q2: `rpmsg_send` 什么时候可以成功？

**答**: 必须满足 `ept->dest_addr != RPMSG_ADDR_ANY`。如果 `dest_addr` 还是 ANY，`rpmsg_send` 会返回负数。

### Q3: FreeRTOS 侧的端点变量放哪里？

**答**: 必须是**全局变量或静态变量**，不能是任务栈上的局部变量。`rpm_task` 中的 `le` 是局部变量，但只要 `rpm_task` 不退出就可以。`sim_node_task` 使用 `static struct rpmsg_endpoint sim_ept` 更安全。

### Q4: Linux 侧 `/dev/rpmsgX` 设备什么时候出现？

**答**: 需要两个条件：
1. FreeRTOS 侧创建了端点（设备树 remote-processor 对应的核运行了固件）
2. Linux 侧 `echo rpmsg_chrdev > .../driver_override` 并绑定驱动
   

通常在 systemd `openamp.service` 中自动完成。

### Q5: 如何判断通道是否正常工作？

**答**: 用 `rpmsg_bind` 发一条 `CMD_ECHO_REQ`，然后看 FreeRTOS 的心跳行：
```text
[RPM-hb] ept=0xb013c958 dest=0x66 tx=1 err=0 poll=0
                          ^^^^^^^^     ^^^^
                          有实地址     tx>0 说明收发正常
```

---

**版本**: v1.0 | **更新**: 2026-07-08 | **维护**: 飞腾派项目组
