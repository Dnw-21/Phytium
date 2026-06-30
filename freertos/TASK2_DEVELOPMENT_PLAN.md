# 任务二：FreeRTOS 多节点数据生成 → Linux 状态估计 — 开发需求计划

> 版本: v5.0 | 日期: 2026-06-09 | 状态: **✅ 三节点全链路跑通, 零错误, 帧捕获率 98%**
>
> **核心目标**: 不依赖物理 LoRa 终端，在 FreeRTOS 侧模拟多节点发电机数据，通过共享内存(SHM)传给 Linux 做 UKF 状态估计，最终实现高并发多节点压力测试，展示飞腾派 CPU 异构性能。

---

## 零、当前状态 (2026-06-09 v5 最终验证)

### 三节点运行状态 (v5)

| 节点 | 发电机 | 状态维 | 绑定核 | 帧数 | FPS | RMSE | CPU% | 状态 |
|------|:------:|:------:|:------:|------:|------:|------:|------:|------|
| 5bus (IEEE 5-Bus) | 2 | 4 | CPU0 | **11,000** | 29.6 | 0.0 | 27.0% | ✅ done |
| 39bus (IEEE 39-Bus) | 10 | 20 | CPU2 | **11,000** | 29.6 | 0.0 | 100.0% | ✅ done |
| 9bus (IEEE 9-Bus) | 3 | 6 | CPU0 | **11,000** | 29.7 | 0.0 | 28.7% | ✅ done |

**FreeRTOS 仿真**: 三节点均完成 360k 步 RK4, 降采样 32× 写入 SHM (约 11,250 帧), UKF 捕获率 ~98%

### 架构 v5 关键修复 (2026-06-09)

```
v5 核心变更 (不同于 v4):

1. SHM 地址重新分配 + Buffer 扩容:
   - 5bus:  0xC8100000 (256KB, 4095帧@64B, 原 32KB)
   - 39bus: 0xC8140000 (128KB, 327帧@400B, 原 64KB @ 0xC8108000)
   - 9bus:  0xC8160000 (128KB, 1260帧@104B, 原 32KB @ 0xC8118000)

2. FreeRTOS SHM 写降采样:
   - 三节点统一 DOWNSAMPLE=32 (原 5bus=4, 39bus=8, 9bus=40)
   - 360k steps / 32 = 11,250 帧/节点, 匹配 UKF 处理速度
   - 降采样只影响 SHM, RPMsg 批处理不受影响

3. Python launcher v3 (彻底解决 MemoryError):
   - stdout 重定向到 /tmp/ukf_out_{node}.bin (不再解析帧)
   - 移除 numpy 依赖, 移除 save_results, 移除 history 累积
   - 仅监控 stderr 心跳解析 frame count / RMSE / latency
   - 零内存压力, 三节点无 OOM

4. ukf_pipeline.c frame_sz 时序修复:
   - 将 frame_sz 读取移到 while(shm->count==0) 之后
   - 修复 FreeRTOS 初始化前读取导致 frame_sz=0→0帧的问题

5. 一键启动 + 综合诊断:
   - start_all.sh v5: 预检→杀旧进程→重载固件→reset SHM→启动 UKF→验证→启动仿真
   - status.sh: 综合诊断 (进程/CPU/SHM/metrics/logs)
   - monitor.sh: 实时资源监控 (CPU/内存/IO/中断)
```

### CPU 分配方案

```
CPU0: 5bus + 9bus UKF + Python launcher + SSH/网络 (轻量混合区)
CPU1: FreeRTOS (三节点 RK4 仿真引擎, remoteproc 独占)
CPU2: 39bus UKF (VIP 独占, 100% CPU 满载)
CPU3: 不可用 (PSCI 固件限制)
```

### 关键已知问题 (v5)

1. **UKF 必须先于 FreeRTOS 启动**: 时序错误 → SHM ring buffer 被覆盖 → NaN
2. **CPU3 不可用**: PSCI 固件限制, echo 1 > /sys/.../cpu3/online 返回 I/O error
3. **39bus UKF 100% CPU**: 39bus 完全占满 Core 2, 处理速度约 30fps (瓶颈在 UKF 计算)
4. **sudo 权限**: ukf_pipeline 需要 mmap /dev/mem, 使用 sudo -S + stdin 密码
5. **SHM 输出数据以二进制格式保存在 /tmp/ukf_out_{node}.bin (需后处理还原)**

### 标准启动流程 (v5)

```bash
# 一键启动 (在开发板上)
cd ~/Phytium/task2_linux
./start_all.sh 15       # speed=15, ~6 分钟全流程

# 另一个 SSH 终端监控
cd ~/Phytium/task2_linux && ./monitor.sh

# 综合诊断
cd ~/Phytium/task2_linux && ./status.sh
```

---

---

## 一、与任务一的关系（重要边界）

### 1.1 任务一（保持不变）

任务一是现有的 LoRa 通信链路：`GD32 终端 → LoRa → E220 → UART2 → FreeRTOS → RPMsg → Linux`。**本次开发完全不修改任务一的任何代码。**

| 任务一文件 | 说明 |
|-----------|------|
| `freertos/main.c` | LoRa 接收主程序，**只读** |
| `freertos/src/master_recv.c` | 帧解析状态机，**只读** |
| `freertos/src/master_poll_task.c` | 终端轮询，**只读** |
| `freertos/src/data_frame.c` | 帧格式/发送，**只读** |
| `freertos/src/chaos_encrypt.c` | 混沌加密，**只读** |
| `freertos/src/lora_uart.c` | UART2 中断驱动，**只读** |
| 其他 `freertos/inc/*.h`, `freertos/src/*.c` | **只读** |

### 1.2 任务二（新建，物理隔离）

任务二在**新文件/新目录**中实现，与任务一共享 RPMsg 通道但使用独立的消息类型和 endpoint。

---

## 二、分阶段目标

### 阶段 1：单节点打通（最小可行链路）🎯

**目标**: FreeRTOS 侧生成 1 个发电机节点的模拟数据 → RPMsg → Linux 侧接收 → UKF 状态估计 → 面板显示。

```
┌──────────────────────────────────────────────────────┐
│ FreeRTOS (CPU1 big核)                                 │
│                                                      │
│  ┌─────────────────────────┐                         │
│  │ sim_node_task (NEW)      │                        │
│  │ - 加载 system_params     │                        │
│  │ - 每 dt=0.0005s 生成1帧  │                        │
│  │ - 打包 RpmsgSimPkt       │                        │
│  │ - rpmsg_send() → Linux  │                         │
│  └───────────┬─────────────┘                         │
│              │ RPMsg (新增 channel 或 复用)            │
└──────────────┼──────────────────────────────────────┘
               │
┌──────────────┼──────────────────────────────────────┐
│ Linux (CPU0/2/3)                                     │
│                                                      │
│  ┌───────────▼─────────────┐                         │
│  │ sim_data_receiver (NEW)  │                        │
│  │ - 打开 /dev/rpmsgX       │                        │
│  │ - 接收数据帧              │                        │
│  │ - 写入共享内存 / 管道     │                        │
│  └───────────┬─────────────┘                         │
│              │                                        │
│  ┌───────────▼─────────────┐                         │
│  │ ukf_runner (NEW)         │                        │
│  │ - 读取 system_params.mat │                        │
│  │ - 逐帧运行 UKF 估计       │                        │
│  │ - 结果写入缓存            │                        │
│  └───────────┬─────────────┘                         │
│              │                                        │
│  ┌───────────▼─────────────┐                         │
│  │ dashboard_server (改造)  │                        │
│  │ - Web 面板显示 UKF 曲线  │                        │
│  │ - 显示 FreeRTOS→Linux    │                        │
│  │   数据传输速率            │                        │
│  └─────────────────────────┘                         │
└──────────────────────────────────────────────────────┘
```

**交付物**:
- `freertos/src/sim_node_task.c` + `freertos/inc/sim_node_task.h`
- `freertos/src/sim_data_gen.c` + `freertos/inc/sim_data_gen.h`（模拟数据生成，内嵌系统参数）
- `Phytium/task2_linux/sim_data_receiver.c`（Linux 侧 RPMsg 接收程序）
- `Phytium/task2_linux/ukf_runner.py`（UKF 状态估计运行器）
- `Phytium/task2_linux/dashboard_server_v2.py`（改造版面板）
- 修改 `freertos/main.c`（仅添加新任务创建，不修改现有代码）

### 阶段 2：多节点异构并行 🔧

**状态**: ✅ 完成 (2026-06-07)

**目标**: 同一 FreeRTOS 上同时运行不同发电机数量的节点（2-gen × 1 + 10-gen × 1 + 3-gen × 1），各自独立产生数据，Linux 侧并行做 UKF 估计。

```
FreeRTOS:
  sim_node_task (5bus/2-gen)  ──→ SHM 0xC8100000 (256KB)
  sim_node_39bus (39bus/10-gen)──→ SHM 0xC8140000 (128KB)
  sim_node_9bus (9bus/3-gen)   ──→ SHM 0xC8160000 (128KB)
  
Linux:
  ukf_pipeline --node 5bus  ──→ CPU0
  ukf_pipeline --node 39bus ──→ CPU2
  ukf_pipeline --node 9bus  ──→ CPU0
  launch_ukf_multi.py       ──→ 管理进程, 指标收集
  dashboard_server_v2.py    ──→ 三节点对比 Dashboard
```

**实际交付物**:
- `freertos/src/sim_node_task.c` (5bus)
- `freertos/src/sim_node_39bus.c` (39bus)
- `freertos/src/sim_node_9bus.c` (9bus)
- `task2_linux/ukf_pipeline.c` (C UKF, 统一处理三节点)
- `task2_linux/launch_ukf_multi.py` (多进程管理 + CPU 绑定)
- `task2_linux/dashboard_server_v2.py` (三节点对比面板)
- `task2_linux/templates/dashboard_v2.html` (前端)
- `task2_linux/start_sim_nodes.c` (RPMsg 控制)
- `task2_linux/reset_shm.c` (SHM 重置工具)

### 阶段 3：高并发压力测试 🚀

**目标**: 模拟大量节点（如 5×2-gen + 5×10-gen = 10 节点同时运行），测试 FreeRTOS 并发数据生成能力和 Linux 并行 UKF 处理能力。

```
FreeRTOS 侧:
  5 个 FreeRTOS task（每 task 内批量处理多个同类型节点）
  或 N 个 task 各处理 1 个节点

Linux 侧:
  10 个 ukf_runner 进程（multiprocessing）
  或 10 个线程（threading）
  
面板:
  实时显示:
  - 每个节点的 UKF 估计 RMSE
  - FreeRTOS CPU 利用率
  - Linux 侧各进程 CPU/内存占用
  - RPMsg 吞吐量 (bytes/s)
  - 端到端延迟
```

**交付物**:
- 压力测试框架 `task2_linux/stress_test_runner.py`
- CPU/内存监控模块 `task2_linux/resource_monitor.py`
- 面板性能监控页面
- 测试报告模板

---

## 三、技术方案详述

### 3.1 FreeRTOS 侧数据生成方案

**关键约束**: FreeRTOS 运行在 CPU1（big 核），内存有限，无硬件 FPU 限制（AArch64 有硬件 FPU）。

**方案**:
- 将 IEEE 5-bus (2-gen) 和 IEEE 39-bus (10-gen) 的系统参数**预编译为 C 数组**嵌入固件
- FreeRTOS 侧只做轻量级数据生成：从预计算的 `true_states.csv` 和 `measurements.txt` 中按时间步读取并分包发送
- **不做** RK4 积分（太耗 CPU），改为回放预计算数据

**数据格式**（RPMsg 消息）:
```c
typedef struct {
    uint8_t  node_id;       // 节点ID (0~N-1)
    uint8_t  gen_count;     // 发电机数量 (2 or 10)
    uint16_t seq;           // 序列号
    uint32_t timestamp_ms;  // 时间戳 (ms)
    float    data[40];      // 测量向量 (最大 98 维 → 分多包发送)
} SimDataPkt;  // 总大小: 8 + 40*4 = 168 字节
```

**分包策略**: 
- 2-gen 测量向量 14 维 → 56 字节数据 → 1 包
- 10-gen 测量向量 98 维 → 392 字节数据 → 3 包（每包最多 40 个 float）

### 3.2 RPMsg 通道方案

**选项 A（推荐）**: 复用现有 RPMsg 通道 `rpmsg-openamp-demo-channel`，使用新的 command code（如 `CMD_SIM_DATA = 0x50`）区分任务一和任务二的消息。

**选项 B**: 注册新的 RPMsg service name（需要修改 Linux 设备树，风险高）。

**推荐选项 A**，理由：
- 不需要修改设备树
- 不需要修改 Linux 内核驱动
- 消息头中的 command 字段即可区分

### 3.3 Linux 侧 UKF 方案

**复用现有代码**:
- `2generators/ukf_estimation_5.py` → 2-gen UKF 引擎
- `10generator/ukf_estimation_39.py` → 10-gen UKF 引擎
- `2generators/dynamic_system.py` → 动态方程
- `2generators/RK4.py` → RK4 积分器

**改造点**:
- 将文件读取改为管道/共享内存读取（接收 FreeRTOS 数据）
- 改为在线逐帧处理（而非离线批量）
- 添加进程管理（multiprocessing 支持多 UKF 实例）

### 3.4 面板方案

**基于 `state_estimation/dashboard_server.py` 改造**:
- 保留 UKF 曲线显示
- 新增多节点切换/并列视图
- 新增性能监控面板（CPU、内存、吞吐量）
- 新增 FreeRTOS ↔ Linux 数据流可视化

---

## 四、关键设计决策（✅ 已确认）

| # | 决策点 | 方案 | 理由 |
|---|--------|------|------|
| 1 | FreeRTOS 数据生成方式 | 回放预计算数据（降采样到 ~20Hz） | 以跑通链路为主，预计算数据已存在 |
| 2 | RPMsg 通道 | 新建独立 endpoint on 同 device | 不改设备树，与任务一互不干扰 |
| 3 | 系统参数存储 | 降采样数据编译为 C 数组嵌入固件 | FreeRTOS 无文件系统 |
| 4 | Linux UKF 进程模型 | multiprocessing（每节点一进程） | 利用多核 CPU，真实反映并发性能 |
| 5 | 面板框架 | Flask（复用现有） | 已有成熟面板代码，减少重复开发 |
| 6 | 性能监控 | `psutil` + `/proc/stat` | Python 标准方案 |
| 7 | 代码隔离 | 新文件在 `freertos/src/` 以 `sim_` 前缀命名 + 新建 `task2_linux/` | 编译系统自动发现，物理隔离 Linux 侧 |
| 8 | 任务一处理 | **暂停执行**，任务二跑通后再决定合并/分开 | 用户决策 |
| 9 | 阶段 1 面板 | 仅 UKF 曲线 | 先跑通核心链路 |
| 10 | 阶段 3 规模 | 先计划 10 节点，后续根据 CPU 能力调整 | 用户决策 |

---

## 五、文件结构规划

```
/home/alientek/Phytium/
├── freertos/                          ← 任务一（不改）
│   ├── main.c                         ← 仅新增 #include + xTaskCreate
│   ├── ...
│
├── task2_freertos/                    ← 任务二 FreeRTOS 侧（新建）
│   ├── sim_data_gen.c                 ← 模拟数据生成器（内嵌参数）
│   ├── sim_data_gen.h
│   ├── sim_node_task.c                ← FreeRTOS 任务：发送模拟数据
│   ├── sim_node_task.h
│   ├── sim_params_5bus.c              ← 5节点2机系统参数（编译为C数组）
│   ├── sim_params_5bus.h
│   ├── sim_params_39bus.c             ← 39节点10机系统参数（编译为C数组）
│   ├── sim_params_39bus.h
│   └── README.md
│
├── task2_linux/                       ← 任务二 Linux 侧（新建）
│   ├── sim_data_receiver.c            ← C 程序：RPMsg 接收 → 共享内存/管道
│   ├── ukf_runner.py                  ← Python：UKF 在线估计引擎
│   ├── ukf_node_manager.py            ← Python：多节点 UKF 进程管理
│   ├── resource_monitor.py            ← Python：CPU/内存监控
│   ├── dashboard_server_v2.py         ← Python：改造版 Web 面板
│   ├── templates/
│   │   └── dashboard_v2.html          ← 改造版前端面板
│   ├── stress_test_runner.py          ← Python：压力测试框架
│   └── README.md
│
├── 2generators/                       ← 参考（2机5节点 UKF 代码）
├── 10generator/                       ← 参考（10机39节点 UKF 代码）
└── state_estimation/                  ← 参考（Dashboard 面板代码）
```

---

## 六、开发顺序与时间估算

| 阶段 | 内容 | 预估工作量 | 依赖 |
|:--:|------|:--:|------|
| **1.1** | FreeRTOS 数据生成模块（回放 2-gen 数据） | 2-3天 | 无 |
| **1.2** | Linux RPMsg 接收程序 | 1-2天 | 1.1 |
| **1.3** | Linux UKF Runner（在线逐帧） | 1-2天 | 1.2 |
| **1.4** | Dashboard 基础改造（单节点显示） | 1天 | 1.3 |
| **1.5** | 阶段1 端到端集成测试 | 1天 | 1.4 |
| **2.1** | FreeRTOS 多节点支持（参数化） | 1-2天 | 1.5 |
| **2.2** | Linux 多 UKF 并行（multiprocessing） | 1-2天 | 1.5 |
| **2.3** | Dashboard 多节点切换 | 1天 | 2.2 |
| **2.4** | 阶段2 集成测试 | 1天 | 2.3 |
| **3.1** | 高并发节点扩展（5×2gen + 5×10gen） | 1-2天 | 2.4 |
| **3.2** | 资源监控模块 | 1天 | 2.4 |
| **3.3** | 性能面板（CPU/内存/吞吐量） | 1-2天 | 3.1, 3.2 |
| **3.4** | 压力测试框架 + 报告 | 1-2天 | 3.3 |
| **3.5** | 最终集成 + 文档 | 1天 | 3.4 |

**总预估**: 15-25 个工作日

---

## 七、参考资源

### 7.1 已有代码资产

| 资源 | 路径 | 用途 |
|------|------|------|
| UKF 2-gen 完整代码 | `Phytium/2generators/` | 终端+主控端，含系统参数 |
| UKF 10-gen 完整代码 | `Phytium/10generator/` | 终端+主控端，含系统参数 |
| Dashboard 面板 | `Phytium/state_estimation/dashboard_server.py` | Flask Web 面板参考 |
| FreeRTOS RPMsg 参考 | `freertos/main.c` L501-536 rpm_task | RPMsg 通信参考实现 |
| Linux RPMsg 参考 | `freertos/rpmsg_bind.c` | Linux 侧 RPMsg 打开/收发参考 |
| RPMsg 协议 | `freertos/inc/rpmsg_proto.h` | 消息结构体定义 |
| 交接文档 | `freertos/HANDOVER.md` | FreeRTOS 当前状态 |
| 调试指南 | `freertos/DEBUG_GUIDE.md` | 历史踩坑记录 |
| 操作手册 | `freertos/OPERATIONS.md` | 编译/部署/验证命令 |

### 7.2 关键技术点（来自 DEBUG_GUIDE.md 的经验）

1. **共享内存必须 MT_DEVICE_NGNRNE** — 否则 Cache 导致 Linux 读不到新数据
2. **RPMsg dest_addr 绑定** — Linux 必须先发一条消息，FreeRTOS 才能获得远端地址
3. **platform_poll 阻塞** — 多任务环境必须用 `platform_poll_nonblocking()`
4. **FMmuMap 顺序** — 必须在任何 puts/spf 之前完成
5. **stop/start remoteproc** — 不要 reboot，但注意 RCU stall 风险

---

## 八、推荐 Skill 工具

以下 skill 已确认可用于本项目开发，建议写入项目记忆文件：

| Skill | 用途 | 使用场景 |
|-------|------|---------|
| `recallloom` | 项目上下文记忆恢复 | **每次对话开始时**恢复项目状态 |
| `superpowers-writing-plans` | 编写实现计划 | 每个阶段开始前写详细计划 |
| `planning-with-files` | Manus 风格文件式任务管理 | 复杂多步骤任务的组织和追踪 |
| `code-review-excellence` | 全面代码审查 | 每次重大修改后审查 |
| `superpowers-verification-before-completion` | 完成前强制验证 | 每阶段完成后验证 |
| `superpowers-dispatching-parallel-agents` | 并行独立任务 | Linux 侧和 FreeRTOS 侧可并行开发 |
| `mattpocock-diagnose` | 问题诊断 | RPMsg 不通、数据异常等问题排查 |
| `rtk` | Token 优化 CLI | git/编译等命令减少 token 消耗 |
| `superpowers-subagent-driven-development` | 子代理驱动开发 | 独立任务并行执行 |
| `superpowers-systematic-debugging` | 系统化调试 | Bug 修复前先系统分析 |

---

## 九、确认决策（2026-06-04 审核结果）

1. ✅ **数据生成**: 回放预计算数据，降采样到约 20Hz，编译为 C 数组嵌入
2. ✅ **阶段 1 系统**: 2-gen/5-bus（IEEE 5 节点 Overbye 系统）
3. ✅ **阶段 1 面板**: 仅 UKF 曲线（转子角度 δ 和转速 ω）
4. ✅ **性能指标**: CPU 利用率、内存占用、RPMsg 吞吐量、端到端延迟、每节点 UKF 耗时
5. ✅ **3-gen 系统**: 等待用户提供，阶段 2 暂不包含
6. ✅ **RPMsg**: 复用现有 device，新建独立 endpoint，各自独立
7. ✅ **阶段 3 规模**: 先计划 10 节点（5×2gen + 5×10gen），根据实测 CPU 调整
8. ✅ **任务一**: 暂停，任务二跑通后再决定合并或分开展示

---

## 十、风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|:--:|:--:|------|
| RPMsg 吞吐量不足（2000Hz 数据） | 中 | 高 | 降低采样率（如 100Hz），或增大 RPMsg payload |
| FreeRTOS 内存不足（预计算数据大） | 低 | 中 | 分块加载、压缩存储 |
| 多任务并发导致 RPMsg 竞争 | 中 | 中 | Mutex 保护发送路径 |
| Linux multiprocessing 开销大 | 低 | 低 | 可降级为 threading |
| 任务一和任务二 RPMsg 消息冲突 | 低 | 中 | 用 cmd code 区分，独立消息队列 |

---

---

## 十一、阶段 1 详细实现方案

> 目标: FreeRTOS 产生 2-gen/5-bus 模拟数据 → RPMsg → Linux UKF 估计 → Flask 面板显示 UKF 曲线

### 11.1 核心设计：FreeRTOS 实时 RK4 生成 + 批量传输

**关键决策变更**：经代码分析确认，FreeRTOS 可以直接运行 RK4 动态仿真生成数据，不需要降采样回放。

**为什么可行**：
- `dynamic_system()` 仅有 6 行有效计算（复矩阵乘法 + 算术）
- 2-gen 系统每步 ~200 次浮点运算，2000Hz 下 CPU 占用 <0.1%
- 10-gen 系统每步 ~5000 次浮点运算，2000Hz 下 CPU 占用 ~1%
- FreeRTOS 运行在 1GHz+ AArch64 big 核，完全胜任

**频率分层设计**：
```
数据生成频率: 2000Hz (FreeRTOS RK4 步长 0.5ms, 全精度)
RPMsg 发送频率: 100Hz (每 10ms 批量发送 20 帧)
Linux UKF 频率: 2000Hz (从批量包解包后逐帧处理)
面板刷新频率: 20Hz (50ms 间隔, 人眼观看流畅)
```

### 11.2 架构总览

```
┌──────────────────────────────────────────────────────────────┐
│ FreeRTOS (CPU1 big核, 0xb0100000)                             │
│                                                              │
│  ┌──────────────────────────────────────────┐               │
│  │ sim_node_task (NEW, prio=4, stack=16KB)  │               │
│  │                                          │               │
│  │ 内嵌系统参数 (预编译C数组, ~10KB):        │               │
│  │   sim_params_5bus.h                      │               │
│  │   YBUS[2][2][3], RV[5][2][3],            │               │
│  │   E_abs[2], PM[2], M[2], D[2], X_0[4]   │               │
│  │                                          │               │
│  │ 每 0.5ms (2000Hz):                       │               │
│  │   1. rk4_step(state, dt, params)         │               │
│  │   2. h_measurement(state) → Z[14]        │               │
│  │   3. buffer_append(Z)                    │               │
│  │                                          │               │
│  │ 每 10ms (100Hz批量发送):                  │               │
│  │   1. 打包 SimDataBatch (20帧, ~1400B)     │               │
│  │   2. rpmsg_send() → Linux                │               │
│  └────────────┬─────────────────────────────┘               │
│               │ RPMsg (独立 endpoint)                         │
└───────────────┼──────────────────────────────────────────────┘
                │
┌───────────────┼──────────────────────────────────────────────┐
│ Linux (CPU0/2/3)                                             │
│                                                              │
│  ┌────────────▼──────────────────┐                           │
│  │ sim_data_bridge (C)            │                          │
│  │ - open /dev/rpmsgX             │                          │
│  │ - 解包 SimDataBatch → 20帧     │                          │
│  │ - 逐帧写入管道 /tmp/sim_fifo   │                          │
│  └────────────┬──────────────────┘                           │
│               │ /tmp/sim_fifo (named pipe)                    │
│  ┌────────────▼──────────────────┐                           │
│  │ ukf_runner_5bus.py (Python)   │                           │
│  │ - 从管道逐帧读取 (2000Hz)      │                           │
│  │ - UKF 估计: sigma点→预测→更新 │                           │
│  │ - 结果写入 multiprocessing.dict│                          │
│  └────────────┬──────────────────┘                           │
│               │ shared dict                                  │
│  ┌────────────▼──────────────────┐                           │
│  │ dashboard_server_v2.py        │                           │
│  │ - Flask :5001                 │                           │
│  │ - 每 50ms 拉取最新 UKF 结果   │                           │
│  │ - Chart.js: δ₁δ₂ / ω₁ω₂ 曲线 │                           │
│  │ - 故障区域红色阴影 (5.0-5.3s) │                           │
│  └───────────────────────────────┘                           │
└──────────────────────────────────────────────────────────────┘
```

### 11.2 数据格式设计

#### RPMsg 消息协议

```c
// 命令码（扩展现有 rpmsg_proto.h）
#define CMD_SIM_DATA    0x50   // FreeRTOS → Linux: 模拟测量数据
#define CMD_SIM_CTRL    0x51   // Linux → FreeRTOS: 控制命令 (start/stop/reset)
#define CMD_SIM_ACK     0x52   // FreeRTOS → Linux: 控制响应

// 模拟数据包（FreeRTOS → Linux）
typedef struct __attribute__((packed)) {
    uint8_t  node_id;       // 节点ID (阶段1固定=0)
    uint8_t  gen_count;     // 发电机数 (阶段1固定=2)
    uint8_t  bus_count;     // 母线数 (阶段1固定=5)
    uint8_t  flags;         // bit0=故障状态, bit1=最后包
    uint16_t seq;           // 帧序列号 (递增, 用于丢包检测)
    uint32_t ts_ms;         // 仿真时间戳 (ms, 0~180000)
    float    Z[14];         // 测量向量: PG[2], QG[2], V[5], θ[5]
} SimDataPkt;               // 总大小: 12 + 14*4 = 68 字节
```

**传输效率**: 68 字节/帧 × 20Hz = 1360 字节/秒，RPMsg 轻松承载。

#### 控制消息（Linux → FreeRTOS）
```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd;           // 0=stop, 1=start, 2=reset
    uint8_t  node_id;       // 目标节点
    uint16_t reserved;
} SimCtrlPkt;               // 4 字节
```

### 11.3 数据格式设计

#### RPMsg 批量数据包

```c
// 命令码
#define CMD_SIM_DATA    0x50   // FreeRTOS → Linux: 批量模拟测量数据
#define CMD_SIM_CTRL    0x51   // Linux → FreeRTOS: 控制命令
#define CMD_SIM_ACK     0x52   // FreeRTOS → Linux: 控制响应

#define SIM_BATCH_SIZE  20     // 每批 20 帧 (10ms ÷ 0.5ms)

// 批量数据包（FreeRTOS → Linux, 每 10ms 发送一次）
typedef struct __attribute__((packed)) {
    uint8_t  node_id;          // 节点ID
    uint8_t  gen_count;        // 发电机数
    uint8_t  bus_count;        // 母线数
    uint8_t  frame_count;      // 本批帧数 (通常=20)
    uint16_t start_seq;        // 起始序列号
    uint32_t ts_ms_start;      // 本批第一帧的仿真时间戳 (ms)
    float    Z_batch[20][14];  // 20帧 × 14维测量向量 = 1120 字节
} SimDataBatch;                // 总大小: 12 + 1120 = 1132 字节
```

**传输效率**:
- 100 批/秒 × 1132 字节 = **~113 KB/s** RPMsg 吞吐量
- RPMsg 单次传输上限 ~512 字节（受 virtqueue 限制）→ 需配置更大 buffer 或分包

**应对 virtqueue 限制的备选方案**:
```c
// 如果单包 1132B 超限，改为半批量:
#define SIM_BATCH_SIZE  10     // 每 5ms 一批, 10帧
// 包大小: 12 + 10*14*4 = 572 字节 (在 512B 边界附近)

// 或更保守:
#define SIM_BATCH_SIZE  5      // 每 2.5ms 一批, 5帧
// 包大小: 12 + 5*14*4 = 292 字节
```

> **注意**: 实际 batch_size 需在开发板上实测 RPMsg 单包上限后确定。阶段 1 先用保守值 5。

#### 控制消息（Linux → FreeRTOS）
```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd;              // 0=stop, 1=start, 2=reset, 3=speed(见data)
    uint8_t  node_id;
    uint16_t data;             // speed倍率(0=最快, 1=实时, 2=2x实时...)
} SimCtrlPkt;                  // 4 字节
```

### 11.4 FreeRTOS 侧 C 代码移植

#### 需要移植的数学函数

| Python 原函数 | C 移植 | 复杂度 | 说明 |
|:--|:--|:--|------|
| `dynamic_system()` | `sim_dynamic_deriv()` | 低 | 复矩阵乘法(2×2 @ 2×1), 可用 `complex.h` |
| `rk4_step()` | `sim_rk4_step()` | 低 | 4 次调用 deriv, 标准公式 |
| `h_measurement()` | `sim_compute_meas()` | 低 | E=I*exp(jδ), I=Ybusm@E, V=RVm@E → PG/QG/Vmag/Vangle |
| `get_system_state()` | `sim_get_fault_state()` | 极低 | 比较时间戳判断故障前/中/后 |

#### 系统参数预处理

**文件**: `task2_linux/scripts/gen_sim_params_5bus.py`
- 调用 `initialize_system_5()` 获取系统参数
- 将关键矩阵导出为 C 头文件 `sim_params_5bus.h`
- 内容包括:
```c
// sim_params_5bus.h (自动生成)
#define SIM_N_GEN      2
#define SIM_N_BUS      5
#define SIM_N_STATE    4      // 2*N_GEN
#define SIM_N_MEAS     14     // 2*N_GEN + 2*N_BUS
#define SIM_DT         0.0005f
#define SIM_FS         2000
#define SIM_TOTAL_S    180.0f
#define SIM_NUM_STEPS  360000
#define SIM_FAULT_START 5.0f
#define SIM_FAULT_END   5.3f

// 复矩阵用两个 float 数组表示 (实部/虚部分离, 避免 complex.h 兼容问题)
static const float SIM_YBUS_REAL[2][2][3] = {...};
static const float SIM_YBUS_IMAG[2][2][3] = {...};
static const float SIM_RV_REAL[5][2][3] = {...};
static const float SIM_RV_IMAG[5][2][3] = {...};
static const float SIM_E_ABS[2] = {...};
static const float SIM_PM[2] = {...};
static const float SIM_M[2] = {...};
static const float SIM_D[2] = {...};
static const float SIM_X0[4] = {...};
```

**设计决策**: 实部/虚部分离存储，避免依赖 C99 `complex.h`（交叉编译兼容性更好）。

#### sim_node_task 核心循环

```c
void sim_node_task(void *pv)
{
    // ── 1. 初始化 ──
    sim_state_t s;
    sim_init(&s);  // 加载系统参数, 初始化状态

    // ── 2. 创建 RPMsg endpoint ──
    rpmsg_create_ept(&sim_ept, g_rpdev, RPMSG_SERVICE_NAME,
                     RPMSG_ADDR_ANY, sim_ept_cb, sim_ept_unbind_cb);

    // ── 3. 等待 Linux 绑定 ──
    while (sim_ept.dest_addr == RPMSG_ADDR_ANY) {
        platform_poll_nonblocking(&rproc);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ── 4. 等待 START 命令 ──
    while (!s.running) {
        platform_poll_nonblocking(&rproc);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // ── 5. 主仿真循环 ──
    SimDataBatch batch;
    uint32_t step = 0;
    uint32_t batch_start_time = sim_get_tick_ms();

    while (s.running && step < SIM_NUM_STEPS) {
        // 确定故障状态 (切换 YBUS)
        float t = step * SIM_DT;
        uint8_t ps = sim_get_fault_state(t);
        s.ybusm_real = &SIM_YBUS_REAL[0][0][ps];
        s.ybusm_imag = &SIM_YBUS_IMAG[0][0][ps];
        s.rvm_real   = &SIM_RV_REAL[0][0][ps];
        s.rvm_imag   = &SIM_RV_IMAG[0][0][ps];

        // RK4 一步积分
        sim_rk4_step(&s, SIM_DT);

        // 计算测量向量
        float Z[SIM_N_MEAS];
        sim_compute_meas(&s, Z);

        // 写入批量缓冲
        uint8_t bi = batch.frame_count;
        memcpy(batch.Z_batch[bi], Z, sizeof(Z));
        batch.frame_count++;

        // 批量满或最后一步 → 发送
        if (batch.frame_count >= SIM_BATCH_SIZE || step == SIM_NUM_STEPS - 1) {
            batch.node_id = 0;
            batch.gen_count = SIM_N_GEN;
            batch.bus_count = SIM_N_BUS;
            batch.start_seq = step - batch.frame_count + 1;
            batch.ts_ms_start = (uint32_t)((step - batch.frame_count + 1) * SIM_DT * 1000);

            int ret = rpmsg_send(&sim_ept, &batch,
                       12 + batch.frame_count * SIM_N_MEAS * sizeof(float));
            if (ret == 0) {
                s.frames_sent += batch.frame_count;
            }
            batch.frame_count = 0;
        }

        step++;
        SHM_HB++;

        // 处理 RPMsg 控制消息
        platform_poll_nonblocking(&rproc);

        // 心跳打印 (每秒)
        if ((step % 2000) == 0) {
            shm_spf("[SIM] t=%.1fs step=%u sent=%u\n",
                    t, step, s.frames_sent);
        }
    }

    shm_spf("[SIM] done: %u frames sent\n", s.frames_sent);
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

### 11.5 实现步骤

#### Step 1: 系统参数导出脚本 (0.5天)

**文件**: `Phytium/task2_linux/scripts/gen_sim_params_5bus.py`
- 调用 `initialize_system_5()` 获取所有系统参数
- 导出 YBUS/RV/E_abs/PM/M/D/X_0 为 C 头文件
- 输出文件: `freertos/inc/sim_params_5bus.h`
- **不需要开发板**

#### Step 2: C 数学库移植 (1天)

**文件**:
- `freertos/src/sim_math.c` — 复矩阵运算、RK4、测量函数
- `freertos/inc/sim_math.h` — 接口声明

**核心函数**:
```c
// 2×2 复矩阵 × 2×1 复向量
void sim_complex_mv_mult_2x2(
    const float A_real[2][2], const float A_imag[2][2],
    const float x_real[2], const float x_imag[2],
    float y_real[2], float y_imag[2]);

// 导数函数: dx = f(t, x, params)
void sim_dynamic_deriv_5bus(const float x[4], float dx[4],
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float E_abs[2], const float PM[2],
    const float M[2], const float D[2]);

// 单步 RK4
void sim_rk4_step_5bus(float x[4], float dt, ...);

// 状态 → 测量向量
void sim_compute_meas_5bus(const float x[4], float Z[14], ...);
```

**验证**: 在主机上用 gcc 编译单元测试，对比 Python 生成的 `true_states.csv` 和 `measurements.txt`，确保 C 版本的输出与 Python 版本一致（浮点误差 < 1e-6）。
- **不需要开发板**

#### Step 3: FreeRTOS sim_node_task (1天)

**文件**:
- `freertos/src/sim_node_task.c`
- `freertos/inc/sim_node_task.h`
- `freertos/inc/sim_params_5bus.h`（Step 1 生成）
- `freertos/inc/rpmsg_proto.h` — **追加** CMD_SIM_DATA/CTRL/ACK + SimDataBatch 结构体

**依赖**: main.c 中暴露 `g_rpdev` 和 `rproc` 为 extern
- **编译检查不需要开发板，RPMsg 测试需要**

#### Step 4: 修改 main.c (0.25天)

最小化改动，仅追加 3 处:
```c
// 1. 文件顶部新增 include
#include "sim_node_task.h"

// 2. 将 rpm_task 中的局部变量提升为文件作用域
struct rpmsg_device *g_rpdev = NULL;    // 新增
struct remote_proc *g_rproc_ptr = NULL; // 新增

// 3. vTaskStartScheduler() 之前新增任务创建
#define SIM_PRIO   4
#define SIM_STK    16384    // 16KB (需要 float 缓冲)
xTaskCreate(sim_node_task, "SIM", SIM_STK, NULL, SIM_PRIO, NULL);
```

#### Step 5: Linux 侧数据桥接 (1天)

**文件**: `Phytium/task2_linux/sim_data_bridge.c`
- RPMsg 接收批量数据包
- 解包为单帧写入命名管道
- 控制命令 (START/STOP/RESET) 发给 FreeRTOS
- 编译: `gcc -O2 -o sim_data_bridge sim_data_bridge.c`
- **需要开发板测试 RPMsg**

#### Step 6: Linux 侧 UKF 运行器 (1天)

**文件**: `Phytium/task2_linux/ukf_runner_5bus.py`
- 从管道逐帧读取 (2000Hz 全速率)
- 调用 `2generators/ukf_estimation_5.py` 的 UKF 引擎
- 结果写入 `multiprocessing.Manager.dict`
- **不需要开发板，主机上可离线测试**

#### Step 7: Dashboard 面板 (0.5天)

**文件**: `Phytium/task2_linux/dashboard_server_v2.py` + `templates/dashboard_v2.html`
- Flask :5001, 仅 UKF 曲线
- 每 50ms 读取共享 dict 中的最新 UKF 结果
- Chart.js 实时渲染 δ₁ δ₂ ω₁ ω₂
- 故障区域红色阴影
- **不需要开发板**

#### Step 8: 集成测试 (1天)

在开发板上完整部署测试。
- **需要开发板上电**

### 11.6 开发板上电时间线

```
Day 1-2:  参数导出脚本 + C 数学库移植 + 单元测试      → 不需要板子
Day 3:    sim_node_task 编写 + main.c 修改             → 不需要板子 (仅交叉编译检查)
Day 3-4:  sim_data_bridge.c 编写                       → 不需要板子
Day 4:    ukf_runner_5bus.py + dashboard_server_v2.py  → 不需要板子 (主机测试)
Day 5:    开发板上电, SDK编译, RPMsg联调, 端到端集成    → 需要板子
Day 5-6:  性能调优, 问题修复                            → 需要板子
```

**总结: 约 60-70% 的开发时间不需要开发板。**

### 11.7 文件清单

| 文件 | 类型 | 说明 | 需要板子? |
|------|:--:|------|:--:|
| `task2_linux/scripts/gen_sim_params_5bus.py` | 新建 | 系统参数 → C 头文件 | ❌ |
| `freertos/inc/sim_params_5bus.h` | 新建 | 预编译系统参数 (自动生成) | ❌ |
| `freertos/inc/sim_math.h` | 新建 | 数学库接口 | ❌ |
| `freertos/src/sim_math.c` | 新建 | RK4 + 测量函数实现 | ❌ |
| `freertos/inc/sim_node_task.h` | 新建 | 任务接口 | ❌ |
| `freertos/src/sim_node_task.c` | 新建 | 仿真任务实现 | ❌→✅ |
| `freertos/inc/rpmsg_proto.h` | **修改** | 追加 SIM 协议 | ❌ |
| `freertos/main.c` | **最小修改** | 追加 include + xTaskCreate + extern | ❌→✅ |
| `task2_linux/sim_data_bridge.c` | 新建 | RPMsg 接收桥接 | ❌→✅ |
| `task2_linux/ukf_runner_5bus.py` | 新建 | UKF 在线估计 | ❌ |
| `task2_linux/dashboard_server_v2.py` | 新建 | Flask 面板 | ❌ |
| `task2_linux/templates/dashboard_v2.html` | 新建 | 前端页面 | ❌ |
| `task2_linux/README.md` | 新建 | 编译运行说明 | ❌ |

### 11.8 验证标准

```
✅ C 数学库单元测试: 与 Python 输出对比, 误差 < 1e-6
✅ FreeRTOS 编译: SDK make 通过, ELF 生成
✅ 固件部署: remoteproc start, trace_reader 看到 [SIM] 启动
✅ RPMsg 绑定: Linux 发 ECHO_REQ → [SIM] bound, dest=0xXXX
✅ 数据发送: [SIM] t=1.0s step=2000 sent=2000 (2000Hz无丢帧)
✅ UKF 接收: ukf_runner 打印每 1000 帧 RMSE
✅ 面板显示: 浏览器 :5001 显示 δ₁ δ₂ ω₁ ω₂ 实时曲线
✅ 故障标记: 5.0s-5.3s 红色阴影区域
✅ 端到端延迟: < 200ms (FreeRTOS生成 → 面板刷新)
✅ 全周期: 180 秒仿真不间断, 360,000 帧全传输
```
