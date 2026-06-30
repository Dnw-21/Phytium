# GD32L233C 主控程序移植到 Phytium PE2204 FreeRTOS 记录

> **移植日期**: 2026-05-14 ~ 2026-05-28
> **移植状态**: 当前按 LoRa UART2 真实硬件链路和 FreeRTOS 实际 CPU1（设备树写 CPU3）继续移植；Dashboard 路线仍使用模拟数据，后续再打通真实链路
> **原程序 v1**: /home/alientek/Phytium/GD32L233C_Prj_Master
> **当前参考版本**: GD32L233C_Prj_Master_v3 / GD32L233C_Prj_Master_v3_0526 等队友持续交付版本
> **目标SDK**: /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master

## 一、移植概述

将GD32L233C上运行的基于FreeRTOS的LoRa主控器程序移植到飞腾派PE2204异构多核系统中。
核心业务逻辑（帧解析、混沌加解密、故障判决）移植到FreeRTOS从核，
通信接口（LoRa UART）映射到飞腾派的外设引脚，
Linux与FreeRTOS之间通过OpenAMP/RPMsg进行核间通信。

### 移植前后架构对比

```
【原GD32架构】
GD32L233C (单核)
├── FreeRTOS Scheduler
│   ├── master_recv_task (UART1 → LoRa模块直接收发)
│   ├── master_judge_task (故障判决)
│   └── master_cmd_task (命令发送)
├── 内部Flash存储 (状态/波形数据)
└── UART1 ←→ ATK-MWCC68D LoRa模块

【移植后Phytium架构】
Phytium PE2204 (异构四核)
├── Linux主核 (CPU0-2, SMP)
│   ├── master_receiver (RPMsg/共享内存 → 接收FreeRTOS数据)
│   └── 后续: 解析真实 LoRa 数据并接入 UKF Dashboard
│
├── FreeRTOS主控侧 (实际 CPU1，设备树/remoteproc 仍写 CPU3)
│   ├── master_recv_task (UART2 ← LoRa帧)
│   ├── master_judge_task (故障判决/后续处理)
│   ├── master_cmd_task (后续分时接收/命令控制)
│   └── 共享内存模拟Flash (状态/波形数据)
│
├── OpenAMP/RPMsg ←→ 核间通信
├── 共享内存 (0xB0100000) → Flash模拟
└── UART2 (J1 Pin8/10) ←→ ATK-MWCC68D LoRa模块
```

## 二、文件清单

### 2.1 FreeRTOS侧核心代码

| 文件 | 路径 | 功能 | 状态 |
|------|------|------|------|
| main.c | freertos/main.c | 系统初始化、任务创建 | 已完成 |
| rpmsg-echo_os.c | freertos/src/rpmsg-echo_os.c | RPMsg通信、master通道处理 | 已完成 |
| master_sys.c | freertos/src/master_sys.c | 主控系统、共享内存Flash模拟 | 已完成 |
| master_recv.c | freertos/src/master_recv.c | 帧接收处理、LoRa stub | 已完成 |
| master_judge.c | freertos/src/master_judge.c | 故障判决任务 | 已完成 |
| master_cmd.c | freertos/src/master_cmd.c | 命令发送（经RPMsg→Linux） | 已完成 |
| chaos_encrypt.c | freertos/src/chaos_encrypt.c | 混沌加解密算法 | 已完成 |
| log.c | freertos/src/log.c | 日志系统（适配f_printk） | 已完成 |
| data_frame.h | freertos/inc/data_frame.h | 帧数据结构定义 | 已完成 |
| chaos_encrypt.h | freertos/inc/chaos_encrypt.h | 混沌加密接口声明 | 已完成 |
| log.h | freertos/inc/log.h | 日志接口声明 | 已完成 |
| master.h | freertos/inc/master.h | 主控系统宏/数据结构 | 已完成 |

**SDK中的位置**:
`/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/`

### 2.2 Linux侧核心代码

| 文件 | 路径 | 功能 | 状态 |
|------|------|------|------|
| master_receiver.c | src/openamp-demo/linux-master/master_receiver.c | 接收FreeRTOS主控数据 | 已完成 |
| lora-uart.dtso | device-tree/lora-uart.dtso | LoRa UART设备树配置 | 已完成 |

### 2.3 GD32原始程序

| 目录 | 路径 | 说明 |
|------|------|------|
| GD32L233C_Prj_Master | GD32L233C_Prj_Master/ | GD32L233C原版完整工程（参考用） |

## 三、RPMsg通信协议

### 3.1 通道定义

| 命令码 | 通道名 | 方向 | 用途 |
|--------|--------|------|------|
| 0x0020 | DEVICE_MASTER_DATA | Linux → FreeRTOS | LoRa帧转发（Linux侧收到LoRa数据后发送给FreeRTOS处理） |
| 0x0021 | DEVICE_MASTER_CMD | FreeRTOS → Linux | 主控命令转发（FreeRTOS判决后发送命令给Linux→LoRa模块） |

### 3.2 数据格式

```c
// ProtocolData (RPMsg通用格式)
typedef struct {
    uint32_t command;      // 命令码: DEVICE_MASTER_DATA / DEVICE_MASTER_CMD
    uint16_t length;       // 数据长度
    char data[MAX_DATA_LENGTH];  // 数据负载
} ProtocolData;

// DEVICE_MASTER_DATA.payload = LoRa原始帧（含帧头、CRC等）
// DEVICE_MASTER_CMD.payload = [node_id(1B) | cmd_code(1B) | params(NB)]
```

### 3.3 命令码定义（FreeRTOS→Linux）

| 命令 | 值 | 说明 |
|------|-----|------|
| MASTER_CMD_REQ_WAVE | 0x01 | 请求终端节点上传波形数据 |
| MASTER_CMD_REQ_FAULT_LIST | 0x02 | 请求终端节点上传故障列表 |
| MASTER_CMD_CLEAR_FLASH | 0x03 | 清除终端节点Flash存储 |
| MASTER_CMD_WAVE_COLLECT | 0x04 | 终端节点采集波形 |

## 四、关键移植变更

### 4.1 硬件驱动替换

| 原GD32外设 | Phytium替代 | 说明 |
|-----------|-------------|------|
| GD32 USART1 (LoRa) | PE2204 UART2 (J1 Pin8=TX, Pin10=RX) | 当前真实 LoRa 硬件链路 |
| GD32 GPIO (AUX) | 按当前实际接线/队友工程继续移植 | LoRa AUX/MD0控制引脚 |
| GD32 内部Flash | 共享内存 (0xB0100000+offset) | 状态/波形数据存储 |

### 4.2 共享内存Flash模拟

```
#define MASTER_SHM_BASE          0xB0120000  // 共享内存基址（避开vring区域）
#define MASTER_SHM_SIZE          0x10000     // 64KB
#define MASTER_FLASH_PER_NODE    0x200       // 每节点512字节
#define MASTER_MAX_NODES         20          // 最大20个终端节点
#define FAULT_UPLOAD_POINTS      64          // 故障上传数据点数
```

功能：
- `master_flash_save_node_data()` - 保存节点状态数据
- `master_flash_save_wave_data()` - 保存波形数据
- `master_flash_erase_node_data()` - 擦除节点数据

### 4.3 LoRa 真实链路与调试注入

当前主线是真实 LoRa 模块接入 FreeRTOS UART2；RPMsg 注入仍可作为调试辅助：
- `master_recv_inject_data()` - 从RPMsg接收LoRa帧数据
- Linux侧 `master_receiver` 可监听到 `DEVICE_MASTER_CMD` 命令

### 4.4 FreeRTOS任务配置

| 任务 | 优先级 | 栈大小 | 周期 |
|------|--------|--------|------|
| master_recv_task | 4 | 2048 | 事件驱动 |
| master_judge_task | 5 | 1024 | 500ms |
| master_cmd_task | 3 | 1024 | 事件驱动 |

## 五、设备树配置

### LoRa UART 口径

当前 LoRa 串口以 **UART2 / J1 Pin8/Pin10** 为准；历史 `lora-uart.dtso`/UART3 overlay 只作为早期调试记录，不再作为当前移植路线。

当前配置要点：
- UART2: 115200, 8N1
- FreeRTOS 主控侧独占 LoRa UART2
- Linux 侧负责接收/显示/后续解析，不直接占用 LoRa 串口

## 六、编译方法

### FreeRTOS从核固件

```bash
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master
export AARCH64_CROSS_PATH=/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf
cd example/system/amp/openamp_for_linux
make clean && make
# 输出: build/pe2204_aarch64_phytiumpi_openamp_for_linux.elf
```

### Linux侧主控程序

```bash
cd /home/alientek/Phytium/src/openamp-demo
make master-recv
# 输出: build/master_receiver
```

## 七、部署运行步骤

```bash
# 1. 部署固件
sudo cp build/openamp_core0.elf /lib/firmware/

# 2. 启动remoteproc
echo start > /sys/class/remoteproc/remoteproc0/state

# 3. 绑定RPMsg驱动
echo rpmsg_chrdev > /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override

# 4. 运行Linux主控接收器
./build/master_receiver --monitor

# 5. 停止（开发完毕后）
echo stop > /sys/class/remoteproc/remoteproc0/state
```

## 八、GD32 v3 (GD32L233C_Prj_Master_v3) 兼容性分析

### 8.1 v3 vs v1 主要差异

| 方面 | v1 (原GD32) | v3 (新GD32) | Phytium移植 |
|------|------------|------------|------------|
| FAULT_UPLOAD_CYCLES | 10 | **2** | 2 ✅ |
| FAULT_UPLOAD_POINTS | 400 | **80** | 80 ✅ |
| NodeUploadData_t | 基本 | **+health_score字段** | 已含 ✅ |
| FaultUploadHeader_t | 无 | **新增(故障上传专用头)** | 已含 ✅ |
| WaveChunkHeader_t | 无 | **新增(波形上传头)** | 已含 ✅ |
| DATA_TYPE_FAULT_LIST | 无 | **新增(0x06)** | 已含 ✅ |
| CMD_WAVE_COLLECT | 无 | **新增(0x13)** | 已含 ✅ |
| MASTER_WAVE_RATE_15000 | 无 | **新增** | 已含 ✅ |

### 8.2 逐文件对比结果

| 文件 | v3 一致性 | 差异说明 |
|------|----------|---------|
| `data_frame.h` | 100% | 相同 |
| `master.h` | 100% | 相同(含Flash API) |
| `master_recv.c` | 95% | 帧格式相同。仿真用明文(sync_code=0)，v3用混沌加密 |
| `master_judge.c` | 100% | 逻辑一致 |
| `master_cmd.c` | 100% | 发送从LoRa→RPMsg(Phytium适配) |
| `master_sys.c` | 95% | 存储从Flash→SHM(Phytium适配) |
| `chaos_encrypt.c` | 100% | 算法/参数一致 |
| `main.c` | 90% | +rpmsg_echo_task(RPMsg通信必需) |

### 8.3 v3 帧协议 (与Phytium一致)

```
完整帧格式 (带帧边界标记，用于UART传输):
┌──────┬──────┬──────────────┬─────────────────────────────────────┬──────┬──────┬──────┐
│0xAA  │0x55  │ data_len(2B) │      DATA 段 (data_len 字节)       │ CRC8 │0x55  │0xAA  │
└──────┴──────┴──────────────┴─────────────────────────────────────┴──────┴──────┴──────┘
                                        │
                                        ▼
                        ┌────────┬──────┬──────────┬─────────────────┐
                        │ ts(4B) │ type │ sync(4B) │ encrypted payload│
                        └────────┴──────┴──────────┴─────────────────┘
                                               │
                              sync_code=0 ────→ 明文模式(仿真)
                              sync_code≠0 ────→ chaos_decrypt → payload
```

### 8.4 接收状态机

```
RSTATE_IDLE          → 等待 DATA_TYPE_STATUS(0x01) / DATA_TYPE_WAVE(0x02) / FAULT_LIST(0x06)
                       STATUS头(15B=FaultUploadHeader_t / 11B=NodeUploadData_t) → 设置 dl->active=1
                       WAVE头 → 擦除旧波形区 → dl->active=1
RSTATE_RECV_NODE_RAW → 累积 DATA_TYPE_NODE_RAW(0x04) int32×4 样本到 dl->node_buffer
                       满 FAULT_UPLOAD_POINTS(80点) → master_flash_save_node_data() → IDLE
RSTATE_RECV_FLASH_WAVE→ 累积 DATA_TYPE_FLASH_WAVE(0x05) int16 样本到波形区
                       满 expected_points → master_recv_wave_data() → IDLE
```

## 九、后续工作（待硬件验证）

1. **LoRa模块连接**: 将ATK-MWCC68D连接到J1接口（Pin7=AUX, Pin8=TXD, Pin10=RXD）
2. **LoRa驱动开发**: Linux侧或FreeRTOS侧实现ATK-MWCC68D的UART驱动
3. **完整数据链路测试**: LoRa终端→FreeRTOS→RPMsg→Linux 完整数据收发
4. **混沌加密同步验证**: 确认收发双方混沌状态同步正确
5. **故障判决实测**: 连接实际终端节点，验证故障检测逻辑

## 十、已知问题与注意事项

1. RPMsg通道名称 `rpmsg-openamp-demo-channel` 需要与设备树和FreeRTOS侧一致
2. 共享内存起始地址 0xB0120000 需要在SDK内存布局中未被其他模块使用
3. LoRa波特率9600需要与ATK-MWCC68D模块配置一致
4. 日志系统使用 `f_printk()` 输出到UART1（FreeRTOS控制台），波特率115200