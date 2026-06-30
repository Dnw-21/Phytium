# OpenAMP 异构多核通信流程详解

> **更新**: 2026-05-28 | **当前架构**: Linux 侧接收 + FreeRTOS 主控侧实际 CPU1（设备树写 CPU3）+ LoRa 真实硬件接入 + 精简链路

## 1. 通信架构总览

```
┌──────────────────────────────────────────────────────────────────┐
│                     Phytium PE2204 SoC                            │
│                                                                   │
│  Linux 侧接收                    FreeRTOS 主控侧                 │
│  ┌────────────────────────┐      ┌───────────────────────────┐  │
│  │  master_receiver       │      │  master_recv_task(Prio=4) │  │
│  │  (只显示LoRa原始数据)  │      │  master_recv.c            │  │
│  │       ↕                │      │       ↕                    │  │
│  │  /dev/rpmsg0           │RPMsg │  lora_uart.c / UART2      │←─┤ LoRa模块
│  │       ↕                │←────→│  115200 8N1               │  │ UART2
│  │  rpmsg_char.ko         │SGI 9 │  当前以真实硬件为准        │  │ 0xAA55帧
│  │  virtio_rpmsg_bus      │      │       ↕                    │  │
│  │  homo_remoteproc       │      │  rpmsg_send_lora_recv_log │  │
│  └───────────┬────────────┘      │  (DEVICE_LORA_DATA 0x0023)│  │
│              │                   └──────────────┬────────────┘  │
│              │    ┌─────────────────────────────┴───────────┐   │
│              └────│ 共享内存 0xB0100000 (409MB)              │   │
│                   │ vring0(TX) + vring1(RX) + 缓冲区 + 固件  │   │
│                   └─────────────────────────────────────────┘   │
│                                                                   │
│              ┌──────────────────────────┐                        │
│              │  IPI 中断 (GICv3 SGI 9)  │                        │
│              │  Linux ←→ FreeRTOS 通知  │                        │
│              └──────────────────────────┘                        │
└──────────────────────────────────────────────────────────────────┘
```

## 2. 数据流: LoRa终端节点 → Linux终端显示

### 当前架构 (精简模式, 2026-05-19)

```
GD32终端节点 ──LoRa无线──→ LoRa模块(UART) ──UART2──→ FreeRTOS 主控侧（实际 CPU1，设备树写 CPU3）
  → lora_uart_poll() / 当前串口接收实现
    → lora_uart_recv_frame() 帧同步 + CRC8校验 + 组帧
      → rpmsg_send_lora_recv_log() 透传原始帧
        → RPMsg DEVICE_LORA_DATA (0x0023)
          → Linux master_receiver → printf hex
```

**关键设计决策**:
- **不进行帧解析** (不走 parse_frame / 判决引擎 / 命令生成)
- **不发送任何命令** 给终端节点
- **单向数据流**: 终端 → 主控显示
- **真实 LoRa 硬件链路为当前主控路线**
- **Linux 侧负责显示/记录，LoRa 串口由 FreeRTOS 主控侧处理**

### 第1步: LoRa模块 → UART2 RX

**LoRa模块连接**: 以当前实际接线和 [docs/调试.md](调试.md) 的纠正为准，J1 Pin8/Pin10 是 UART2，不是 UART3。

| 飞腾派 Pin | 信号 | LoRa模块 | 线色 |
|:----------:|------|:--------:|:----:|
| Pin 8 | UART2_TXD | RXD | 黄色 |
| Pin 10 | UART2_RXD | TXD | 橙色 |
| Pin 6 | GND | GND | 黑色 |
| Pin 1 | VCC_3.3V | VCC | 红色 |

详见 [lora-real-hardware-接入指南.md](lora-real-hardware-接入指南.md)

**UART 配置** — [lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c):
- 当前口径: UART2，J1 Pin8/Pin10
- 波特率: **115200** 8N1
- RX ring buffer: 4096 字节
- 轮询模式 (FreeRTOS task 每 10ms 调用 `lora_uart_poll()`)

### 第2步: 帧提取

**LoRa 帧格式** (与 GD32 终端一致):
```
[0xAA][0x55][LEN_H][LEN_L][ DATA(N字节) ][CRC8][0x55][0xAA]
帧总长 = 7 + N
```

**函数**: `lora_uart_recv_frame()` — [lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c)
- 状态机: SYNC_HDR1→HDR2→LEN_H→LEN_L→DATA→CRC→TAIL1→TAIL2
- CRC8 校验通过才返回帧
- 返回完整帧 buffer (含 0xAA 0x55 帧头和 0x55 0xAA 帧尾)

### 第3步: RPMsg 透传 (FreeRTOS→Linux)

**新增端点**: `DEVICE_LORA_DATA 0x0023`

**函数**: `rpmsg_send_lora_recv_log()` — [rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c)
```c
int rpmsg_send_lora_recv_log(const uint8_t *raw_data, uint16_t raw_len)
{
    ProtocolData tx_data;
    tx_data.command = DEVICE_LORA_DATA;
    tx_data.length = raw_len;
    memcpy(tx_data.data, raw_data, raw_len);
    return rpmsg_send(g_ept, &tx_data, 6 + raw_len);
}
```

**调用点**: [master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) — `master_recv_task`:
```c
raw_len = master_recv_lora_data(raw_buf, sizeof(raw_buf));
if (raw_len > 0) {
    rpmsg_send_lora_recv_log(raw_buf, raw_len);  // 透传到Linux
}
```

### 第4步: Linux 显示

**程序**: [master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c)

**行为**: 只监听 `DEVICE_LORA_DATA`，收到即打印 hex:
```
[#1] len=20  AA 55 00 10 83 2A 07 AD 5F 65 3E 85 A6 DF 6F 3D D9 7B 56 90
[#2] len=132 AA 55 00 80 08 8A 65 F7 86 22 60 77 36 CB 9F 5A ...
```

**运行**:
```bash
ssh user@192.168.88.11
~/Phytium/demo/master_receiver
```

## 3. 数据格式定义

### RPMsg 消息格式

所有消息遵循统一的 `ProtocolData` 格式:

```
[4B command][2B length][nB data]
```

| 字段 | 大小 | 说明 |
|------|------|------|
| command | 4字节 | 消息类型 (0x0020/0x0021/0x0010/0x0011等) |
| length | 2字节 | data字段长度 |
| data | 变长 | 消息载荷 |

定义位置: [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) 和 [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c)

### LoRa 帧格式

```
[0xAA][0x55][LEN][NODE_ID][TYPE][PAYLOAD][CRC8][0x55][0xAA]
```

定义位置: [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c)

### 数据结构定义

全部在 [freertos/inc/data_frame.h](file:///home/alientek/Phytium/freertos/inc/data_frame.h):

| 结构体 | 用途 |
|--------|------|
| `NodeSample_t` | 节点采样数据 (有功功率、无功功率、电压) |
| `NodeUploadData_t` | 节点上传数据头 |
| `FaultUploadHeader_t` | 故障上传数据头 |
| `WaveChunkHeader_t` | 波形数据块头 |

## 4. 各环节对应文件速查

| 环节 | 步骤 | 文件路径 | 核心函数 |
|------|------|---------|---------|
| LoRa接收 | 1 | [freertos/src/lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c) | `lora_uart_init()`, `lora_uart_poll()`, `lora_uart_recv_frame()` |
| UART2 接收 | 1 | [freertos/src/lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c) | 串口接收, 115200-8N1 |
| 帧透传 | 3 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | `rpmsg_send_lora_recv_log()` |
| 任务调度 | 2-3 | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | `master_recv_task()` (每10ms读取UART) |
| Linux显示 | 4 | [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c) | `read()` → hex 打印 |
| RPMsg头 | — | [freertos/inc/master.h](file:///home/alientek/Phytium/freertos/inc/master.h) | `DEVICE_LORA_DATA 0x0023` 声明 |

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
    → chaos_init() → master_init() → master_task_create()
    → rpmsg_echo_task() → xTaskCreate(RpmsgEchoTask)
    → vTaskStartScheduler()

Step 4: RPMsg 通道建立
  RpmsgEchoTask → device_init()
    → platform_create_proc() / platform_setup_share_mems()
    → platform_create_rpmsg_vdev() →创建 VirtIO RPMsg 设备
    → rpmsg_create_ept("rpmsg-openamp-demo-channel")
  virtio_rpmsg_bus 检测到端点 → 创建 /sys/bus/rpmsg/devices/

Step 5: 绑定用户驱动
  echo rpmsg_chrdev > .../driver_override
  echo virtio0... > .../bind
  → /dev/rpmsg0 创建

Step 6: Linux 应用连接
  master_receiver.c
    → open("/dev/rpmsg_ctrl0")
    → ioctl(CREATE_EPT, "rpmsg-openamp-demo-channel")
    → open("/dev/rpmsg0")
```

## 6. 停止流程

```bash
# FreeRTOS从核 (rpmsg-echo_os.c)
echo stop > /sys/class/remoteproc/remoteproc0/state
  → shutdown_req = 1
  → RpmsgEchoTask 退出循环
  → rpmsg_destroy_ept()
  → platform_cleanup()
  → FPsciCpuOff()  → 按 remoteproc/设备树口径下电目标核（实测运行口径仍以 CPU1 为准）
```

## 7. 当前状态: LoRa 真实硬件接入

**当前模式**: 真实 LoRa UART2 链路；历史 `USE_LORA_SIMULATION = 0` 只作为早期切换说明。

```
【当前数据路径】(物理LoRa模块已接入)
GD32终端 → LoRa无线 → ATK-MWCC68D(LoRa模块) → UART2(Pin8/10)
  → FreeRTOS 主控侧（实际 CPU1，设备树写 CPU3）
    → lora_uart_poll() 读取 UART RX FIFO 字节 → 环形缓冲区
      → lora_uart_recv_frame() 帧同步(C状态机) + CRC8校验 + 组帧
        → master_recv_task → rpmsg_send_lora_recv_log() → RPMsg
          → Linux master_receiver → printf hex 显示

【回归测试】
历史仿真入口可留作回归测试，但不再作为当前主控链路事实；UKF Dashboard 当前模拟数据来自 state_new/，与 LoRa 链路尚未打通。
```

**数据流不经过**: 帧解析(parse_frame)、判决引擎(master_judge)、命令生成(master_cmd)、混沌加密。纯单向显示。

## 8. 混沌加密安全边界 (精简模式下不启用)

**注意**: 当前精简模式下数据流不经过加密/解密管线。但加密模块代码保留以备后续需要下发命令时启用。

**加密区域** = LoRa空中无线链路 (对抗电磁监听、重放攻击)

```
                            ┌──────────────┐
                            │  密文传输     │
                            │  [sync][cipher│
                            │   text...]    │
                            └──────┬───────┘
                   ┌───────────────▼──────────┐
                   │  终端节点上报 (密文)      │
                   │  LoRa TX                 │
                   └───────────────┬──────────┘
                                   │
         ┌─────────────────────────▼── FreeRTOS ───────────────┐
         │ (精简模式: 不对数据解密，直接透传原始帧到Linux)      │
         │ LoRa RX → lora_uart_recv_frame()                    │
         │   → rpmsg_send_lora_recv_log(raw_frame)             │
         │                                                     │
         │ RPMsg 透传原始密文帧                                 │
         └──────────────┬─ RPMsg (原始帧) ─────────────────────┘
                        │
         ┌──────────────▼── Linux (CPU0-2) ───────────────────┐
         │  master_receiver.c:                                  │
         │    → printf hex (原始帧，含密文数据)                  │
         └─────────────────────────────────────────────────────┘
```

**保留的加密模块**: [freertos/src/chaos_encrypt.c](file:///home/alientek/Phytium/freertos/src/chaos_encrypt.c)

## 9. 异核通信实现位置

| 层 | 位置 | 说明 |
|-----|------|------|
| FreeRTOS 应用层 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | `rpmsg_send()`, `rpmsg_endpoint_cb()`, `platform_poll()` |
| FreeRTOS OpenAMP库 | SDK内置 | `rpmsg_create_ept()`, `rpmsg_send()`, `platform_create_proc()` |
| 共享内存初始化 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) (资源表) + 设备树 | vring地址、大小定义 |
| Linux 内核驱动 | `drivers/remoteproc/homo_remoteproc.c` (飞腾定制) | `homo_rproc_kick()` 写GICv3 SGI寄存器 |
| Linux 应用层 | [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c) | `/dev/rpmsg0` read/write |

**通道数量**: 1个 (`rpmsg-openamp-demo-channel`)，双向复用，通过command字段区分消息。

## 10. LoRa RX 开关控制

通过 RPMsg 命令 `DEVICE_LORA_CTRL (0x0022)` 实时控制 LoRa 数据接收的启停。

### 10.1 协议

```
Linux → FreeRTOS (控制):
  [4B 0x0022][2B len=1][1B subcmd]
  subcmd: 0x00=STOP, 0x01=START, 0x02=QUERY

FreeRTOS → Linux (响应):
  [4B 0x0022][2B len=2][1B state][1B subcmd_echo]
  state: 0=DISABLED, 1=ENABLED
```

### 10.2 使用方式

```bash
# 方式1: CLI 工具
./demo/lora_ctrl start     # 开启 LoRa 接收
./demo/lora_ctrl stop      # 关闭 LoRa 接收
./demo/lora_ctrl status    # 查询状态
```

### 10.3 实现原理

```
FreeRTOS master_recv_task 主循环:
  while(1):
    if (!g_lora_rx_enabled):     ← DEVICE_LORA_CTRL 设置此标志
      vTaskDelay(100ms)          ← 空闲等待，停止 UART 读取
      continue
    lora_uart_poll()             ← 读取 UART2 RX FIFO 字节
    raw_len = lora_uart_recv_frame()  ← 帧提取
    if (raw_len > 0)
        rpmsg_send_lora_recv_log()  ← 透传到 Linux
```

默认上电后 `g_lora_rx_enabled = 1`（自动开始接收），可通过 Linux 命令随时关闭。关闭后不会丢失数据（终端节点仍在发送），只是 FreeRTOS 不再从 UART RX FIFO 读取字节。重新开启后恢复正常接收。