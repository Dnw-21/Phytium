# GD32L233C 主控程序移植到 Phytium PE2204 FreeRTOS 记录

> **移植日期**: 2026-05-18
> **移植状态**: 代码层面完成，待硬件验证（LoRa模块连接后测试）
> **原程序**: /home/alientek/Phytium/GD32L233C_Prj_Master
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
│   ├── master_receiver (RPMsg → 接收FreeRTOS数据)
│   └── 未来: LoRa驱动 (UART设备)
│
├── FreeRTOS从核 (CPU3, 独占)
│   ├── master_recv_task (RPMsg ← LoRa帧)
│   ├── master_judge_task (故障判决)
│   ├── master_cmd_task (RPMsg → 命令转发)
│   └── 共享内存模拟Flash (状态/波形数据)
│
├── OpenAMP/RPMsg ←→ 核间通信
├── 共享内存 (0xB0100000) → Flash模拟
└── UART3 + GPIO2_10 ←→ ATK-MWCC68D LoRa模块 (未接)
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
| GD32 USART1 (LoRa) | PE2204 UART3 (J1 Pin8=TX, Pin10=RX) | 当前未接硬件，使用stub |
| GD32 GPIO (AUX) | PE2204 GPIO2_10 (J1 Pin7) | LoRa AUX/MD0控制引脚 |
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

### 4.3 LoRa通信Stub

当LoRa模块未连接时，使用RPMsg注入接口测试：
- `master_recv_inject_data()` - 从RPMsg接收LoRa帧数据
- Linux侧 `master_receiver` 可监听到 `DEVICE_MASTER_CMD` 命令

### 4.4 FreeRTOS任务配置

| 任务 | 优先级 | 栈大小 | 周期 |
|------|--------|--------|------|
| master_recv_task | 4 | 2048 | 事件驱动 |
| master_judge_task | 5 | 1024 | 500ms |
| master_cmd_task | 3 | 1024 | 事件驱动 |

## 五、设备树配置

### LoRa UART Overlay (`device-tree/lora-uart.dtso`)

```bash
# 编译并加载
dtc -@ -I dts -O dtb -o lora-uart.dtbo lora-uart.dtso
sudo dtoverlay lora-uart.dtbo
```

配置内容：
- UART3: 波特率9600, 8N1 (ATK-MWCC68D默认)
- GPIO2_10: AUX控制引脚，初始状态下拉

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

## 八、后续工作（待硬件验证）

1. **LoRa模块连接**: 将ATK-MWCC68D连接到J1接口（Pin7=AUX, Pin8=TXD, Pin10=RXD）
2. **LoRa驱动开发**: Linux侧或FreeRTOS侧实现ATK-MWCC68D的UART驱动
3. **完整数据链路测试**: LoRa终端→FreeRTOS→RPMsg→Linux 完整数据收发
4. **混沌加密同步验证**: 确认收发双方混沌状态同步正确
5. **故障判决实测**: 连接实际终端节点，验证故障检测逻辑

## 九、已知问题与注意事项

1. RPMsg通道名称 `rpmsg-openamp-demo-channel` 需要与设备树和FreeRTOS侧一致
2. 共享内存起始地址 0xB0120000 需要在SDK内存布局中未被其他模块使用
3. LoRa波特率9600需要与ATK-MWCC68D模块配置一致
4. 日志系统使用 `f_printk()` 输出到UART1（FreeRTOS控制台），波特率115200