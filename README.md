# Phytium PE2204 LoRa 主控系统

在飞腾派 CEK8903 开发板上实现 **异构多核 LoRa 主控系统**：LoRa 主控移植路线由 FreeRTOS 侧接收和处理真实终端节点数据，再传给 Linux；UKF 面板路线当前先使用仿真数据完成状态估计、故障/自然灾害预警和通知展示，后续再接入 LoRa→FreeRTOS→Linux 的真实数据链路。

> **当前状态**: LoRa 主控链路以真实硬件为目标，串口以 UART2 为准，FreeRTOS 实际运行在 CPU1（设备树中 remote processor 仍写 CPU3）。支持 **FLASH_WAVE (type=0x05) 波形数据完整接收并绘图**。精简单向数据链路：终端→LoRa→UART2→FreeRTOS→共享内存→trace_reader→Python 绘图。
> 
> **UKF 状态估计 Dashboard**: 当前唯一面板是 [state_estimation/dashboard_server.py](state_estimation/dashboard_server.py)，端口 5000；当前面板使用模拟数据，故障在 5s 和 15s 出现，尚未与 LoRa 真实链路打通。详见 [state_estimation/](state_estimation/)
>
> **操作手册**: [docs/operations-guide.md](docs/operations-guide.md) ★ **所有 AI 和开发者请先阅读此文档**
> 
> **参考基准**: `/home/alientek/Phytium/GD32L233C_Prj_Master_v3 (2)`、`/home/alientek/Phytium/GD32L233C_Prj_Master_v3_0526` 等目录是队友发送的 GD32 主控/终端节点工程，后续会继续作为飞腾派 FreeRTOS 主控移植参考。

## 项目文档导航

| 文档 | 内容 |
|------|------|
| [docs/operations-guide.md](docs/operations-guide.md) | ★ **操作手册** — 如何运行、测试、验证、绘图 (自包含) |
| [docs/architecture.md](docs/architecture.md) | ★ **架构全景图** - 硬件布局、内存映射、数据流、所有关键文件 |
| [docs/freertos-task-flow.md](docs/freertos-task-flow.md) | ★ **FreeRTOS 任务流程** - 4个任务的优先级、代码、交互 |
| [docs/debug-log.md](docs/debug-log.md) | 调试日志 — 27 个已解决问题的完整记录 |
| [docs/communication-flow.md](docs/communication-flow.md) | 通信流程详解 |
| [docs/knowledge-base.md](docs/knowledge-base.md) | 知识库 - 硬件配置、驱动架构 |
| [docs/setup-guide.md](docs/setup-guide.md) | 部署指南 |
| [docs/lora-real-hardware-接入指南.md](docs/lora-real-hardware-接入指南.md) | LoRa 硬件接线指南 |
| [docs/optimization-record.md](docs/optimization-record.md) | 性能优化记录 |

## 快速开始 — 接收波形并绘图

### 一键部署 + 抓取 + 绘图 (虚拟机端)

```bash
cd /home/alientek/Phytium/freertos

# 1. 编译部署固件 (如已是最新可跳过)
bash deploy.sh

# 2. 抓取数据并生成波形图
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo user | sudo -S timeout 60 /home/user/trace_reader 2>/dev/null" > trace_wave.txt
python3 plot_wave.py trace_wave.txt
# 输出: waveform.png (FLASH_WAVE 波形图)
```

### 开发板端监听 (带实时显示)

```bash
# 清空历史数据
echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 1
echo user | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3

# 实时显示 + 保存文件
sudo /home/user/trace_reader 2>/dev/null | tee /home/user/trace_wave.txt
# 看到 [FW_END] 后 Ctrl+C
```

详细说明见 [docs/operations-guide.md](docs/operations-guide.md)。

## 项目结构

```
Phytium/
├── README.md                           # 项目说明 (本文档)
├── PROJECT_INFO.md                     # 项目信息汇总
├── Makefile                            # 顶层构建
│
├── freertos/                           # ★ FreeRTOS 从核业务代码
│   ├── main.c                          #   系统启动入口, 任务创建, FLASH_WAVE 逐帧输出
│   ├── plot_wave.py                    # ★ 波形解析+绘图脚本 ([FW_DAT] 格式)
│   ├── deploy.sh                       # ★ 一键编译+部署脚本
│   ├── src/
│   │   ├── rpmsg-echo_os.c             #   ★ RPMsg通信核心 (OpenAMP端点)
│   │   ├── master_recv.c               #   LoRa帧接收管线
│   │   ├── master_judge.c              #   故障判决任务
│   │   └── master_cmd.c                #   命令生成/发送
│   └── inc/
│       ├── master.h                    #   主控系统定义
│       ├── data_frame.h                #   数据类型定义 (DATA_TYPE_FLASH_WAVE=0x05)
│       └── rpmsg_proto.h               #   RPMsg 协议头
│
├── docs/                               # ★ 文档
│   ├── operations-guide.md             #   ★ 操作手册 (自包含, 新人和AI首选)
│   ├── architecture.md                 #   ★ 架构全景图
│   ├── debug-log.md                    #   调试日志 (27个问题和解决方案)
│   ├── communication-flow.md           #   通信流程详解
│   ├── freertos-task-flow.md           #   FreeRTOS 任务流程
│   ├── setup-guide.md                  #   部署指南
│   ├── knowledge-base.md               #   知识库
│   └── lora-real-hardware-接入指南.md  #   LoRa 硬件接线指南
│
├── state_estimation/                   # ★ UKF 状态估计 Dashboard
│   ├── dashboard_server.py             #   Flask 服务端 + UKF 引擎
│   ├── templates/dashboard.html        #   Web 可视化面板
│   ├── ukf_estimation.py               #   UKF 算法核心
│   └── ...                             #   动态系统模型、测量数据
│
└── src/                                # Linux 侧程序 (C交叉编译)
    └── openamp-demo/linux-master/      #   master_receiver (RPMsg 接收程序)
```

## 硬件平台

| 项目 | 详情 |
|------|------|
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.11/24 |
| 用户 | user / root (密码: user / root) |

## CPU 分配

| CPU | 核心 | MPIDR | 用途 |
|-----|------|-------|------|
| CPU0 | FTC310 (LITTLE) | 0x200 | Linux SMP |
| **CPU1** | **FTC310 (LITTLE)** | **0x201** | **FreeRTOS 当前实际运行核心** |
| CPU2 | FTC664 (big) | 0x000 | Linux SMP |
| CPU3 | FTC664 (big) | 0x100 | 设备树 remote-processor 记录为 CPU3 |

> 当前调试口径：FreeRTOS 实际运行在 CPU1，但设备树/remoteproc 配置里仍写 CPU3，排查启动与中断问题时需要同时注意这两个事实。

## 通信架构

```
Linux侧接收/展示                         FreeRTOS主控侧 (实际 CPU1，设备树写 CPU3)
┌──────────────────────┐          ┌──────────────────────────┐
│  master_receiver     │          │  RpmsgEchoTask (Prio=4)   │
│  /dev/rpmsg0         │  RPMsg   │  ├─ DEVICE_MASTER_DATA ← │
│  rpmsg_char.ko       │ ←──────→ │  └─ DEVICE_MASTER_CMD  → │
│  virtio_rpmsg_bus    │  SGI 9   │                           │
│  homo_remoteproc     │          │  master_recv_task (Prio=4)│
└──────────┬───────────┘          │  master_judge_task(Prio=5)│
           │                      │  master_cmd_task  (Prio=3)│
           │      ┌───────────────┴──────────────────────────┐│
           └──────│  共享内存 0xB0100000 (409MB)              ││
                  │  vring0 + vring1 + RPMsg缓冲区 + 固件     ││
                  └──────────────────────────────────────────┘│
```

**关键答案**:
- **异核通信**: 共享内存 + GICv3 SGI 9 中断，实现位置在 `rpmsg-echo_os.c` (FreeRTOS侧) 和内核 `homo_remoteproc` 驱动 (Linux侧)
- **通道数量**: **1个** RPMsg 通道 (`rpmsg-openamp-demo-channel`)，双向复用，通过 `command` 字段区分消息类型
- **LoRa数据**: 当前 LoRa 主控路线以真实硬件链路为准，串口使用 UART2；队友会继续提供 GD32 模拟主控工程，本项目将其移植到飞腾派 FreeRTOS 侧，实现 LoRa 接收、解析、处理和分时接收等逻辑。UKF Dashboard 当前仍使用模拟数据，后续再接入 LoRa→FreeRTOS→Linux 的真实数据。

## 开发资源

| 资源 | 路径 |
|------|------|
| FreeRTOS SDK | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` |
| 裸机 SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 内核源码 (5.10) | `/home/alientek/Phytium_syscode/内核源码/` |
| 参考手册 | `/home/alientek/phytium-embedded-docs-master/` |

## 参考链接

- 飞腾嵌入式文档: https://gitee.com/phytium_embedded/phytium-embedded-docs
- OpenAMP 手册: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
- OpenAMP 官方: https://www.openampproject.org/

## 许可证

MIT License

---

**版本**: v3.2 | **更新**: 2026-05-28 | **状态**: LoRa 真实主控链路移植与 UKF 模拟数据 Dashboard 双路线推进