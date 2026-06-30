# Phytium PE2204 异构多核系统

在飞腾派 CEK8903 开发板上实现 **异构多核 LoRa 主控 + UKF 状态估计 + 微电网监控大屏** 系统。

> **当前状态**: Task 1（LoRa 主控）与 Task 2（多节点 UKF）双链路并行；`dashboard_board/` 为推荐面板；`state_estimation/` 为旧版面板保留参考。
> 
> **项目信息汇总**: [PROJECT_INFO.md](PROJECT_INFO.md) — 包含完整目录结构、功能清单、RPMsg/SHM 地址、已知问题。

---

## 一、三条链路总览

| 链路 | 入口 | 当前状态 | 一句话说明 |
|------|------|----------|------------|
| **Task 1 — LoRa 主控** | [freertos/](freertos/) + [src/linux-app/rpmsg_recv.c](src/linux-app/rpmsg_recv.c) | UART2/FreeRTOS 真实硬件链路运行中；RPMsg 透传已准备但未接线；终端 24 字段数据待修复 | GD32 终端 → LoRa → UART2 → FreeRTOS → SHM 调试打印 / RPMsg(预留) → Linux |
| **Task 2 — 多节点 UKF** | [task2_linux/](task2_linux/) + [freertos/src/sim_node_*.c](freertos/src/sim_node_task.c) | 5/9/39bus 全链路跑通 | FreeRTOS RK4 仿真 → SHM → Linux UKF Pipeline |
| **Dashboard — 监控大屏** | [dashboard_board/](dashboard_board/) | 推荐版本，预计算数据驱动 | Flask + Chart.js + 飞书/微信推送 + VNC 桌面 |

---

## 二、快速开始

### 2.1 编译部署 FreeRTOS 固件（Task 1 / Task 2 共用）

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

> `deploy.sh` 会同步源码、交叉编译、scp 到开发板、检查 remoteproc、更新固件并运行 45 秒验证。

### 2.2 运行 Task 2 — 多节点 UKF

在开发板上执行：

```bash
cd /home/user/Phytium/task2_linux
./start_all.sh 1
```

> 一键完成：重启固件 → 重置 SHM → prime RPMsg endpoint → 启动三个 UKF Pipeline → 启动 FreeRTOS 仿真。
> 
> 详见 [task2_linux/README.md](task2_linux/README.md)。

### 2.3 运行 Dashboard（推荐版）

VM 端生成数据并部署：

```bash
cd /home/alientek/Phytium/dashboard_board
python3 prep/prepare_data.py
bash scripts/deploy_to_board.sh
```

浏览器访问：

```
http://192.168.88.10:5000
```

VNC 桌面：

```
192.168.88.10:5902  密码: user123
```

> 详见 [dashboard_board/README.md](dashboard_board/README.md) 和 [dashboard_board/DEPLOY.md](dashboard_board/DEPLOY.md)。

### 2.4 接收 LoRa 波形并绘图（Task 1）

```bash
# 开发板端：启动监听
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 \
  "echo user | sudo -S timeout 60 /home/user/trace_reader 2>/dev/null" > trace_wave.txt

# 虚拟机端：绘图
cd /home/alientek/Phytium/freertos
python3 plot_wave.py trace_wave.txt
# 输出: waveform.png
```

> 详见 [docs/operations-guide.md](docs/operations-guide.md)。

---

## 三、项目结构

```
Phytium/
├── README.md                  # 本文件
├── PROJECT_INFO.md            # 项目信息汇总（目录、功能、地址、问题）
├── 底层架构与通信摸底报告.md   # Phase 3 摸底报告
│
├── freertos/                  # FreeRTOS 从核业务代码（Task 1 + Task 2）
│   ├── main.c                 #   系统入口，创建 9 个任务
│   ├── deploy.sh              #   一键编译部署
│   ├── plot_wave.py           #   波形解析+绘图
│   ├── src/master_recv.c      #   LoRa 接收
│   ├── src/master_poll_task.c #   主控轮询
│   ├── src/sim_node_task.c    #   5bus 仿真
│   ├── src/sim_node_9bus.c    #   9bus 仿真
│   ├── src/sim_node_39bus.c   #   39bus 仿真
│   └── inc/                   #   头文件
│
├── task2_linux/               # Task 2 Linux 侧
│   ├── README.md
│   ├── ukf_pipeline_online.c  #   在线 UKF 主程序
│   ├── launch_ukf_multi.py    #   多节点并行启动
│   ├── start_all.sh           #   一键启动
│   └── STRESS_TEST_REPORT.md  #   压力测试报告
│
├── dashboard_board/           # 推荐版监控大屏
│   ├── README.md / DEPLOY.md
│   ├── server/dashboard_server.py
│   ├── templates/dashboard.html
│   └── scripts/deploy_to_board.sh
│
├── state_estimation/          # 旧版 UKF Dashboard（保留参考）
│
├── src/                       # Linux 侧 C 程序
│   ├── linux-app/             #   RPMsg/LoRa 验证工具
│   └── openamp-demo/          #   OpenAMP 示例与生产程序
│
└── docs/                      # 项目文档
    ├── operations-guide.md
    ├── architecture.md        # 已同步 v2.0
    ├── freertos-task-flow.md  # 已同步 v2.1
    ├── communication-flow.md  # 已同步 v3.0
    └── ...
```

---

## 四、硬件平台

| 项目 | 详情 |
|------|------|
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.10/24 |
| 用户 | user / root（密码: user / user） |

### CPU 分配

| CPU | 当前用途 | 说明 |
|-----|----------|------|
| CPU0 | Linux SMP / 5bus + 9bus UKF | A55 小核，1.5 GHz |
| **CPU1** | **FreeRTOS 实际运行核心** | A55 小核，1.5 GHz；设备树仍写 CPU3 |
| CPU2 | Linux SMP / 39bus UKF | A76 big 核，1.8 GHz |
| CPU3 | 口径不一致 | [task2_linux/README.md](task2_linux/README.md) 称离线；[底层架构与通信摸底报告.md](底层架构与通信摸底报告.md) 称可用。需按实际场景复核。 |

> 关于 CPU3 可用性的说明：Task 2 实测中 `/sys/devices/system/cpu/online` 显示 `0-2`，故当前 UKF 分配为 5bus/9bus → CPU0、39bus → CPU2。但 Phase 3 摸底报告曾在桌面环境下测得 CPU3 可用。是否启用 CPU3 需结合 `isolcpus`、remoteproc 配置与桌面进程状态综合验证。

---

## 五、关键通信接口

### 5.1 RPMsg 通道

| 通道名 | 命令 | 用途 |
|--------|------|------|
| `rpmsg-openamp-demo-channel` | `CMD_LORA_RAW 0x0023` | LoRa 原始帧透传 |
| `rpmsg-openamp-demo-channel` | `CMD_HEARTBEAT 0x0030` | 心跳 |
| `rpmsg-sim-5bus` | `CMD_SIM_CTRL 0x51` | 5bus 仿真控制 |
| `rpmsg-sim-9bus` | `0x0070` | 9bus 仿真控制（待统一） |
| `rpmsg-sim-39bus` | `0x0060` | 39bus 仿真控制（待统一） |

### 5.2 共享内存数据区

| 节点 | 基地址 | 大小 | 帧大小 |
|------|--------|------|--------|
| 5bus | 0xC8100000 | 256 KB | 64 B |
| 39bus | 0xC8140000 | 512 KB | 400 B |
| 9bus | 0xC81C0000 | 128 KB | 104 B |

---

## 六、开发资源

| 资源 | 路径 |
|------|------|
| FreeRTOS SDK | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` |
| 裸机 SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 交叉编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 参考手册 | `/home/alientek/phytium-embedded-docs-master/` |

---

## 七、当前重点任务

### 高优先级

1. **修复 GD32 终端 24 字段数据** — 仅 PG1 + timestamp 有值，其余 22 字段为 0。
2. **Task 1 RPMsg 数据路径接线** — `freertos/main.c` 已准备 `rpmsg_send_lora_raw()`，但未被业务任务调用。
3. **Dashboard 接入真实 Task 2 数据** — 当前使用预计算数据。

### 中优先级

4. **统一 FreeRTOS 侧加解密策略** — 代码与 `freertos/OPERATIONS.md` 口径不一致。
5. **处理 `rpmsg-echo_os.c` 未定义符号** — 决定废弃或补全。
6. **统一 9/39bus RPMsg 控制命令码** — 当前硬编码 `0x0070`/`0x0060`。

### 低优先级

7. 补齐/删除 `lora_uart.h` 中未实现的声明。
8. 为 `src/linux-app/` 和 `src/openamp-demo/` 编写 README。

> 完整问题清单与后续任务见 [PROJECT_INFO.md](PROJECT_INFO.md)。

---

## 八、文档导航

| 文档 | 内容 |
|------|------|
| [PROJECT_INFO.md](PROJECT_INFO.md) | 项目信息汇总、完整文件索引、功能清单、RPMsg/SHM 地址 |
| [docs/operations-guide.md](docs/operations-guide.md) | Task 1 操作手册 |
| [task2_linux/README.md](task2_linux/README.md) | Task 2 操作手册 |
| [task2_linux/STRESS_TEST_REPORT.md](task2_linux/STRESS_TEST_REPORT.md) | Task 2 压力测试报告 |
| [dashboard_board/README.md](dashboard_board/README.md) | 推荐版 Dashboard 说明 |
| [dashboard_board/DEPLOY.md](dashboard_board/DEPLOY.md) | Dashboard 部署指南 |
| [底层架构与通信摸底报告.md](底层架构与通信摸底报告.md) | Phase 3 摸底报告 |

---

## 许可证

MIT License

---

**版本**: v5.2 | **更新**: 2026-06-24 | **状态**: Task 1 + Task 2 + Dashboard 三条链路并行推进；文档与代码一致性更新
