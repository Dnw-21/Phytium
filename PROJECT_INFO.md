# Phytium PE2204 异构多核项目 — 项目信息汇总

> **更新**: 2026-06-24 | **状态**: Task 1 与 Task 2 双链路并行，dashboard_board 为推荐面板
> 
> **操作手册**: [docs/operations-guide.md](docs/operations-guide.md)（Task 1 口径） / [task2_linux/README.md](task2_linux/README.md)（Task 2 口径） / [dashboard_board/README.md](dashboard_board/README.md)（面板口径）

---

## 一、项目基本信息

| 项目 | 内容 |
|------|------|
| 项目名称 | Phytium PE2204 异构多核 LoRa 主控 + UKF 状态估计系统 |
| 项目路径 | `/home/alientek/Phytium` |
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.10/24 |
| 用户 | user / root（密码: user / user） |

---

## 二、项目架构概览

本项目在飞腾派 CEK8903 上实现 **异构多核** 应用，分为三条可独立运行又可最终融合的链路：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             Phytium PE2204 SoC                              │
│                                                                             │
│  ┌───────────────────────────────┐   ┌───────────────────────────────────┐ │
│  │ Linux 主核 (CPU0/CPU2, SMP)    │   │ FreeRTOS 从核 (实际 CPU1,         │ │
│  │                               │   │ 设备树 remote-processor 写 CPU3)  │ │
│  │  • master_receiver / rpmsg_recv│   │                                   │ │
│  │  • task2_linux/ukf_pipeline_*  │   │  • LoRa 主控接收 (UART2)          │ │
│  │  • dashboard_board/server      │   │  • 多节点 RK4 仿真 (5/9/39bus)    │ │
│  │  • state_estimation/ (旧面板)  │   │  • RPMsg / SHM 数据输出           │ │
│  │                               │   │                                   │ │
│  └───────────────┬───────────────┘   └───────────────┬───────────────────┘ │
│                  │                                    │                     │
│                  └──────────┬─────────────────────────┘                     │
│                             │                                               │
│                  RPMsg + 共享内存 0xB0100000 (409MB)                        │
│                  SHM 数据区 0xC8100000 / 0xC8140000 / 0xC81C0000           │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 外部接口                                                            │   │
│  │  GD32 终端节点 ──LoRa无线──→ ATK-MWCC68D ──UART2──→ FreeRTOS        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 三条链路

| 链路 | 名称 | 当前状态 | 数据流 |
|------|------|----------|--------|
| **Task 1** | LoRa 主控 | UART2/FreeRTOS 真实硬件链路运行中；RPMsg 透传已准备但未接线；终端 24 字段数据待修复 | GD32 → LoRa → UART2 → FreeRTOS → SHM 调试打印 / RPMsg(预留) → Linux |
| **Task 2** | 多节点 UKF 状态估计 | 三节点（5/9/39bus）全链路跑通，FreeRTOS RK4 仿真 → SHM → Linux UKF | FreeRTOS SIM → SHM → `ukf_pipeline_*` |
| **Dashboard** | 微电网监控大屏 | `dashboard_board/` 为推荐版本；`state_estimation/` 为旧版 | 预计算数据 / 模拟数据 → Flask → 浏览器/VNC |

---

## 三、目录结构与核心文件

```
Phytium/
├── README.md                          # 项目入口说明
├── PROJECT_INFO.md                    # 本文件：项目信息汇总
├── Makefile / CMakeLists.txt          # 顶层构建（当前次要）
│
├── freertos/                          # ★ Task 1 + Task 2 FreeRTOS 侧
│   ├── main.c                         #   系统入口，创建全部任务
│   ├── deploy.sh                      #   一键编译部署 FreeRTOS 固件
│   ├── restart_lora.sh                #   重启 openamp.service
│   ├── openamp_for_linux.config       #   FreeRTOS SDK Kconfig 配置
│   ├── plot_wave.py                   #   [FW_DAT] 波形解析+绘图
│   ├── src/
│   │   ├── rpmsg-echo_os.c            #   遗留 RPMsg echo 示例（当前未接入 main.c）
│   │   ├── master_recv.c              #   LoRa 帧接收 + 解密 + 分流
│   │   ├── master_poll_task.c         #   主控轮询命令（替代原 master_cmd）
│   │   ├── master_judge.c             #   节点在线/故障判定
│   │   ├── master_sys.c               #   节点状态表、模拟 Flash 存储
│   │   ├── lora_uart.c                #   UART2 PL011 中断驱动
│   │   ├── data_frame.c               #   LoRa 帧格式、CRC8、ACK
│   │   ├── chaos_encrypt.c            #   混沌加解密
│   │   ├── sim_node_task.c            #   5bus RK4 仿真任务
│   │   ├── sim_node_9bus.c            #   9bus RK4 仿真任务
│   │   ├── sim_node_39bus.c           #   39bus RK4 仿真任务
│   │   ├── sim_math.c                 #   5bus 数学库
│   │   ├── shm_print.c                #   共享内存调试打印
│   │   └── log.c                      #   带时间戳日志
│   └── inc/                           #   头文件（master.h、data_frame.h、rpmsg_proto.h、sim_*、shm_data.h 等）
│
├── task2_linux/                       # ★ Task 2 Linux 侧
│   ├── README.md                      #   Task 2 操作手册
│   ├── ukf_pipeline_online.c          #   在线 UKF Pipeline 主程序
│   ├── ukf_online_core.h              #   在线 UKF 核心
│   ├── launch_ukf_multi.py            #   多节点 UKF 并行启动器
│   ├── start_sim_nodes.c              #   RPMsg 仿真控制节点
│   ├── reset_shm.c / shm_print_dump.c #   SHM 工具
│   ├── dashboard_server_v2.py         #   Task 2 三节点对比面板
│   ├── board_metrics.py               #   板级资源监控
│   ├── stress_monitor.c               #   高并发压力监控
│   ├── start_all.sh / status.sh / monitor.sh / bench_*.sh  # 一键脚本
│   ├── STRESS_TEST_REPORT.md          #   阶段3压力测试报告
│   └── CODE_INDEX.md                  #   Task 2 代码索引
│
├── task2_freertos/                    #   Task 2 FreeRTOS 侧补充（ukf_core、shm_reader 等）
│
├── dashboard_board/                   # ★ 推荐版微电网 UKF Dashboard
│   ├── README.md / DEPLOY.md / 需求开发文档.md
│   ├── config.json                    #   系统配置（飞书/微信/Webhook）
│   ├── server/dashboard_server.py     #   Flask 主服务
│   ├── server/feishu_notifier.py      #   飞书推送
│   ├── server/wechat_notifier.py      #   微信推送
│   ├── server/heartbeat_source.py     #   心跳源
│   ├── templates/dashboard.html       #   Chart.js 大屏前端
│   ├── prep/prepare_data.py           #   预计算数据生成
│   ├── prep/build_data_1000hz.py      #   三节点 1000Hz 数据构建
│   ├── scripts/deploy_to_board.sh     #   一键部署到飞腾派
│   ├── scripts/start.sh / stop.sh / status.sh / start_vnc.sh
│   ├── systemd/dashboard-board.service
│   ├── systemd/dashboard-vnc.service
│   ├── data/                          #   预计算数据、节点数据
│   ├── tools/controller_online*       #   C UKF 二进制
│   └── ukf_src/                       #   C UKF 源码
│
├── state_estimation/                  #   旧版 UKF Dashboard（保留参考）
│   ├── dashboard_server.py            #   Flask 服务端 + UKF 引擎
│   ├── templates/dashboard.html
│   ├── ukf_estimation.py 等算法文件
│   └── README_使用说明.txt
│
├── src/                               #   Linux 侧 C 程序
│   ├── linux-app/                     #     直接与 LoRa/RPMsg 交互的验证工具
│   │   ├── rpmsg_recv.c               #     当前主线：RPMsg 接收 LoRa 原始帧
│   │   ├── lora_receiver.c            #     Linux 直接驱动 LoRa（历史验证路径）
│   │   ├── aux_check.c / at_test.c / hw_verify.c / uart_test.c
│   │   └── main.c / Makefile
│   └── openamp-demo/                  #     OpenAMP 示例与生产程序
│       ├── linux-master/master_receiver.c
│       ├── linux-master/rpmsg_test.c / rpmsg_ping.c / rpmsg_master.c / lora_ctrl.c
│       ├── remote-core/rpmsg_slave.c  #     早期模板，当前未使用
│       ├── Makefile / openamp.dtso / scripts/deploy.sh
│       └── docs/README.md
│
└── docs/                              #   项目文档
    ├── operations-guide.md            #   Task 1 操作手册
    ├── architecture.md                #   系统架构全景（已同步到当前代码 v2.0）
    ├── freertos-task-flow.md          #   FreeRTOS 任务流程（已同步到当前代码 v2.1）
    ├── debug-log.md                   #   调试日志（27 个问题）
    ├── communication-flow.md          #   通信流程（已同步到当前代码 v3.0）
    ├── setup-guide.md                 #   部署指南
    ├── knowledge-base.md              #   知识库
    ├── optimization-record.md         #   优化记录
    ├── lora-real-hardware-接入指南.md
    ├── transplant-gd32-to-phytium.md
    └── 调试.md / 调试_claude.md
```

---

## 四、功能与任务清单

### 4.1 Task 1 — LoRa 主控链路

| 功能 | 负责文件 | 状态 | 说明 |
|------|----------|------|------|
| UART2 LoRa 接收 | [freertos/src/lora_uart.c](freertos/src/lora_uart.c) | ✅ | 中断接收 + 环形缓冲区 |
| LoRa 帧解析 | [freertos/src/data_frame.c](freertos/src/data_frame.c) | ✅ | 帧头、长度、CRC8、ACK |
| 混沌加解密 | [freertos/src/chaos_encrypt.c](freertos/src/chaos_encrypt.c) | ⚠️ | 跨平台一致性已验证，但当前 FreeRTOS 侧解密策略与文档存在矛盾 |
| 节点状态管理 | [freertos/src/master_sys.c](freertos/src/master_sys.c) | ✅ | 模拟 Flash 存储 |
| 轮询/命令下发 | [freertos/src/master_poll_task.c](freertos/src/master_poll_task.c) | ✅ | 替代原 master_cmd.c |
| 节点在线判定 | [freertos/src/master_judge.c](freertos/src/master_judge.c) | ✅ | 15s 超时判定 |
| RPMsg 透传 | [freertos/main.c](freertos/main.c) + [src/linux-app/rpmsg_recv.c](src/linux-app/rpmsg_recv.c) | ⚠️ | `CMD_LORA_RAW 0x0023` 已准备，但 `rpmsg_send_lora_raw()` 未被调用 |
| 波形数据输出 | [freertos/main.c](freertos/main.c) | ✅ | `[FW_DAT]` 逐帧 hex dump |
| Python 绘图 | [freertos/plot_wave.py](freertos/plot_wave.py) | ✅ | 解析 `[FW_DAT]` 生成 waveform.png |
| 终端 24 字段完整数据 | GD32 终端固件 | 🔴 | 仅 PG1 + timestamp 有值，其余为 0，待排查 Keil 编译 |
| UKF 状态估计正确性 | [Quantized_Measurement_C/](Quantized_Measurement_C/) | 🔶 | 数据不完整导致 RMS 偏大 |

### 4.2 Task 2 — 多节点 UKF 状态估计

| 功能 | 负责文件 | 状态 | 说明 |
|------|----------|------|------|
| 5bus RK4 仿真 | [freertos/src/sim_node_task.c](freertos/src/sim_node_task.c) | ✅ | 2-gen / 5-bus，2000Hz，SHM 0xC8100000 |
| 9bus RK4 仿真 | [freertos/src/sim_node_9bus.c](freertos/src/sim_node_9bus.c) | ✅ | 3-gen / 9-bus，上限 2000Hz，SHM 0xC81C0000 |
| 39bus RK4 仿真 | [freertos/src/sim_node_39bus.c](freertos/src/sim_node_39bus.c) | ✅ | 10-gen / 39-bus，背压匹配 UKF，SHM 0xC8140000 |
| 5bus UKF | [task2_linux/ukf_pipeline_5bus](task2_linux/ukf_pipeline_5bus) | ✅ | 非 FT 版本，~2200 fps |
| 9bus UKF | [task2_linux/ukf_pipeline_9bus](task2_linux/ukf_pipeline_9bus) | ✅ | 非 FT 版本，~2200 fps（并发） |
| 39bus UKF | [task2_linux/ukf_pipeline_39bus_ft](task2_linux/ukf_pipeline_39bus_ft) | ✅ | FT 版本，~300 fps（并发） |
| 多进程启动器 | [task2_linux/launch_ukf_multi.py](task2_linux/launch_ukf_multi.py) | ✅ | 自动 CPU 绑定 + 指标收集 |
| 一键启动 | [task2_linux/start_all.sh](task2_linux/start_all.sh) | ✅ | 固件重启 + SHM 重置 + prime + UKF + 仿真 |
| RPMsg 控制 | [task2_linux/start_sim_nodes.c](task2_linux/start_sim_nodes.c) | ✅ | START/STOP/RESET/SPEED |
| 压力测试 | [task2_linux/stress_monitor.c](task2_linux/stress_monitor.c) + bench 脚本 | ✅ | CPU/内存/SHM/指标监控 |
| 高并发扩展（>3 节点） | — | 🔶 | 规划中，当前 3 节点已满载 CPU0/CPU2 |

### 4.3 Dashboard 与可视化

| 功能 | 负责文件 | 状态 | 说明 |
|------|----------|------|------|
| 推荐版大屏 | [dashboard_board/server/dashboard_server.py](dashboard_board/server/dashboard_server.py) | ✅ | IEEE 9-Bus，三节点轮播，故障回放，飞书/微信推送 |
| 前端面板 | [dashboard_board/templates/dashboard.html](dashboard_board/templates/dashboard.html) | ✅ | Chart.js，18 图表，灾害仿真 |
| 预计算数据 | [dashboard_board/prep/prepare_data.py](dashboard_board/prep/prepare_data.py) | ✅ | VM 端生成，SCP 部署 |
| 一键部署 | [dashboard_board/scripts/deploy_to_board.sh](dashboard_board/scripts/deploy_to_board.sh) | ✅ | VM → 飞腾派 |
| 旧版面板 | [state_estimation/dashboard_server.py](state_estimation/dashboard_server.py) | ⚠️ | 保留，依赖 state_new/，当前非推荐 |
| 接入真实 LoRa/FreeRTOS 数据 | — | 🔴 | 尚未打通，当前 Dashboard 使用预计算/模拟数据 |

---

## 五、RPMsg / SHM 端点与地址

### 5.1 RPMsg 通道

| 通道名 | 端点/命令 | 方向 | 用途 |
|--------|-----------|------|------|
| `rpmsg-openamp-demo-channel` | `CMD_LORA_RAW 0x0023` | RTOS → Linux | LoRa 原始帧透传 |
| `rpmsg-openamp-demo-channel` | `CMD_HEARTBEAT 0x0030` | RTOS → Linux | 心跳 |
| `rpmsg-openamp-demo-channel` | `CMD_ECHO_REQ/RESP 0x0040/0x0041` | 双向 | 绑定/测试 |
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | Linux ↔ RTOS | 5bus 仿真控制 |
| `rpmsg-sim-9bus` | `0x0070` | Linux ↔ RTOS | 9bus 仿真控制（硬编码，待统一） |
| `rpmsg-sim-39bus` | `0x0060` | Linux ↔ RTOS | 39bus 仿真控制（硬编码，待统一） |

### 5.2 共享内存数据区

| 节点 | 基地址 | 大小 | 帧大小 | 容量 |
|------|--------|------|--------|------|
| 5bus | 0xC8100000 | 256 KB | 64 B | 4095 帧 |
| 39bus | 0xC8140000 | 512 KB | 400 B | 1310 帧 |
| 9bus | 0xC81C0000 | 128 KB | 104 B | 1260 帧 |

> 旧版 Task2 计划中的 9bus 地址 `0xC8160000` 已废弃，当前以 `0xC81C0000` 为准。

---

## 六、编译与部署速查

### 6.1 FreeRTOS 固件

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

详细命令：
```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

### 6.2 Linux 程序

```bash
# Task 1 master_receiver
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64_aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
cd /home/alientek/Phytium/src/openamp-demo
make master-recv

# Task 2 UKF pipeline
cd /home/alientek/Phytium/task2_linux
make
```

### 6.3 Dashboard 部署

```bash
cd /home/alientek/Phytium/dashboard_board
python3 prep/prepare_data.py
bash scripts/deploy_to_board.sh
```

---

## 七、已知问题与待完成任务

### 7.1 代码/链路问题

| 问题 | 位置 | 优先级 | 说明 |
|------|------|--------|------|
| 终端 24 字段数据不全 | GD32 终端 `data_monitor.c` | 高 | 仅 PG1 + timestamp 有值，需 Keil Clean + Rebuild 验证 |
| rpmsg-echo_os.c 未定义符号 | [freertos/src/rpmsg-echo_os.c](freertos/src/rpmsg-echo_os.c) | 中 | 引用了 `master_recv_inject_data` 等未实现函数，需决定废弃或补全 |
| 加解密策略不统一 | [freertos/src/master_recv.c](freertos/src/master_recv.c) | 中 | `OPERATIONS.md` 称不解密，但代码仍调用 `chaos_decrypt_packet()` |
| 9/39bus RPMsg 命令码硬编码 | [freertos/src/sim_node_9bus.c](freertos/src/sim_node_9bus.c)、[sim_node_39bus.c](freertos/src/sim_node_39bus.c) | 低 | 建议统一到 `CMD_SIM_CTRL 0x51` |
| `lora_uart.h` 声明未实现 | [freertos/inc/lora_uart.h](freertos/inc/lora_uart.h) | 低 | `lora_uart_read_byte` 等函数未实现 |

### 7.2 文档/口径问题

| 问题 | 说明 |
|------|------|
| `docs/architecture.md` 与 `docs/freertos-task-flow.md` | 已同步到当前 9 任务架构 |
| `docs/communication-flow.md` 端点命名 | 已更新为 `CMD_LORA_RAW`，并标注数据路径未接线 |
| CPU3 可用性口径矛盾 | [底层架构与通信摸底报告.md](底层架构与通信摸底报告.md) 称 CPU3 可用；[task2_linux/README.md](task2_linux/README.md) 称 CPU3 离线。需按实际运行场景复核 |
| `src/openamp-demo/docs/README.md` 过时 | 仍描述裸机从核 echo demo，未覆盖当前主控链路 |
| `src/linux-app/` / `src/openamp-demo/` README | ✅ 已新增 README.md 说明各工具用途 |

### 7.3 后续任务

1. **Task 1**: 修复/验证 GD32 终端 24 字段数据，完成端到端 LoRa → UKF 状态估计。
2. **Task 1**: 统一加解密策略（FreeRTOS 侧透传密文 vs 跨平台一致解密）。
3. **Task 1**: 将 `rpmsg_send_lora_raw()` 接入 `master_recv_task` 或 `master_process_task`，使能 RPMsg 数据路径。
4. **Task 2**: 统一 9/39bus RPMsg 控制协议。
5. **Task 2**: 高并发扩展评估（是否需要启用 CPU3、隔离 CPU1、关闭桌面）。
6. **Dashboard**: 接入 Task 2 真实 SHM/UKF 数据，替代预计算数据。

---

## 八、文档索引

| 文档 | 内容 |
|------|------|
| [README.md](README.md) | 项目入口与快速开始 |
| [PROJECT_INFO.md](PROJECT_INFO.md) | 本文件：项目信息汇总 |
| [docs/operations-guide.md](docs/operations-guide.md) | Task 1 操作手册 |
| [docs/architecture.md](docs/architecture.md) | 系统架构全景（已同步 v2.0） |
| [docs/freertos-task-flow.md](docs/freertos-task-flow.md) | FreeRTOS 任务流程（已同步 v2.1） |
| [docs/debug-log.md](docs/debug-log.md) | 调试日志 |
| [docs/communication-flow.md](docs/communication-flow.md) | 通信流程（已同步 v3.0） |
| [docs/setup-guide.md](docs/setup-guide.md) | 部署指南 |
| [docs/knowledge-base.md](docs/knowledge-base.md) | 知识库 |
| [docs/lora-real-hardware-接入指南.md](docs/lora-real-hardware-接入指南.md) | LoRa 硬件接线 |
| [task2_linux/README.md](task2_linux/README.md) | Task 2 操作手册 |
| [task2_linux/STRESS_TEST_REPORT.md](task2_linux/STRESS_TEST_REPORT.md) | Task 2 压力测试报告 |
| [dashboard_board/README.md](dashboard_board/README.md) | 推荐版 Dashboard 说明 |
| [dashboard_board/DEPLOY.md](dashboard_board/DEPLOY.md) | Dashboard 部署指南 |
| [底层架构与通信摸底报告.md](底层架构与通信摸底报告.md) | Phase 3 摸底报告 |

---

**版本**: v6.1 | **更新**: 2026-06-24 | **状态**: 汇总了 Task 1、Task 2、Dashboard 三条链路当前状态；同步 communication-flow.md 与 RPMsg 实际状态
