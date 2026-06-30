# Task 2: FreeRTOS → Linux UKF Pipeline

> 版本: v4.0 | 日期: 2026-06-18 | 状态: 39bus/9bus/5bus 链路已跑通并在 Phytium 板实测, 无面板

## 快速开始

### 标准启动流程 (开发板上)

```bash
cd /home/user/Phytium/task2_linux

# 1. 重启 FreeRTOS 固件
echo stop > /sys/class/remoteproc/remoteproc0/state
sleep 2
echo start > /sys/class/remoteproc/remoteproc0/state
sleep 5

# 2. 重置 SHM 计数器
./reset_shm

# 3. 预绑定 RPMsg endpoint (解决 39bus/9bus 首次绑定失败问题)
./start_sim_nodes 1 &
SIM_PID=$!
sleep 6
kill -9 $SIM_PID
sleep 1

# 4. 正式启动 FreeRTOS 仿真
./start_sim_nodes 1

# 5. 在另一个终端启动 UKF Pipeline (示例: 39bus FT 版本)
taskset -c 2 env LD_LIBRARY_PATH=/home/user/Phytium/fc_lib/BLAS-FT_v1.5.0/lib:/home/user/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib ./ukf_pipeline_39bus_ft
```

> **注意**: 当前 RPMsg 存在绑定竞态。FreeRTOS 重启后, 5bus 通常能直接绑定, 但 39bus/9bus 端点需要一次 "prime" (先启动再杀掉 `start_sim_nodes`) 才能稳定绑定。步骤 3 不可省略。

### 一键启动 (使用 launch_ukf_multi.py)

```bash
./start_all.sh 1
```

> 该脚本会自动完成重启固件、重置 SHM、prime RPMsg endpoint、启动三个 UKF Pipeline、启动 FreeRTOS 仿真。

## 架构

```
FreeRTOS (CPU1, remoteproc)                  Linux (CPU0 / CPU2)
─────────────────────────                    ─────────────────────
sim_node_task (5bus)  ──→ SHM 0xC8100000    ukf_pipeline_5bus   → CPU0
sim_node_39bus (39bus)──→ SHM 0xC8140000    ukf_pipeline_39bus  → CPU2
sim_node_9bus (9bus)  ──→ SHM 0xC81C0000    ukf_pipeline_9bus   → CPU0
       │ RPMsg (控制)                              │
       └───────────────────────────────────────────┘
```

### SHM 配置

| 节点  | 基地址       | 大小   | 帧大小 | 容量(帧) |
|-------|--------------|--------|--------|----------|
| 5bus  | 0xC8100000   | 256 KB | 64 B   | 4095     |
| 39bus | 0xC8140000   | 512 KB | 400 B  | 1310     |
| 9bus  | 0xC81C0000   | 128 KB | 104 B  | 1260     |

> 39bus 共享内存 512KB, 每帧 400B, 可存放约 **1310 个样本点**。

### FreeRTOS 数据生成策略

三个仿真任务均采用 "背压 + 批量休眠" 策略, 避免 SHM 爆满:

- **5bus**: `sim_node_task.c`, 修正为 `DT=0.0005s`/`FS=2000Hz`, 与 Linux UKF 对齐。
  `sim5_speed=1` 时每 8 帧休眠 4ms, 目标 **2000 fps**。
- **39bus**: `sim_node_39bus.c`, `sim39_speed=1` 时每 8 帧休眠 16ms, 目标 **500 fps**;
  `sim39_speed>=2` 时每 8 帧休眠 8ms, 目标 **1000 fps**。
- **9bus**: `sim_node_9bus.c`, 每帧写入 SHM, `sim9_speed=1` 时每 8 帧休眠 4ms, 目标 **2000 fps**;
  `sim9_speed>=2` 时每 8 帧休眠 2ms, 目标 **4000 fps**, 实际由背压限制在 UKF 消费能力附近。

写入 SHM 前统一检查 `(cnt - ri) < cap`, 若 SHM 满则阻塞等待 1ms (背压)。
Linux 侧 UKF 每处理完一帧会更新 `shm->ri`, FreeRTOS 据此判断已消费并继续写入。

## 文件说明

| 文件 | 语言 | 说明 |
|------|:--:|------|
| `ukf_pipeline_online.c` | C | 在线 UKF Pipeline 主程序 |
| `ukf_online_core.h` | C | UKF 核心算法 (支持 FT 加速) |
| `sim_params_39bus.h` | C | 39bus 仿真参数 |
| `ukf_pipeline_39bus` | 二进制 | 39bus 非 FT 版本 |
| `ukf_pipeline_39bus_ft` | 二进制 | 39bus FT 版本 (依赖 BLAS-FT / LAPACK-FT) |
| `start_sim_nodes.c` | C | RPMsg 仿真控制节点 |
| `start_sim_nodes` / `start_sim_nodes_arm64` | 二进制 | 多节点仿真启动器 |
| `reset_shm.c` | C | 重置 SHM 计数器 |
| `reset_shm` | 二进制 | SHM 重置工具 |
| `launch_ukf_multi.py` | Python | 并行启动 3 个 UKF 进程并收集指标 |
| `start_all.sh` | Shell | 一键启动脚本 |
| `bench_39bus_compare.sh` | Shell | 39bus FT vs 非 FT 性能测试 |
| `bench_5bus_compare.sh` | Shell | 5bus FT vs 非 FT 性能测试 |
| `bench_9bus.sh` | Shell | 9bus FT vs 非 FT 性能测试 (追求最高频率) |
| `shm_print_dump.c` | C | 读取 FreeRTOS SHM 调试打印缓冲区 |

## 通信协议

- 数据通道: SHM ring buffer (每节点独立地址, 见上表)
- 控制通道: RPMsg
  - 5bus:  channel `rpmsg-sim-5bus`,  dst=0
  - 39bus: channel `rpmsg-sim-39bus`, dst=10
  - 9bus:  channel `rpmsg-sim-9bus`,  dst=20
- 命令码: 0x51 (CTRL), 0x60 (SIM39_CTRL), 0x70 (SIM9_CTRL), 0x53 (DONE)
- 控制命令: START=1, STOP=0, SPEED=3

## FT 版本运行

39bus FT 版本动态链接 BLAS-FT / LAPACK-FT, 运行前需设置:

```bash
export LD_LIBRARY_PATH=/home/user/Phytium/fc_lib/BLAS-FT_v1.5.0/lib:/home/user/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib
```

## 调试

### 查看 FreeRTOS 调试输出

```bash
./shm_print_dump
```

### 查看 SHM 计数

```bash
busybox devmem 0xC8140008 32   # 39bus CNT
busybox devmem 0xC81C0008 32   # 9bus  CNT
busybox devmem 0xC8100008 32   # 5bus  CNT
```

### 查看 RPMsg 设备

```bash
ls /sys/bus/rpmsg/devices/
```

### 常见问题

1. **39bus/9bus SHM count 为 0**
   - 原因: RPMsg endpoint 绑定竞态, FreeRTOS 端点未收到 Linux 的绑定通知。
   - 解决: 做一次 prime (先启动 `start_sim_nodes` 6s 后杀掉, 再重新启动)。

2. **UKF 输出 NaN**
   - 原因: FreeRTOS 任务未启用 FPU 上下文, 上下文切换时浮点寄存器损坏。
   - 解决: 确保 `sim_node_39bus_task` / `sim_node_task` / `sim_node_9bus_task` 开头调用 `vPortTaskUsesFPU()`。

3. **FT 版本无法加载 libblas.so.1**
   - 原因: `LD_LIBRARY_PATH` 未包含 FT 库路径。
   - 解决: 按上面设置 `LD_LIBRARY_PATH`。

4. **`bench_all_nodes.sh` 中 39bus 帧数解析为 0**
   - 原因: 39bus 处理速度慢, 30s 后会被 SIGKILL, 没有输出 `done:` 行; 脚本只从 `done:` 行解析 frames。
   - 解决: 修改 `parse_log()`, 当没有 `done:` 行时从最后心跳行提取 frames, 并用 SHM count 差值估算生成频率。

5. **单独测试 5bus/9bus 时 SHM count 始终为 0**
   - 原因: `start_sim_nodes` 被 kill 后 RPMsg 断开, FreeRTOS 端点不会自动恢复; 再次启动 `start_sim_nodes` 也无效。
   - 解决: 每次单独测试前必须重启 FreeRTOS 固件 (`echo stop/start > /sys/class/remoteproc/remoteproc0/state`), 让 RPMsg 状态重置。

6. **SSH 在高负载下断开**
   - 原因: 三节点并发或启动/停止桌面进程时系统负载突增, SSH 连接被重置。
   - 解决: 把测试流程写成脚本放到板子上执行, 避免通过 SSH 直接传长命令; 执行后等待几秒再重新连接查看结果。

## 硬件与内核限制

- **可用 CPU 核心**: Linux 下实际只有 **CPU0 和 CPU2** 在线 (`/sys/devices/system/cpu/online` 显示 `0-2`)。
  - `possible` 与 `present` 虽然显示 `0-3`, 但 **CPU3 不在线**, `taskset -c 3` 会报 "Invalid argument"。
  - 因此三节点并发只能把 5bus/9bus 放在 CPU0, 39bus 放在 CPU2; 无法给 9bus 单独分配一个 Linux 核。
- **CPU1 未被隔离**: 内核启动参数没有 `isolcpus=1`, Linux 进程 (如 firefox) 仍可调度到 CPU1, 与 FreeRTOS 竞争。
  - 测试前建议清理桌面进程: `sudo pkill -9 -f firefox; sudo pkill -9 -f Xtigervnc`。
  - 若需严格隔离 CPU1, 需要修改 U-Boot 启动参数并重启。

## CPU 满载原因分析

三节点并发时 `bench_all_nodes.sh` 显示 CPU0/CPU2 100%, 原因如下:

- **CPU0**: 同时运行 `ukf_pipeline_5bus` 和 `ukf_pipeline_9bus`。
  - 5bus 单独占约 40~50% CPU, 9bus 单独占约 50~60% CPU, 两者相加接近或达到 100%。
  - 清理 firefox/Xtigervnc 等桌面进程后, CPU0 几乎全部消耗在这两个 UKF 进程上, 无其他"无用线程"拖累。
- **CPU2**: 运行 `ukf_pipeline_39bus`。
  - 39bus 矩阵大 (ns=20, nm=98), 单核处理 250~300 fps 即满载, 属于正常计算负载。
  - 未清理桌面时, firefox 的 Isolated Web Content (约 44%) 和 Xtigervnc (约 16%) 也会占用 CPU2, 进一步挤压 39bus。
- **CPU1**: FreeRTOS 独占 (remoteproc), 但 Linux 仍可调度进程上去; firefox 等桌面程序会与之竞争。

## FT 版本优势场景

实测结论: **FT 库仅对 39bus 这种大矩阵 UKF 有显著收益, 对 5bus/9bus 小矩阵没有优势, 甚至引入调用开销导致性能下降**。

| 节点 | 矩阵规模 | 非 FT FPS | FT FPS | 结论 |
|------|----------|-----------|--------|------|
| 5bus (单独) | ns=4, nm=14 | 2261 | 2268 | 基本持平, FT 延迟略高 (166us → 182us) |
| 9bus (单独) | ns=6, nm=24 | 2936 | 2138 | **FT 明显更差**, 调用开销大于矩阵加速 |
| 39bus (三节点并发) | ns=20, nm=98 | 250 | 300 | **FT 提升 20%**, 延迟降低 24% |

因此:
- 生产部署时 **5bus/9bus 应使用非 FT 版本**。
- **39bus 使用 FT 版本** 以获得最佳吞吐和最低延迟。
- 后续若新增更多母线/测量 (矩阵规模接近或超过 39bus), 可再评估 FT 版本。

## 性能测试

- 39bus 对比: `./bench_39bus_compare.sh 30`
- 5bus 对比 (重点观察延迟/CPU): `./bench_5bus_compare.sh 20`
- 9bus 对比 (追求最高频率): `./bench_9bus.sh 20`

结果分别写入 `logs_bench_{39bus,5bus,9bus}/summary_<timestamp>.json`, 详见 [STRESS_TEST_REPORT.md](STRESS_TEST_REPORT.md)。
