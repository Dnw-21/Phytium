# 调试日志

> **更新**: 2026-05-19 | **当前架构**: Linux主核 + FreeRTOS从核 (GD32 v3移植版，链路验证通过)
>
> 本文件记录历史调试问题和解决方案，是项目演进过程的客观记录。当前架构和通信流程见 [architecture.md](architecture.md) 和 [communication-flow.md](communication-flow.md)。

## 2026-05-11: OpenAMP 异构多核通信打通

### 问题 1: 设备树 Overlay 无法使能 OpenAMP

**现象**: 通过 `/sys/kernel/config/device-tree/overlays/` 应用 overlay 添加 `reserved-memory` 和 `homo_rproc` 节点后，驱动虽然绑定但不创建 remoteproc 设备。

**根因**: 
1. 当前内核 dtb 无 `__symbols__` 节点，overlay 编译时带 `-@` 会产生 `__symbols__`，内核检查不一致时拒绝应用 overlay
2. 即使 overlay 成功应用，`reserved-memory` 节点只在启动时被内核处理，overlay 添加的不会生效

**解决**: 不走 overlay 路线，直接修改启动用的 FIT image 中的 dtb。

### 问题 2: 平铺 DT 结构 vs 嵌套 DT 结构

**现象**: 用平铺结构 (kernel 5.10 风格) 修改 dtb 后，remoteproc 设备不创建。

```
# 平铺结构 (kernel 5.10)
homo_rproc: homo_rproc@0 {
    compatible = "homo,rproc";
    remote-processor = <3>;
    inter-processor-interrupt = <9>;
    memory-region = <&rproc>;
    firmware-name = "openamp_core0.elf";
    status = "okay";
};
```

**根因**: 内核 6.6 的 `homo_remoteproc.c` 包含 `homo_core_of_init` 函数，需要子节点 `compatible = "homo,rproc-core"`。

**解决**: 使用嵌套结构:

```
# 嵌套结构 (kernel 6.6)
homo_rproc: homo_rproc@0 {
    compatible = "homo,rproc";
    status = "okay";
    homo_core0: homo_core0@b0100000 {
        compatible = "homo,rproc-core";
        remote-processor = <3>;
        inter-processor-interrupt = <9>;
        memory-region = <&rproc>;
        firmware-name = "openamp_core0.elf";
    };
};
```

### 问题 3: FIT Image 更新

**过程**:
1. 从 `/dev/mmcblk0` 偏移 4MB 处读取当前 fitImage
2. 用 `dumpimage` 拆出内核和 dtb
3. `dtc` 解编译 dtb → 添加 OpenAMP 节点 → 重新编译
4. 用 `mkimage` + `.its` 文件重打包 fitImage
5. 用 `runtime_replace_bootloader.sh fitImage` 写回 mmc

**关键命令**:
```bash
# 提取
dumpimage -T flat_dt -p 0 -o kernel.gz fitImage
dumpimage -T flat_dt -p 1 -o board.dtb fitImage

# 修改 dtb
dtc -I dtb -O dts board.dtb > board.dts
# 编辑 dts 添加 OpenAMP 节点
dtc -I dts -O dtb board.dts > new.dtb

# 打包
mkimage -f new_fit.its new_fitImage

# 刷入
runtime_replace_bootloader.sh fitImage
```

### 问题 4: 裸机固件编译

**需要**: `aarch64-none-elf-gcc` (bare-metal, 非 Linux 版)

**下载**: ARM GNU Toolchain 13.3.Rel1 `aarch64-none-elf`

**编译**:
```bash
export AARCH64_CROSS_PATH="/path/to/toolchain"
cd phytium-standalone-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean
make all
# 输出: pe2204_aarch64_phytiumpi_openamp_core0.elf
```

### 问题 5: /dev/rpmsg0 权限

**现象**: rpmsg-demo-single 报 "Permission denied" 打开 /dev/rpmsg_ctrl0

**解决**: `sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0`

### 验证成功日志

> 注：以下内核日志中的 `CPU3` 是当时 remoteproc/设备树口径；当前实测 FreeRTOS 实际运行在 CPU1，设备树仍写 CPU3。

```
[  361.124486] remoteproc remoteproc0: powering up homo_core0
[  361.125318] remoteproc remoteproc0: Booting fw image openamp_core0.elf, size 1249064
[  361.190610] psci: CPU3 killed (polled 0 ms)
[  361.515587] virtio_rpmsg_bus virtio0: rpmsg host is online
[  361.515619] remoteproc remoteproc0: remote processor homo_core0 is now up
[  361.632578] virtio_rpmsg_bus virtio0: creating channel rpmsg-openamp-demo-channel addr 0x0
```

### 成功运行的 demo 输出

```
received message: Hello World! No:1
received message: Hello World! No:2
...
received message: Hello World! No:100
```

## 2026-05-11 (续): FreeRTOS 切换

### 问题 6: FreeRTOS SDK 编译 — symlink 路径解析

**现象**: `$(SDK_DIR)/../freertos.kconfig` 在 symlink 下 `..` 穿越到错误目录

**根因**: FreeRTOS SDK 依赖 standalone SDK（需放在 `freertos-sdk/standalone/`）。
使用 symlink 时，`standalone/../` 的 `..` 解析到物理路径（standalone SDK 根目录）而非逻辑路径（FreeRTOS SDK 根目录）。

**解决**: 不使用 symlink，直接 `cp -r` 复制 standalone SDK 到 `freertos-sdk/standalone/`。

**修复的文件**:
1. `Kconfig`: `source "$(SDK_DIR)/../freertos.kconfig"` → `source "$(FREERTOS_SDK_DIR)/freertos.kconfig"`
2. `makefile`: `FREERTOS_SDK_DIR =` → `FREERTOS_SDK_DIR := $(abspath ...)` 并 `export`
3. `tools/freertos_comonents.mk`: `FREERTOS_SDK_DIR := $(SDK_DIR)/..` → `$(abspath $(SDK_DIR)/..)`

### 问题 7: FreeRTOS 传感器数据只发1包

**现象**: `rpmsg_send` 第2次调用失败，只收到1包

**根因**: `platform_poll()` 需要 remoteproc 结构体（`&remoteproc_device_00`），但 `send_all_sensor_packets` 从回调中获得的 `priv` 不是正确的 remoteproc 指针。

**解决**: 
1. 添加全局变量 `g_remoteproc_priv` 在 `FRpmsgEchoApp` 中保存
2. `send_all_sensor_packets` 使用 `g_remoteproc_priv` 调用 `platform_poll()`

### FreeRTOS 传感器通信验证成功

```
[SEND] Requested sensor data from slave
[RECV] Waiting for 10 sensor packets...
  [PKT  1] ID= 1 ts=    0 V=220.50V A=1.25A T=27.3C [NORMAL]
  ...
  [PKT 10] ID=10 ts=  900 V=221.20V A=1.32A T=35.2C [NORMAL]
  >> [COMPLETED] Batch 1: Received 10/10 sensor packets
```
15批次 × 10包 = 150包稳定连续收发。

## 2026-05-11 (续2): A1+C2 优化实施

### A1: 批量消息合并 — 效果验证

| 指标 | 优化前(逐个) | 优化后(批量) | 提升 |
|------|-------------|-------------|------|
| 批次延迟 | 19.79ms | 0.03ms | 659× |
| 单包延迟 | 1979μs | 3.1μs | 638× |
| rpmsg_send/批 | 10次 | 1次 | 10× |
| SGI9中断/批 | 20次 | 2次 | 10× |

**实现**: FreeRTOS 将 10 个 SensorPacket 打包为一个 `DEVICE_SENSOR_BATCH` 消息，1 次 `rpmsg_send` 完成。Linux 侧一次 `read()` 解析全部 10 包。

### C2: 边缘异常检测 — 效果验证

- 阈值：电压 210-230V, 电流 0.5-2.5A, 温度 35°C(WARN)/50°C(ERROR)
- 运行 70 包中检测到 21 次异常，49 次正常
- FreeRTOS 侧完成预判，Linux 侧直接使用 status 字段

### 面板 v4 新增指标

- `optimize_speedup`: 优化加速比 (vs 逐个发送基准)
- `edge_alarms` / `edge_normal`: 边缘检测告警数/正常数

---

## 2026-05-14: GD32 主控代码移植到 FreeRTOS 从核

### 问题 8: FreeRTOS 编译错误 — rpmsg_send_master_cmd 未定义

**现象**:
```
undefined reference to `rpmsg_send_master_cmd'
```

**根因**: SDK 中的 `rpmsg-echo_os.c` 是原始版本，不包含项目自定义函数 `rpmsg_send_master_cmd()`

**解决**:
1. 从项目目录 `/home/alientek/Phytium/freertos/src/rpmsg-echo_os.c` 复制到 SDK 目录覆盖
2. 该函数通过 RPMsg 将 FreeRTOS 命令转发给 Linux 侧

**修复的文件**: `rpmsg-echo_os.c` (SDK版→项目定制版)

### 问题 9: 开发板一上电就循环打印 OPENAMP_DEVICE 日志

**现象**:
```
cpu1:OPENAMP_DEVICE:src:0x400
cpu1:OPENAMP_DEVICE:command:0x10,length:0
...（循环）
```

**根因**: FreeRTOS 从核未按 remoteproc 流程启动（当时按设备树 CPU3 口径记录；当前实测运行在 CPU1），RPMsg 设备未绑定，之前编译的固件中包含了测试用的循环打印任务。

**解决**:
1. SSH 到开发板检查状态: `cat /sys/class/remoteproc/remoteproc0/state` → offline
2. 执行启动脚本: `~/start-openamp.sh` (等价于 `echo start > /sys/class/remoteproc/remoteproc0/state`)

### 问题 10: /dev/rpmsg0 不存在

**现象**: FreeRTOS 从核启动后只有 `/dev/rpmsg_ctrl0`，没有 `/dev/rpmsg0`

**根因**: RPMsg chrdev 驱动未绑定到 channel，需要手动绑定才能创建设备节点。

**解决**:
```bash
CH=$(ls /sys/bus/rpmsg/devices/ | grep openamp-demo)
echo rpmsg_chrdev > /sys/bus/rpmsg/devices/$CH/driver_override
echo $CH > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
```

---

## 2026-05-15: RPMsg 通信链路调试

### 问题 11: master_receiver 打开 /dev/rpmsg0 失败 "Device or resource busy"

**现象**:
```
open /dev/rpmsg0: Device or resource busy
```

**根因（双重原因）**:
1. 历史板端 `dashboard_server` 进程持续占用 `/dev/rpmsg0`（非当前 `state_estimation/dashboard_server.py`）
2. `master_receiver` 未先通过 `/dev/rpmsg_ctrl0` 创建端点

**解决**:
1. 强制终止占用进程: `sudo kill -9 $(pgrep -f dashboard_server)`
2. 修改 `master_receiver.c`，添加创建端点逻辑: 先 `open(/dev/rpmsg_ctrl0)` → `ioctl(RPMSG_CREATE_EPT)` → 再 `open(/dev/rpmsg0)`

**修复的文件**: `/home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c`

### 问题 12: 数据链路无数据传输

**现象**: Linux 侧 `master_receiver` 运行后无数据，FreeRTOS 侧日志无 `g_ept` 相关的发送记录。

**根因（双重原因）**:
1. FreeRTOS 侧全局变量 `g_ept` 未初始化（创建端点后未赋值 `g_ept = &lept`），导致 `rpmsg_send_master_cmd()` 失败
2. Linux 侧未先发送握手帧触发 FreeRTOS 侧的 `g_ept` 初始化

**解决**:
1. 在 `FRpmsgEchoApp` 端点创建成功后添加: `g_ept = &lept;`
2. 在 `master_receiver` 启动时发送 `DEVICE_MASTER_DATA` 握手帧（长度0），触发 FreeRTOS 侧初始化

**修复的文件**:
- `rpmsg-echo_os.c`: 添加 `g_ept = &lept;` 初始化
- `master_receiver.c`: 添加握手帧发送

### 问题 13: 固件未更新导致修改不生效

**现象**: 编译了新固件并部署到 `/lib/firmware/` 后，重启从核发现行为未改变。

**根因**: 开发板当前运行的固件与编译生成的不一致。开发板固件 MD5: `f746cf8d...`，本地编译固件 MD5: `d5990623...`。

**解决**:
1. 卸载 rpmmsg 内核模块: `rmmod rpmsg_tty rpmsg_ctrl rpmsg_char`
2. 停止从核: `echo stop > /sys/class/remoteproc/remoteproc0/state`
3. 复制新固件: `cp new_firmware.elf /lib/firmware/openamp_core0.elf`
4. 重启从核: `echo start > /sys/class/remoteproc/remoteproc0/state`
5. 验证 MD5: `md5sum /lib/firmware/openamp_core0.elf`

### RPMsg 链路验证成功

使用 `rpmsg_ping` 测试工具发送 10 次 `DEVICE_CORE_CHECK` 命令:
```
Sent 10 DEVICE_CORE_CHECK pings, received 10 responses
Avg latency: ~1.2ms
```
→ 确认 RPMsg 双向通信链路完全通畅。

---

## 2026-05-16: 仿真数据流调试

### 问题 14: M33 核心停止失败

**现象**: `echo stop > /sys/class/remoteproc/remoteproc0/state` 返回错误，无法停止从核。

**根因**: `rpmsg` 内核模块正在使用 M 核资源（virtqueue、共享内存等），从核被模块引用计数锁定，不允许停止。

**解决**:
```bash
sudo rmmod rpmsg_tty rpmsg_ctrl rpmsg_char
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### 问题 15: 仿真数据未触发命令发送

**现象**: `master_judge_task` 检测到故障后放入命令队列，但 `master_cmd_task` 发送失败后，`wave_pending` 标志未被正确重置，导致判决任务不再重发命令。

**根因**: `master_cmd.c` 中的 `send_lora_cmd()` 函数在 RPMsg 发送失败后，没有重置 `wave_pending` 状态。`master_judge_task` 检查 `!n->wave_pending` 条件，发现为 TRUE 就不会再次向命令队列发送请求。

**解决**: 修改 `send_lora_cmd()`，在发送失败时增加 `cmd_retry` 计数，并在重试次数未达上限时重置 `wave_pending = 0`，允许判决任务下一周期重试。

**修复的文件**:
- `master_cmd.c` (SDK版): 添加重试逻辑
- `freertos/src/master_cmd.c` (项目本地): 同步修改

### 仿真数据链路初步验证

- FreeRTOS 仿真器生成 3 个节点的故障数据（过压/欠压/电压骤升）
- `master_recv_task` 接收并解析仿真帧
- `master_judge_task` 检测到 SEVERITY_DANGER 后触发命令请求
- 命令通过 RPMsg 发送到 Linux 侧
- 链路: **仿真器 → 帧解析 → 存储(共享内存) → 判决 → 命令生成 → RPMsg → Linux** 已打通

---

## 2026-05-19: 总结和 GD32 v3 移植准备

### 开发板状态

**开发板已下电**，后续上电后继续验证。当前在纯代码层进行适配。

### GD32L233C_Prj_Master_v3 新版本分析

新版本 v3 相比 v1 的主要变化：

| 方面 | v1 (原GD32) | v3 (新GD32) | Phytium 当前 |
|------|------------|------------|-------------|
| FAULT_UPLOAD_CYCLES | 10 | **2** | 2 ✅ |
| FAULT_UPLOAD_POINTS | 400 | **80** | 80 ✅ |
| WaveChunkHeader_t | 无 | **新增** | 已有 ✅ |
| CMD_WAVE_COLLECT | 无 | **新增(0x13)** | 已有 ✅ |
| Frame 格式 | [AA 55][len][ts][type][sync][enc][CRC][55 AA] | 相同 | 相同 ✅ |
| Flash 操作 | fmc_page_erase/fmc_word_program | 相同 | SHM模拟 ✅ |
| 混沌加密 | chaos_encrypt_packet | 相同 | 相同 ✅ |
| 任务优先级 | RECV=4, JUDGE=5, CMD=3 | 相同 | 相同 ✅ |
| NodeUploadData_t 增加字段 | 无 | **health_score** | 已有 ✅ |
| FaultUploadHeader_t | 无 | **新增** | 已有 ✅ |
| DATA_TYPE_FAULT_LIST(0x06) | 无 | **新增** | 已有 ✅ |

**结论**: Phytium 当前代码已经与 v3 完全兼容！所有 v3 的新增功能（2-cycle上传、WAVE_COLLECT、
FAULT_LIST、health_score等）都已在前一轮移植中实现。

### v3 移植确认的文件

经逐文件对比，以下文件已经匹配 v3：

| 文件 | v3 匹配度 | 说明 |
|------|----------|------|
| `data_frame.h` | **100%** | FAULT_UPLOAD_POINTS=80，含 WaveChunkHeader_t |
| `master.h` | **100%** | CMD_WAVE_COLLECT（0x13），所有 Flash API |
| `master_recv.c` | **95%** | 帧格式相同。仿真器当前使用 sync_code=0 明文模式（可测试用） |
| `master_judge.c` | **100%** | 逻辑一致 |
| `master_cmd.c` | **100%** | CMD_WAVE_COLLECT case 已有，发送改用 RPMsg |
| `master_sys.c` | **95%** | Flash→SHM映射相同，初始化一致 |
| `chaos_encrypt.c` | **100%** | 算法参数一致 |
| `main.c` | **90%** | 额外包含 RPMsg 初始化（Phytium 必需） |

### 待完成的代码改进（可在无板状态下完成）

1. **master_recv.c 仿真增强**: 可选添加混沌加密到仿真数据（当前 sync_code=0 明文模式也可工作）
2. **master_recv.c log 修正**: 修改日志 "10-cycle saved" → "status data saved"（v3 为 2 周期）

---
## 2026-05-19: 开发板上电验证——全链路打通

### 操作步骤

1. **开发板上电** → SSH 建立连接 → 检查状态：remoteproc `running`，RPMsg 设备已就绪
2. **同步代码**：将项目 `freertos/src/` 和 `freertos/inc/` 最新代码同步到 SDK 编译目录
3. **编译固件**：`make clean && make all` 生成 `pe2204_aarch64_phytiumpi_openamp_for_linux.elf`
4. **部署固件**：停止从核(`echo stop`)，替换 `/lib/firmware/openamp_core0.elf`，启动从核(`echo start`)
5. **绑定通道**：`rpmsg_chrdev` 绑定 `openamp-demo-channel`，创建 `/dev/rpmsg0`
6. **编译 Linux 程序**：交叉编译 `master_receiver`，部署到 `/home/user/demo/`
7. **运行测试**：停止历史板端 `dashboard_server`(PID 450)，运行 `master_receiver`

### 问题 16: `/dev/rpmsg0` Device or resource busy

**现象**: `master_receiver` / `lora_ctrl` 启动时报 `open /dev/rpmsg0: Device or resource busy`

**根因**: 历史板端 `dashboard_server` 进程已占用 `/dev/rpmsg0`

**解决**: `sudo kill -9 <PID>` 终止历史板端 dashboard_server 即可

**补充 (2026-05-19)**: 历史板端 `dashboard_server` 由 systemd 服务 `openamp.service` 管理，
直接 kill 后会被 systemd 自动重启，导致 EBUSY 持续出现。必须先停止服务再杀进程：

```bash
sudo systemctl stop openamp.service
sudo killall dashboard_server
# 如果仍然 EBUSY, 需要 unbind/rebind 通道:
sudo sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/unbind'
sleep 1
sudo sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind'
```

### 链路验证结果 ✅

运行 `master_receiver` 8 秒，成功接收到 1 条 FreeRTOS 命令：

```
[CMD #001] node=0 cmd=REQ_WAVE(ext)(0x10) params=2 [00 00]
```

**结论**: 完整链路 **仿真器 → 帧解析 → 共享内存存储 → 故障判决 → 命令生成 → RPMsg → Linux** 全部打通！

- Handshake 成功（Linux 侧 DEVICE_MASTER_DATA → FreeRTOS g_ept 初始化）
- FreeRTOS 侧 master_judge_task 检测到 SEVERITY_DANGER 故障
- 命令通过 RPMsg 正确传输到 Linux master_receiver
- 757 次读取中 756 次为空（NONBLOCK I/O 正常现象），1 次成功接收命令

---
## 2026-05-19: LoRa 真实硬件接入——精简数据链路

### 背景

原链路: **仿真器 → 帧解析 → 共享内存存储 → 故障判决 → 命令生成 → RPMsg → Linux**。

问题: 仿真阶段全链路正常，但接入真实 LoRa 硬件后终端用户只需要 **看到从终端节点接收的原始数据**，不需要主控下发命令给终端节点。启动时还看到 `[CMD #001] REQ_WAVE`，这是握手时注入的测试命令。

目标: **LoRa 数据单向展示** — GD32终端 → LoRa无线 → 飞腾派UART2 → FreeRTOS 主控侧（实际 CPU1，设备树写 CPU3）→ RPMsg → Linux 终端显示。不发送任何命令。

### 问题 17: 波特率错误 (9600 → 115200)

**现象**: 对接 LoRa 模块后收不到任何数据。

**根因**: 原 `lora_uart.c` 配置 9600，GD32 终端和 LoRa 模块通信波特率为 115200。

**解决**: 修改 `lora_uart.c` 中 UART2 PL011 波特率寄存器：

```
原值 (9600):  IBRD=651, FBRD=3
新值 (115200): IBRD=54,  FBRD=16
计算: 100MHz / (16 × 115200) = 54.2535 → IBRD=54, FBRD=16
```

### 问题 18: UART2 引脚接错

**现象**: 所有 UART (AMA0~AMA3) 读取均为 0 字节。

**根因**: LoRa 模块接线错误，未按接入指南连接。按 [lora-real-hardware-接入指南.md](lora-real-hardware-接入指南.md) 后修正:

| 飞腾派 Pin | 信号 | LoRa模块 |
|:----------:|------|:--------:|
| Pin 8 | UART2_TXD | RXD |
| Pin 10 | UART2_RXD | TXD |
| Pin 6 | GND | GND |
| Pin 1 | VCC_3.3V | VCC |
| Pin 7 | GPIO2_10 | AUX/MD0 |

**教训**: Linux 端 `stty` 设波特率无效——LoRa UART2 由 FreeRTOS 主控侧直接操作寄存器，当前实测 FreeRTOS 运行在 CPU1（设备树仍写 CPU3）。

### 问题 19: 测试命令注入 (移除)

**现象**: master_receiver 启动后立即显示 `[CMD #001] node=0 cmd=REQ_WAVE(ext)(0x10)`。

**根因**: [rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) 中 `case DEVICE_MASTER_DATA` 握手时硬编码了测试命令:
```c
/* 测试：用 rpmsg_send_master_cmd 回发测试命令 */
{
    uint8_t params[2] = {0, 0};
    int tr = rpmsg_send_master_cmd(0, 0x10, params, 2);
    (void)tr;
}
```

**解决**: 删除该代码块。握手仅保存 `g_ept` 并使能接收注入管线。

### 问题 20: 帧长度字段 bug

**现象**: [lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c) 的 `lora_uart_recv_frame()` 将长度字段写为全零。

**根因**: `SYNC_TAIL2` 状态机中 `s_state = SYNC_HDR1; s_data_len = 0;` 执行在 `buf[2]=(s_data_len>>8)` 之前，导致长度恒为 0。

**修复**:
```c
// 修复前: 先清零再用
s_state = SYNC_HDR1;
s_data_len = 0;
buf[2] = (uint8_t)(s_data_len >> 8);  // 恒为 0 ← BUG

// 修复后: 用 s_idx(=s_data_len) 写，清零在写之后
buf[2] = (uint8_t)(s_idx >> 8);
buf[3] = (uint8_t)(s_idx);
s_state = SYNC_HDR1;
s_data_len = 0;
```

### 问题 21: 数据链路精简

**目的**: 终端用户只需要看到 LoRa 接收的原始数据，不需要判决/命令/加密等管线。

**修改范围**:

| 文件 | 修改 |
|------|------|
| [rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | 删除握手时测试命令注入 (7行) |
| [master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | 移除 `master_recv_inject_data()` 调用，不再走判决管线 |
| [lora_uart.c](file:///home/alientek/Phytium/freertos/src/lora_uart.c) | 修复帧长度 bug |
| [master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c) | 重写，只显示 `DEVICE_LORA_DATA` hex 数据 |

**精简后数据路径**:
```
GD32终端 → LoRa无线 → 飞腾派UART2(Pin8/10)
  → FreeRTOS 主控侧（实际 CPU1，设备树写 CPU3） (lora_uart.c: lora_uart_poll + lora_uart_recv_frame)
    → rpmsg_send_lora_recv_log() → RPMsg DEVICE_LORA_DATA(0x0023)
      → Linux master_receiver: printf("[#N] len=%d  XX XX XX...")
```

**不发生**: 帧解析、判决引擎、命令生成、加密、命令下发。纯单向显示。

### 问题 22: master_receiver 权限问题

**现象**: `open /dev/rpmsg0: Permission denied`

**根因**: root 创建的字符设备默认仅 root 可读写。

**解决**:
```bash
sudo chmod 666 /dev/rpmsg0
```
或通过 udev 规则固化:
```bash
echo 'KERNEL=="rpmsg*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-rpmsg.rules
```

---

## 2026-05-23: FLASH_WAVE 波形数据接收与绘图

### 背景

精简链路打通后，终端节点通过 LoRa 发送各类数据帧。其中 `FLASH_WAVE` (type=0x05) 包含 int16 波形采样数据，需要**完整接收并绘图**。原方案使用固定 12000 字节缓冲区累积全部帧后一次性 dump，存在以下问题：

1. 波形数据量大（84帧×128B=10752B，后续可能更大），固定缓冲区不够用
2. 必须等全部帧收齐才输出，无法看到实时进展
3. `[WCAP]...[WCAP_END]` 格式每行固定 128 字节切割，不灵活

### 问题 23: 逐帧 hex dump 迁移 (WCAP → FW_DAT)

**目标**: 移除 `fw_cap_raw[12000]` 固定缓冲区，改为每收到一帧立即输出 hex 数据，无大小限制，支持 Python 脚本解析和绘图。

**修改文件**:

| 文件 | 变更 |
|------|------|
| [freertos/main.c](file:///home/alientek/Phytium/freertos/main.c) | 移除 `fw_cap_raw`/`fw_cap_len`/`fw_cap_active`/`fw_cap_pkts`/`FW_CAP_MAX_PKTS` 变量；FLASH_WAVE 分支改为逐帧 `[FW_DAT]` 输出 |
| [freertos/plot_wave.py](file:///home/alientek/Phytium/freertos/plot_wave.py) | 新增文件，从 `parse_wcap` 改为 `parse_fw_dat` 解析新格式，支持多波形会话、frame 边界标注、兼容旧 [WCAP] 格式 |

**新输出格式**:
```
[FW_BEG] wave#N                          ← 波形会话开始
[FW_DAT p=N ts=T len=L]                  ← 每帧标记行 (p=帧序号, ts=时间戳, len=数据长度)
HEXHEXHEX...                              ← 完整 hex (无空格, 不截断, 2字符/字节)
[FW_END] wave#N pkts=N bytes=N ...       ← 波形会话结束带汇总信息
```

**关键代码变更 (main.c)**:
```c
// 旧: 固定缓冲区累积
static u8  fw_cap_raw[12000];
static u16 fw_cap_len = 0;
if (fw_cap_active && fw_cap_len + dec_len <= sizeof(fw_cap_raw)) {
    memcpy(&fw_cap_raw[fw_cap_len], dec_buf, dec_len);
    fw_cap_len += dec_len;
}

// 新: 逐帧输出, 无缓冲
shm_spf("[FW_DAT p=%u ts=%u len=%u]\r\n", fw_seq, ts, dec_len);
for (u32 i = 0; i < dec_len; i++) {
    shm_putc(hx[(dec_buf[i] >> 4) & 0xF]);
    shm_putc(hx[dec_buf[i] & 0xF]);
}
shm_puts("\r\n");
```

**验证结果**: 成功接收 85 帧 FLASH_WAVE 数据，共 10880 字节、5440 个 int16 采样点，数据完整无截断。波形图生成成功。

### 问题 24: RPMsg TX FAIL (-2003) 分析

**现象**: trace_reader 输出中大量 `RPMsg TX FAIL (-2003)` 错误。

**根因**: 错误码 -2003 = `RPMSG_ERR_NO_BUFF`。在精简后的数据链路中，Linux 侧不再通过 `/dev/rpmsg0` 创建接收端点来消费 FreeRTOS 发送的 RPMsg 消息。`trace_reader` 通过 `/dev/mem` 直接读取共享内存（不经过 RPMsg 通道），因此 FreeRTOS 尝试通过 RPMsg 发送时找不到 Linux 侧的接收端点。

**结论**: **这是预期行为，不影响功能**。所有数据通过共享内存直接输出，`trace_reader` 可以完整读取。如果将来需要 Linux 侧程序通过 RPMsg 接收数据，需要先在 `/dev/rpmsg_ctrl0` 创建端点并绑定到 `rpmsg-openamp-demo-channel`。

### 问题 25: trace_reader 历史数据显示

**现象**: 运行 `sudo /home/user/trace_reader` 后显示大量历史数据（之前的 LoRa 初始化、AT 指令、INJECT 帧等），新波形数据混在历史中。

**根因**: 共享内存是环形缓冲区，FreeRTOS 长时间运行后累积了大量日志。`trace_reader` 启动时 dump 整个缓冲区的当前内容。

**解决**: 重启 FreeRTOS 从核清空共享内存：
```bash
echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 1
echo user | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3
```
然后再运行 `trace_reader`，输出从 FreeRTOS 启动日志开始，不再包含历史数据。

### 问题 26: SSH 断连导致 scp 静默失败

**现象**: 在开发板串口终端看到波形数据，但 `scp user@192.168.88.10:/home/user/trace_wave.txt .` 执行后本地文件为空。

**根因**: SSH 连接断开 (`No route to host`)，`scp` 静默失败导致创建了空的目标文件。

**排查方法**:
```bash
# 在开发板上检查文件
wc -l /home/user/trace_wave.txt
grep -c 'FW_DAT' /home/user/trace_wave.txt   # > 0 表示有数据

# 检查网络
ping 192.168.88.1
ip addr | grep 192.168
```

**解决**: 等待网络恢复后重新 scp；或者直接在虚拟机端用 `sshpass` 一行命令完成抓取+传输：
```bash
cd /home/alientek/Phytium/freertos
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 \
  "echo user | sudo -S timeout 60 /home/user/trace_reader 2>/dev/null" > trace_wave.txt
python3 plot_wave.py trace_wave.txt
```

### 问题 27: WAVE-STAT 数值异常 (端序问题，不影响功能)

**现象**: FreeRTOS 输出的 `[WAVE-STAT] val=[2321..32248]` 等值范围看起来不正常。

**根因**: `main.c` 中 WAVE-STAT 统计代码使用了错误的字节序解析 int16：
```c
// 当前 (Little-Endian): 错误
s16 v = (s16)((dec_buf[i * 2 + 1] << 8) | dec_buf[i * 2]);
// 应为 (Big-Endian):
s16 v = (s16)((dec_buf[i * 2] << 8) | dec_buf[i * 2 + 1]);
```

GD32 终端发送的波形数据是 Big-Endian int16。Python `plot_wave.py` 使用 `struct.unpack('>nh', ...)` 正确解析，因此**绘图结果正确，但 FreeRTOS 侧 WAVE-STAT 统计值不可信**。

**影响范围**: 仅影响 FreeRTOS 侧实时显示的最小/最大值统计，不影响 Python 绘图和实际数据。待修复优先级：低。
