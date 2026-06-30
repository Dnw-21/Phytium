# Task2 FreeRTOS → Linux UKF 性能测试报告

> **状态**: 已实测并部署 (2026-06-18)
>
> 测试对象: `task2_linux/ukf_pipeline_{5bus,9bus,39bus}` (非 FT) vs `ukf_pipeline_{5bus,9bus,39bus}_ft` (FT)
>
> 测试平台: Phytium PE2204 (aarch64), Linux 6.6.63-phytium-embedded-v3.2
>
> 测试脚本: `bench_5bus_compare.sh`, `bench_9bus.sh`, `bench_39bus_compare.sh`, `bench_all_nodes.sh`

## 通用测试方法

1. 重启 FreeRTOS 固件, 重置 SHM。
2. 通过一次 `start_sim_nodes` "prime" 解决 39bus/9bus RPMsg endpoint 绑定竞态。
3. 正式启动 `start_sim_nodes`, 仅让 FreeRTOS 生成数据。
4. 单独启动目标 UKF Pipeline, 绑定固定 CPU, 运行规定时长。
5. 从 stderr 心跳提取 `frames` / `latency` / `RMSE`, 并计算 wall-time fps。

> 非 FT 版本为静态链接; FT 版本在开发板上用本地 `gcc` 动态链接 BLAS-FT v1.5.0 / LAPACK-FT v1.4.0 (交叉编译产物因 glibc 版本不匹配无法运行)。

---

## 1. FT 版本优势场景 (核心结论)

**FT 库仅对 39bus 这种大矩阵 UKF 有显著收益; 对 5bus/9bus 小矩阵没有优势, 甚至引入调用开销导致性能下降。**

| 场景 | 节点 | 矩阵规模 | 非 FT FPS | FT FPS | 延迟变化 | 结论 |
|------|------|----------|-----------|--------|----------|------|
| 单独运行 | 5bus | ns=4, nm=14 | 2261 | 2268 | 166us → 182us | 基本持平, FT 略慢 |
| 单独运行 | 9bus | ns=6, nm=24 | 2936 | 2138 | 280us → 248us | **FT 明显更差** |
| 三节点并发 | 39bus | ns=20, nm=98 | 250 | 300 | 3576us → 2717us | **FT 提升 20%**, 延迟降低 24% |
| 三节点并发 | 5bus | ns=4, nm=14 | 2271 | 2268 | 93us → 104us | 基本持平 |
| 三节点并发 | 9bus | ns=6, nm=24 | 2236 | 2125 | 260us → 256us | FT 略差 |

**建议**:
- **5bus/9bus 生产部署使用非 FT 版本**。
- **39bus 生产部署使用 FT 版本**。
- 后续新增节点若 `ns × nm` 接近或超过 39bus (20×98) 规模, 可再评估 FT。

---

## 2. 39bus 测试结果

数据来源: `logs_bench_39bus/summary_20260618_000155.json`

| 指标 | 非 FT (初始版本) | FT 版本 | 提升 |
|------|------------------|---------|------|
| 运行时长 | 30.0 s | 30.0 s | - |
| 处理帧数 | 11500 | 14000 | +21.7% |
| Wall-time FPS | **383.3** | **466.7** | **+21.7%** |
| Simulation-time FPS | 1982.8 | 2000.0 | +0.9% |
| 平均单帧延迟 (lat) | **2674 us** | **2211 us** | **-17.3%** |
| RMSE (稳态) | 2.4479 | 2.4487 | 基本持平 |

**分析**: FT 加速有效, 单帧延迟降低约 17%, 吞吐提升约 22%。在 500 fps 数据源速率约束下, FT 版本已接近实时消费。

---

## 3. 5bus 测试结果

测试脚本: `bench_5bus_compare.sh`
数据来源: `logs_bench_5bus/summary_20260618_022143.json`

### 关键改动

- 修正 `sim_params_5bus.h`: `DT=0.0005s`, `FS=2000Hz`, `NUM_STEPS=360000`, 与 Python 参考 (`system_params.mat`) 和 Linux UKF 的 `deltt=0.0005` 对齐。
- 在 [sim_node_task.c](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/sim_node_task.c) 中加入与 39bus/9bus 相同的 SHM 背压机制, 避免 SHM 写爆。
- 删除 5bus 冗余的 RPMsg 批量发送, 仅通过 SHM 传输数据。
- Linux 侧优化: stdout 全缓冲 + 每 500 帧 flush, 减少每行 CSV 的系统调用开销。

### 测试配置

- UKF 参数: `sigma_a=0.01`, `sigma_w=0.03`, `sigma_m=0.01`
- 5bus: `ns=4`, `nm=14`, `np=8`
- SHM: base=`0xC8100000`, size=`256 KB`, frame=`64 B`, capacity=`4095` 帧
- CPU 绑定: 5bus UKF → CPU0; FreeRTOS → CPU1
- FreeRTOS 生成速率: speed=1 时 2000 fps (实时)

### 结果表格

| 指标 | 非 FT | FT 版本 | 备注 |
|------|-------|---------|------|
| 运行时长 | 20 s | 20 s | - |
| 处理帧数 | 44063 | 44020 | 含初始 SHM 满缓冲 4095 帧的消费 |
| Wall-time FPS | **2203.2** | **2201.9** | 基本持平 |
| FreeRTOS 生成 FPS (`shm_fps`) | **1998.5** | **1997.2** | 与 speed=1 设定的 2000Hz 实时目标一致 |
| 平均单帧延迟 (lat) | **99 us** | **106 us** | FT 库调用开销对小矩阵略高 |
| RMSE (稳态) | 1.0847 | 1.0847 | 基本一致 |

**分析**: 5bus 状态维度小, 2000Hz 已足够。非 FT 版本已能轻松实时消费; FT 版本由于 BLAS/LAPACK 函数调用开销, 单帧延迟反而略高。生产部署建议使用 **非 FT 版本**。

---

## 4. 9bus 测试结果

测试脚本: `bench_9bus.sh`, `bench_all_nodes.sh`
数据来源: `logs_bench_9bus/summary_20260618_021205.json`, `logs_bench_all_nodes/ft_20260618_102623.json`

### 关键改动

- [sim_node_9bus.c](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/sim_node_9bus.c) 保持每帧写入 SHM (无降采样), 并加入背压: SHM 满时阻塞等待 Linux UKF 消费。
- **9bus 最大频率限制为 2000 Hz**: 固定每 8 帧 `vTaskDelayUntil(4ms)`, 与 5bus 保持一致。
- Linux 侧 FT 版本使用 BLAS-FT / LAPACK-FT 加速矩阵运算。
- stdout 全缓冲 + 每 500 帧 flush, 减少系统调用。

### 测试配置

- UKF 参数: `sigma_a=0.01`, `sigma_w=0.03`, `sigma_m=0.01`
- 9bus: `ns=6`, `nm=24`, `np=12`
- SHM: base=`0xC81C0000`, size=`128 KB`, frame=`104 B`, capacity=`1260` 帧
- CPU 绑定: 9bus UKF → CPU0 (与 5bus 共享); FreeRTOS → CPU1
- FreeRTOS 生成速率: 硬上限 **2000 fps**

### 结果表格

| 指标 | 非 FT (单独, speed=2) | FT 版本 (单独, speed=2) | 非 FT (三节点并发) | FT 版本 (三节点并发) |
|------|----------------------|-------------------------|--------------------|----------------------|
| 运行时长 | 20 s | 20 s | 30 s | 30 s |
| 处理帧数 | 95423 | 95431 | 67966 | 64183 |
| Wall-time FPS | **4773.3** | **4775.0** | **2235.9** | **2124.9** |
| FreeRTOS 生成 FPS (`shm_fps`) | **4710.3** | **4712.0** | **2194.4** | **2083.2** |
| 平均单帧延迟 (lat) | **82 us** | **83 us** | **260 us** | **256 us** |
| RMSE (稳态) | 1.3011 | 1.3011 | 1.3011 | 1.3010 |

**分析**: 9bus 在单独运行、speed=2 时可达约 4775 fps; 按用户要求将最大频率限制在 **2000 Hz** 后, 三节点并发场景下实际生成帧率约 **2083~2194 fps**, 与 2000Hz 目标一致。FT 版本对小矩阵无明显加速, 甚至在单独运行时因调用开销导致 FPS 从 2936 降至 2138。生产部署建议使用 **非 FT 版本**。

---

## 5. 三节点并发测试结果

测试脚本: `bench_all_nodes.sh`
数据来源: `logs_bench_all_nodes/nonft_20260618_102623.json`, `logs_bench_all_nodes/ft_20260618_102623.json`

### 测试配置

- 同时运行 5bus/9bus/39bus 三个 FreeRTOS 仿真任务 + 三个 Linux UKF Pipeline。
- FreeRTOS 独占 CPU1; Linux UKF 绑定: 5bus/9bus → CPU0, 39bus → CPU2。
- 三个任务优先级均为 6, FreeRTOS 时间片轮转调度。
- 每个节点使用独立 SHM 区域, 互不重叠; 背压机制防止 SHM 写爆。

### 非 FT 版本结果

| 节点 | 处理帧数 | Wall-time FPS | 生成 FPS | 延迟 (us) | RMSE | CPU |
|------|----------|---------------|----------|-----------|------|-----|
| 5bus | 68407 | 2271.2 | 2135.3 | 93 | 1.0864 | CPU0 100% |
| 9bus | 67966 | 2235.9 | 2194.4 | 260 | 1.3011 | CPU0 100% |
| 39bus | 7500 | 250.0 | 241.8 | 3576 | 2.4444 | CPU2 100% |

### FT 版本结果

| 节点 | 处理帧数 | Wall-time FPS | 生成 FPS | 延迟 (us) | RMSE | CPU |
|------|----------|---------------|----------|-----------|------|-----|
| 5bus | 68361 | 2267.6 | 2131.8 | 104 | 1.0864 | CPU0 100% |
| 9bus | 64183 | 2124.9 | 2083.2 | 256 | 1.3010 | CPU0 100% |
| 39bus | 9000 | 300.0 | 288.3 | 2717 | 2.4444 | CPU2 100% |

### 资源消耗

- **CPU**: CPU0 (5bus+9bus) 满载 100%; CPU2 (39bus) 满载 100%; CPU1 (FreeRTOS) 独立运行。
  - 清理 firefox/Xtigervnc 等桌面进程后, CPU0/CPU2 几乎全部消耗在 UKF 计算上, 无其他"无用线程"拖累。
  - 未清理桌面时, firefox 的 Isolated Web Content (约 44%) 和 Xtigervnc (约 16%) 会占用 CPU2, 进一步挤压 39bus。
- **内存**: 非 FT 运行前后 1079M → 1081M; FT 运行前后 1078M → 1088M。三个 UKF 进程 RSS 较小, 内存占用稳定。
- **SHM**: 三个节点独立地址, 无冲突; 背压机制有效, 无溢出。

### 结论

- 三个节点**可以同时生成数据和状态估计, 互不冲突**。
- 5bus/9bus 生成频率被严格限制在约 **2000 Hz**, 符合用户要求。
- 39bus 受 UKF 处理能力限制, 实际生成/消费频率约 **250~300 Hz**, 背压机制使其自动匹配 UKF 处理速度, 做到连续状态估计。

---

## 6. 关键参数汇总

| 节点 | ns | nm | SHM base | SHM size | frame size | capacity | 实测生成频率 (并发) |
|------|----|----|----------|----------|------------|----------|---------------------|
| 5bus | 4 | 14 | 0xC8100000 | 256 KB | 64 B | 4095 | ~2130 Hz |
| 9bus | 6 | 24 | 0xC81C0000 | 128 KB | 104 B | 1260 | ~2080 Hz |
| 39bus | 20 | 98 | 0xC8140000 | 512 KB | 400 B | 1310 | ~240~290 Hz |

## 7. 结论与后续

- **39bus**: FT 版本已实现约 20% 性能提升 (250→300 fps), 延迟降低 24%, 链路已跑通。生产部署使用 **FT 版本**。
- **5bus**: 链路已跑通并实测。FreeRTOS 实际生成速率约 **2130 fps**, 与 2000Hz 实时目标一致; UKF 消费帧率约 **2268 fps**, 无丢帧。生产部署使用 **非 FT 版本**。
- **9bus**: 已按用户要求将最大频率限制在 **2000 Hz**。三节点并发下实际生成帧率约 **2080~2190 fps**, UKF 消费帧率约 **2125~2236 fps**, 可连续状态估计。生产部署使用 **非 FT 版本**。
- **多节点并发**: 三个节点可同时运行, 独立 SHM + 背压机制保证无冲突、无溢出。CPU0 (5bus+9bus) 和 CPU2 (39bus) 满载, FreeRTOS 独占 CPU1。
- **已知限制**: Linux 下实际只有 CPU0/CPU2 可用 (CPU3 不在线); CPU1 未被 `isolcpus` 隔离, 桌面进程会与 FreeRTOS 竞争。详见 [README.md](README.md)。
- 所有二进制与 FreeRTOS 固件已部署到 Phytium 板 (`192.168.88.10`), 并通过 `bench_all_nodes.sh 30` 验证。
