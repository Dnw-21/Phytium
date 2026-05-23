# Phytium PE2204 LoRa 主控系统 — 项目信息汇总

> **更新**: 2026-05-23 | **状态**: FLASH_WAVE 波形数据接收+绘图打通 | **操作手册**: [docs/operations-guide.md](docs/operations-guide.md)

## 一、项目基本信息

| 项目 | 内容 |
|------|------|
| 项目名称 | Phytium PE2204 LoRa 主控系统 |
| 项目路径 | `/home/alientek/Phytium` |
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.11/24 |
| 用户 | user / root (密码: user / root) |

## 二、项目架构概览

```
GD32终端节点 ──LoRa无线──→ ATK-MWCC68D ──UART3──→ FreeRTOS CPU3 (飞腾派PE2204)
                                                       │
                                              共享内存 (trace_reader 读取)
                                                       │
                                              Linux终端 / SSH → 虚拟机
                                                       │
                                              plot_wave.py → waveform.png
```

**关键特性**:
- **精简单向数据链路**: 终端→LoRa→UART3→FreeRTOS→共享内存→trace_reader→绘图。不发命令、不做判决。
- **FLASH_WAVE 逐帧输出**: `[FW_DAT]` 格式，不受缓冲区大小限制，支持任意长度波形。
- **Python 绘图**: `plot_wave.py` 解析 `[FW_DAT]` 格式，支持多波形会话、Big-Endian int16。

> 详细架构见: [docs/architecture.md](docs/architecture.md)
> 操作步骤见: [docs/operations-guide.md](docs/operations-guide.md)

## 三、核心代码文件

### 3.1 FreeRTOS 从核 (freertos/)

| 文件 | 功能 |
|------|------|
| [freertos/main.c](freertos/main.c) | ★ 系统入口，FLASH_WAVE 逐帧 hex dump 输出 (`[FW_DAT]` 格式) |
| [freertos/plot_wave.py](freertos/plot_wave.py) | ★ 波形解析+绘图脚本 (解析 [FW_DAT], 生成 waveform.png) |
| [freertos/deploy.sh](freertos/deploy.sh) | ★ 一键编译+部署脚本 |
| [freertos/src/rpmsg-echo_os.c](freertos/src/rpmsg-echo_os.c) | RPMsg通信核心，OpenAMP端点 |
| [freertos/src/master_recv.c](freertos/src/master_recv.c) | LoRa帧接收管线 |
| [freertos/src/lora_uart.c](freertos/src/lora_uart.c) | UART3 PL011 驱动 (115200, 轮询) |
| [freertos/src/master_judge.c](freertos/src/master_judge.c) | 故障判决 (精简链路中不使用) |
| [freertos/src/master_cmd.c](freertos/src/master_cmd.c) | 命令生成 (精简链路中不使用) |
| [freertos/src/master_sys.c](freertos/src/master_sys.c) | 节点管理，共享内存Flash模拟(状态区+波形区) |
| [freertos/src/chaos_encrypt.c](freertos/src/chaos_encrypt.c) | 混沌加解密算法 (原GD32移植) |

### 3.2 Linux 主核 (src/)

| 文件 | 功能 |
|------|------|
| [src/openamp-demo/linux-master/master_receiver.c](src/openamp-demo/linux-master/master_receiver.c) | ★ 主控数据接收，解析 RPMsg DEVICE_MASTER_CMD |
| [src/openamp-demo/linux-master/rpmsg_master.c](src/openamp-demo/linux-master/rpmsg_master.c) | RPMsg echo 基础测试 |

### 3.3 RPMsg 消息端点

| 端点 ID | 值 | 方向 | 功能 |
|---------|-----|------|------|
| DEVICE_MASTER_DATA | 0x0020 | Linux → FreeRTOS | LoRa帧转发 |
| DEVICE_MASTER_CMD | 0x0021 | FreeRTOS → Linux | 主控指令转发/测试响应 |
| DEVICE_LORA_CTRL | 0x0022 | Linux → FreeRTOS | LoRa RX控制 (0=STOP, 1=START, 2=QUERY) |
| DEVICE_LORA_DATA | 0x0023 | FreeRTOS → Linux | LoRa收到原始帧透传显示 |
| DEVICE_MASTER_TEST | 0x0030 | Linux → FreeRTOS | 测试命令 (PING/故障注入/加密验证) |
| DEVICE_SENSOR_BATCH | 0x0011 | FreeRTOS → Linux | 传感器批量数据 |

## 四、编译工具链

### 4.1 FreeRTOS 固件编译

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

### 4.2 Linux 程序编译

```bash
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"

# 编译 master_receiver
cd /home/alientek/Phytium/src/openamp-demo
make master-recv
```

### 4.3 编译器路径速查

| 用途 | 路径 |
|------|------|
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 交叉编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |

## 五、关键源码目录

| 目录 | 说明 |
|------|------|
| `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` | FreeRTOS SDK |
| `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` | Bare-metal SDK |
| `/home/alientek/Phytium_syscode/内核源码/` | 内核源码 (5.10.209) |
| `/home/alientek/phytium-embedded-docs-master/` | 飞腾参考手册 |

## 六、部署与运行

### 当前工作流 (精简链路)

```bash
# 开发板上: 清空历史 + 启动监听
echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 1
echo user | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3
sudo /home/user/trace_reader 2>/dev/null | tee /home/user/trace_wave.txt

# 虚拟机上: 一键抓取 + 绘图
cd /home/alientek/Phytium/freertos
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo user | sudo -S timeout 60 /home/user/trace_reader 2>/dev/null" > trace_wave.txt
python3 plot_wave.py trace_wave.txt
```

### 一键编译部署

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

deploy.sh 自动完成: 同步源码 → 编译 → scp 传输 → 检查 remoteproc → 更新固件 → 验证。

### 验证命令

```bash
cat /sys/class/remoteproc/remoteproc0/state    # running/offline
grep -c FW_DAT /home/user/trace_wave.txt       # 波形数据帧数 (>0 = 收到)
dmesg | grep -i rproc                           # 启动日志
```

## 七、测试与验证

### 7.1 波形数据接收验证

```bash
# 开发板上: 清空历史 + 启动监听
echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 1
echo user | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3
sudo /home/user/trace_reader 2>/dev/null | tee /home/user/trace_wave.txt

# 看到 [FW_END] 后 Ctrl+C, 确认数据
grep -c 'FW_DAT' /home/user/trace_wave.txt    # > 0
grep 'FW_END' /home/user/trace_wave.txt       # lost=0

# 虚拟机端: 绘图
cd /home/alientek/Phytium/freertos
scp user@192.168.88.11:/home/user/trace_wave.txt .
python3 plot_wave.py trace_wave.txt
# 输出: waveform.png
```

### 7.2 验证清单

- [ ] `deploy.sh` 编译成功
- [ ] remoteproc 状态 `running`
- [ ] `trace_reader` 输出含 `[FW_BEG]` 和 `[FW_DAT]`
- [ ] FLASH_WAVE 帧 ≥ 10 帧，序号连续
- [ ] `python3 plot_wave.py` 无报错
- [ ] `waveform.png` 生成成功，波形无明显异常
- [ ] `[FW_END]` 汇总中 `lost=0`

## 八、文档索引

| 文档 | 内容 |
|------|------|
| [docs/operations-guide.md](docs/operations-guide.md) | ★ 操作手册 (自包含, 新人/AI 首选) |
| [docs/architecture.md](docs/architecture.md) | ★ 架构全景: 硬件布局、内存映射、数据流、文件索引 |
| [docs/debug-log.md](docs/debug-log.md) | 调试日志: 27 个已解决问题的完整记录 |
| [docs/freertos-task-flow.md](docs/freertos-task-flow.md) | FreeRTOS 任务流程 |
| [docs/communication-flow.md](docs/communication-flow.md) | 通信流程详解 |
| [docs/knowledge-base.md](docs/knowledge-base.md) | 知识库 |
| [docs/setup-guide.md](docs/setup-guide.md) | 部署指南 |
| [docs/optimization-record.md](docs/optimization-record.md) | 性能优化记录 |

---

**版本**: v4.0 | **状态**: FLASH_WAVE 波形接收+绘图打通 | **基于**: GD32L233C_Prj_Master_v3