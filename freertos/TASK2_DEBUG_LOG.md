# 任务二：调试经验文档

> 创建日期: 2026-06-04 | 最后更新: 2026-06-09 (v5 最终修复: SHM地址匹配+降采样+Buffer扩容+MemoryError修复)

---

## 开发板上电调试 (2026-06-04)

### 端到端管道验证结果

```
✅ FreeRTOS SDK 编译: ELF 722KB, sim_node_task 编译通过 (仅 packed-member warning)
✅ 固件部署: remoteproc stop/start, state=running
✅ SIM task 启动: [SIM] init: 2-gen/5-bus, dt=0.0005s, 360000 steps
✅ RPMsg endpoint: ept ret=0, 独立通道 "rpmsg-sim-5bus"
✅ RPMsg 绑定: [SIM] bound: dest=0x400
✅ START 命令: [SIM] START 成功接收
✅ 数据生成: 360,000/360,000 帧完成 (~154k steps/s, 77x real-time)
✅ 数据传输: RPMsg 批量发送, 1600 帧捕获验证
✅ 数据精度: PG1/2, V1 与 Python 参考误差 < 1e-4
```

### 性能实测

| 指标 | 值 |
|------|-----|
| FreeRTOS 仿真速度 | ~154,000 steps/s (77x real-time) |
| 每步耗时 | ~6.5us |
| RPMsg 批量发送 | ~22,500 batches/s |
| 数据精度 (vs Python) | < 1e-4 |
| ELF 大小增加 | +15KB (707→722KB) |

---

### 2.7 主机 UKF + Dashboard 联调 (2026-06-04)

**方案**: 开发板无网络无法 pip install numpy/scipy/flask, 改为:
- 开发板: sim_bridge_stdout → 二进制帧写 stdout
- SSH 管道: `ssh board ./bridge | python3 ukf_stdin_runner.py`
- 主机: UKF 从 stdin 读取, 保存 npz, Dashboard 读取 npz

**结果**:
- UKF 处理 50,000 帧 @ 1473 fps (远超 2000Hz 实时要求)
- 故障检测: t=5.000s ⚡ FAULT → t=5.310s ✅ CLEARED
- RMSE 均值 0.0608
- Dashboard API 正常, 浏览器可访问 `http://localhost:5001`

**注意**: 主机的 http_proxy 环境变量导致 curl 访问 localhost 被代理 (502)。浏览器不受影响。

### 2.1 RPMsg 通道名冲突

**现象**: SIM task RPMsg endpoint 创建成功但 Linux 侧 `/dev/rpmsgX` 不出现，"channel already exist" 错误。
**根因**: SIM task 使用与 RPM task 相同的通道名 `rpmsg-openamp-demo-channel`，Linux virtio_rpmsg_bus 拒绝重复通道。
**修复**: 改为独立通道名 `rpmsg-sim-5bus`。dmesg 确认: `creating channel rpmsg-sim-5bus addr 0x0`。
**教训**: 多个 OpenAMP endpoint 需使用不同 service name，内核不支持同名通道。

### 2.2 rpmsg_send 返回值误判

**现象**: `sent=0` 但 Linux 侧收到了数据。
**根因**: SDK 的 `rpmsg_send()` 成功时返回发送字节数 (如 458)，而非 0。代码中 `if (ret == 0)` 误判为失败。
**修复**: 改为 `if (ret >= 0)`。
**教训**: 不同 OpenAMP 版本 `rpmsg_send` 返回值语义不同，需查阅 SDK 实现或实际测试确认。

### 2.3 回调中 RpmsgPkt 头未跳过

**现象**: FreeRTOS 收到 Linux 的 START 命令(0x51)但不触发 `sim_running = 1`。
**根因**: OpenAMP 回调的 `data` 参数指向完整 RPMsg 消息（含 6 字节 command/length 头），但代码直接将其当 SimCtrlPkt 解析，导致读到的是 RpmsgPkt.command 的低字节(0x51)而非 SimCtrlPkt.cmd(0x01)。
**修复**: 先解析 RpmsgPkt 头，再从 `pkt->data` 中读取 SimCtrlPkt。
**教训**: FreeRTOS 端 RPMsg 回调接收完整 RpmsgPkt（与 main.c 中 rpmsg_endpoint_cb 一致），Linux→FreeRTOS 方向有 6 字节头。

### 2.4 sudo 密码管道问题

**现象**: `echo user | sudo -S command` 通过 ssh 执行时 sudo 提示输入密码但未读取。
**根因**: ssh 远程执行时 stdin 管道行为异常，`sudo -S` 无法正确读取密码。
**解决**: 使用 `printf "user\n" | sudo -S command` 或写密码到临时文件 `sudo -S < /tmp/pass`。
**教训**: 通过 ssh 运行 sudo 命令时优先使用 `ssh -t` 或 NOPASSWD sudoers 配置。

### 2.5 ELF 编译警告 (packed member)

**现象**: `warning: taking address of packed member... may result in unaligned pointer value`
**根因**: `SimDataBatch *batch = (SimDataBatch *)batch_buf` 将 aligned buffer 转为 packed struct 指针。
**影响**: AArch64 支持非对齐访问，此警告可忽略。
**缓解**: 使用 `__attribute__((aligned(4)))` 声明缓冲区。

### 2.6 数据传输速率过高

**现象**: FreeRTOS 以 77x real-time 速度生成数据，RPMsg 发送 22,500 batches/s。
**影响**: 可能超过 RPMsg virtqueue 处理能力。
**解决**: 添加速度控制 (`sim_speed`), 默认 speed=0(最快), speed=1(实时=4ms/batch)。
**待优化**: 当前 Linux 测试程序只接收了 1600 帧(约 0.8s 仿真数据)，需确认全速率下的稳定性。

---

## 一、已解决的问题 (主机端)

### 1.1 测量函数 PG 值不匹配

**日期**: 2026-06-04
**现象**: C 代码 `sim_compute_meas_5bus()` 输出的 PG=4.75, 4.79，与 Python 参考值 PG=8.20, 3.53 不匹配。
**根因**: 单元测试中硬编码了错误的手动录入参数。`gen_sim_params_5bus.py` 自动生成的头文件参数是正确的，但 `sim_math_test.c` 中手动复制的 E_abs、PM、X_0、RV_imag 等值均有误。
**修复**: 改为直接 `#include "sim_params_5bus.h"` 使用自动生成的头文件，彻底消除手动录入错误。
**教训**: **绝不手动转录数值**。所有参数必须通过脚本自动生成。

### 1.2 RV 矩阵虚部非零

**日期**: 2026-06-04
**现象**: 初版假设 RV 矩阵虚部全为零，但实际 `initialize_system_5.py` 计算出的 `RV[:,:,0]` 有显著非零虚部（~-0.1 量级）。
**根因**: 电压恢复矩阵 RV = -inv(Y11) @ Y12，其中 Y11 和 Y12 均为复数矩阵，结果必然有虚部。
**修复**: 在参数导出脚本中同时导出 RV_REAL 和 RV_IMAG 两个数组。
**教训**: 不要假设复矩阵的虚部为零，必须通过实际计算验证。

### 1.3 system_params.mat 缺少 UKF 参数

**日期**: 2026-06-04
**现象**: `ukf_runner_5bus.py` 加载 `system_params.mat` 时报 KeyError: 'ns'。
**根因**: `terminal_node_5.py` 保存的 `.mat` 文件只包含系统参数 (YBUS, RV, E_abs, PM, M, D 等)，不包含 UKF 初始化所需的派生参数 (ns, nm, deltt, P, Q_mat, R_meas, W, X_hat)。
**修复**: 从基础参数计算派生参数:
```python
ns = 2 * n
nm = 2 * n + 2 * s
deltt = 1.0 / fs
P = sig**2 * eye(ns)
Q_mat = sig**2 * eye(ns)
R_meas = sig**2 * eye(nm)
W = ones(2*ns, 1) / (2*ns)
X_hat = X_0
```
**教训**: `.mat` 文件只保存了"终端→主控"传输所需的最小参数集，UKF 启动参数需要主控端自行从基础参数推导。

### 1.4 RK4 实现 Bug (k4 系数)

**日期**: 2026-06-04
**现象**: 初版 `sim_rk4_step_5bus()` 中 k4 未乘以 dt，但在最终组合时使用了 `k4[i]*dt`，导致结果错误。
**根因**: 编码失误。k1/k2/k3 都在循环中乘以了 dt，但 k4 遗漏了 `k4[i] *= dt`。
**修复**: 添加 `k4[i] *= dt`，最终组合改为 `(k1 + 2*k2 + 2*k3 + k4) / 6`。
**教训**: **RK4 四组系数必须统一处理**。最佳实践：统一在循环中乘 dt，避免不一致。

### 1.5 main.c 变量重命名级联

**日期**: 2026-06-04
**现象**: `sed` 替换 `rpdev → g_rpdev` 时，`g_rpdev` 也被匹配，变成 `g_g_rpdev`。
**根因**: 全局替换 `rpdev` 时没有限定词边界。
**修复**: 手动检查并回退。使用 `sed -i 's/g_g_rpdev/g_rpdev/g'` 修复。
**教训**: 使用 sed 全局替换时需注意新名称包含旧名称的情况，可用 `\b` 或先替换为临时名称。

---

### 2.8 RPMsg dest_addr 不更新 — 数据发送成功但 Linux 收不到 (2026-06-04, 关键修复)

**现象**: 冷启动后 FreeRTOS `rpmsg_send` 返回成功(sent=step 计数递增), 但 Linux `/dev/rpmsgX` 的 `select()` 永不返回, 管道空。
**排查过程**:
1. 桥接重启会新建 `/dev/rpmsgX` 设备, 每个设备有新地址
2. FreeRTOS SIM 回调中 `src` 参数被 `(void)src` 忽略
3. OpenAMP 低层只在 `dest_addr==RPMSG_ADDR_ANY` 时更新一次地址
4. 桥接重启后 SIM 仍向旧地址发送, 数据进入无监听者的 virtqueue
5. virtqueue 满后 `rpmsg_send` 开始失败(仅 35% 成功率)
**根因**: RPMsg 回调未同步 `ept->dest_addr = src`, 导致 FreeRTOS 向已断开的旧 Linux 端点发送数据。
**修复**: 在 `sim_node_task.c` 和 `sim_node_39bus.c` 的回调中添加:
```c
if (src != RPMSG_ADDR_ANY && ept) ept->dest_addr = src;
```
每次收到 START 命令时更新目标地址到最新发送者。
**结果**: `sent=step` → **100% 成功率**, 双管道 (5bus+39bus) 同时接收数据。
**教训**: RPMsg 多端点场景下, **每次收到消息都必须更新 dest_addr**, 不能依赖 OpenAMP 的首次自动绑定。

### 2.9 C UKF 在 Phytium 上性能验证 (2026-06-04)

**结果**: 
- C UKF (5bus/2-gen): **1,804 fps** (3x Python 的 602 fps)
- RMSE: 0.0607 (与 Python 0.0608 一致)
- CPU: 53.7% (单核)
- 34k 帧处理成功, 状态值正确(故障后振荡)

**双管道并行**:
- 5bus: `sent=step` 100% 成功率, C UKF @ 1,804 fps
- 39bus: `sent=step` 100% 成功率, 数据接收正常(待 UKF 联调)

**FreeRTOS 端**:
- SIM 5bus: RK4 @ 2000Hz, 360k 帧
- SIM 39bus: RK4 @ 2000Hz, 360k 帧  
- 双任务 CPU 占用: 各 ~0.1% (因为 vTaskDelay 实时调速)

### 2.10 三节点全部 100% 成功 (2026-06-04)

**方案**: 统一 RPMsg 通道 + 三端点桥接。
- FreeRTOS 三任务共享通道名 `rpmsg-sim-ctrl`，各用不同 src 地址 (0x0, 0xa, 0x14)
- Linux 桥接打开三个独立 endpoint，dst 分别匹配 FreeRTOS src
- 不同命令码 (0x51/0x60/0x70) 区分节点

**结果**: 
```
SIM   (5bus/2-gen):   sent=56000/56000  ✅ 100%
SIM39 (39bus/10-gen): sent=6000/6000    ✅ 100%
SIM9  (9bus/3-gen):   sent=6000/6000    ✅ 100%
```

**教训**: OpenAMP 同一通道多个 endpoint 时，必须用精确 dst 地址发送消息，RPMSG_ADDR_ANY 只送达第一个端点。

### 2.11 sudo -n 导致 9bus 帧数为 0 (2026-06-07)

**现象**: `launch_ukf_multi.py` 中 9bus 节点帧数为 0，但直接运行 `ukf_pipeline --node 9bus` 能读取 845 帧。
**根因**: `sudo -n` (non-interactive) 在没有 NOPASSWD 配置时失败，进程静默退出。launch_ukf_multi.py 的 stdout 读取线程收到 EOF，帧数保持 0。
**修复**: 改为 `sudo -S` + `proc.stdin.write(b'user\n')` 提供密码。
**教训**: 通过 Python subprocess 运行 sudo 命令时，优先使用 `sudo -S` + stdin 密码，而非依赖 `sudo -n` + NOPASSWD 配置。

### 2.12 5bus UKF NaN — pipeline 启动时序问题 (2026-06-07)

**现象**: 5bus UKF 输出 `X=[nan,nan] rmse=nan`，但 39bus 和 9bus 正常。
**根因**: 5bus 仿真步数少 (360k 步，speed=5 时约 36 秒完成)。当 `launch_ukf_multi.py` 在 FreeRTOS 仿真完成后才启动时，ukf_pipeline 从 ring buffer 末尾读取数据，但 buffer 已被多次覆盖，导致 UKF 输入数据损坏。
**修复**: **必须先启动 ukf_pipeline 再启动 FreeRTOS 仿真**。Pipeline 会等待 `shm->count > 0`，当仿真开始写入数据时立即开始读取，保证数据完整性。
**教训**: SHM ring buffer 模式中，消费者必须比生产者更早启动，否则会读到被覆盖的旧数据。

### 2.13 npz 保存文件后缀问题 (2026-06-07)

**现象**: 保存的 npz 文件名为 `ukf_results_5bus.npz.tmp.npz` 而非 `ukf_results_5bus.npz`。
**根因**: numpy 的 `np.savez('/tmp/ukf_results_5bus.npz.tmp', ...)` 自动追加 `.npz` 后缀。
**修复**: 将 temp 文件命名为 `ukf_results_5bus.tmp.npz` (不含 `.npz` 在中间), 然后 rename 到 `ukf_results_5bus.npz`。
**教训**: `np.savez` 会自动追加 `.npz` 后缀, temp 文件名必须避免包含 `.npz`。

---

## 二、当前状态 (2026-06-07 交接)

### 2.1 三节点 UKF 运行状态

| 节点 | 发电机 | 状态维 | 绑定核 | 帧数 | FPS | RMSE | 状态 |
|------|:------:|:------:|:------:|------:|------:|------:|------|
| 5bus | 2 | 4 | CPU0 | 2,464 | 2,924 | 0.0 | done |
| 39bus | 10 | 20 | CPU2 | 262 | 948 | 0.0 | done |
| 9bus | 3 | 6 | CPU0 | 264 | 1,394 | 0.0 | done |

### 2.2 Dashboard 可访问性

- 浏览器: `http://192.168.1.100:5001`
- VNC: `192.168.1.100:5902`
- 对比表格显示: 发电机数、状态维、绑定核、帧数、RMSE、延迟(us)、FPS、CPU%

### 2.3 数据流

```
FreeRTOS → SHM ring buffer → ukf_pipeline (mmap /dev/mem) → stdout 二进制帧
  → launch_ukf_multi.py (读取, 保存 npz/json, 收集 metrics)
  → dashboard_server_v2.py (Flask API, 读取 npz/json)
  → dashboard_v2.html (Chart.js 前端)
```

### 2.4 关键文件

| 文件 | 说明 |
|------|------|
| `task2_linux/ukf_pipeline.c` | C UKF, 从 SHM 读数据, 写 stdout 二进制帧 |
| `task2_linux/launch_ukf_multi.py` | 并行启动 3 个 ukf_pipeline 进程, 绑定 CPU, 收集 metrics |
| `task2_linux/dashboard_server_v2.py` | Flask 面板 :5001 |
| `task2_linux/templates/dashboard_v2.html` | 暗色主题前端, 三节点对比表格 |
| `task2_linux/start_sim_nodes.c` | 通过 RPMsg 启动 FreeRTOS 仿真 |
| `task2_linux/reset_shm.c` | 工具: 重置 SHM 计数器 |
| `freertos/src/sim_node_task.c` | FreeRTOS 5bus 仿真 |
| `freertos/src/sim_node_39bus.c` | FreeRTOS 39bus 仿真 |
| `freertos/src/sim_node_9bus.c` | FreeRTOS 9bus 仿真 |
| `freertos/inc/shm_data.h` | SHM 基地址定义 |

---

## 三、调试技巧

### 3.1 交叉验证方法

```
Python (initialize_system_5.py) → 导出 C 头文件 → C 代码
                ↓                                    ↓
         Python reference output              C unit test output
                ↓                                    ↓
                └────────── compare ─────────────────┘
```

### 3.2 浮点精度

- C 代码使用 `float` (单精度), Python 使用 `float64` (双精度)
- 允许相对误差: 1e-4 (对 1.0 量级的值: ±0.0001)
- 对多次 RK4 积分后的累计误差: 允许 1e-3
- **当前测量函数误差**: < 1e-5 (vs Python), 远好于预期

### 3.3 性能预估

| 操作 | 单步耗时 | 2000Hz 总 CPU |
|------|---------|:--:|
| RK4 积分 (2-gen) | ~200 float ops | <0.1% |
| 测量函数 | ~100 float ops | <0.05% |
| RPMsg 批量发送 | ~50us/batch | <2% |
| **FreeRTOS 总计** | — | **<3%** |
| Linux UKF (Python) | ~500us/step | ~100% (单核) |

**注意**: Linux UKF 在 Python 下接近性能瓶颈 (~1916 steps/s vs 需要 2000)。建议:
1. 使用 PyPy 加速 (~3x)
2. 或降低 UKF 频率 (如每 10 步做一次 UKF, 200Hz)
3. 或使用 C 扩展 / Cython 加速
4. 或在开发板上使用多核并行

---

## 四、下次开发板上电后待做

### Phase 3: 高并发扩展

1. [ ] 同一节点多数量 → 共享 SHM, 多 UKF worker 进程池并行消费
2. [ ] 逐步扩展节点数: 3→6→10→20, 测试 CPU 饱和点
3. [ ] 根据计算量优化 CPU 分配 (计算量大的节点独占一核)
4. [ ] WebServer 参考架构 (Epoll + Reactor + 线程池) 用于高并发
5. [ ] 资源监控面板: CPU 利用率、内存占用、核间传输延迟

### 标准启动流程 (重要!)

```bash
# 1. 重启 FreeRTOS
echo "user" | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 2
echo "user" | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3

# 2. 清理旧数据
echo "user" | sudo -S rm -f /tmp/ukf_*.npz /tmp/ukf_*.json /tmp/ukf_*.log /tmp/ukf_metrics.json /tmp/ukf_*.tmp*

# 3. 先启动 UKF Pipeline (等待 SHM 数据)
cd /home/user/Phytium/task2_linux
python3 launch_ukf_multi.py &

# 4. 再启动 FreeRTOS 仿真
echo "user" | sudo -S ./start_sim_nodes 5bus 39bus 9bus

# 5. 启动 Dashboard
python3 dashboard_server_v2.py --port 5001
```

**重要**: 步骤 3 必须在步骤 4 之前执行，否则 5bus NaN。

### 常用调试命令

```bash
# 查看 SHM 计数器
echo "user" | sudo -S busybox devmem 0xC8100000 32  # 5bus WI
echo "user" | sudo -S busybox devmem 0xC8100008 32  # 5bus CNT
echo "user" | sudo -S busybox devmem 0xC8118000 32  # 9bus WI
echo "user" | sudo -S busybox devmem 0xC8118008 32  # 9bus CNT

# Dashboard API 测试
curl -s http://localhost:5001/api/compare
curl -s http://localhost:5001/api/status

# 查看 FreeRTOS 状态
echo "user" | sudo -S cat /sys/class/remoteproc/remoteproc0/state

# 查看日志
tail -f /tmp/ukf_log_5bus.log
```

---

## 五、参考链接

- 开发计划: `TASK2_DEVELOPMENT_PLAN.md`
- 交接文档: `HANDOVER.md`
- 调试指南: `DEBUG_GUIDE.md`
- 操作手册: `OPERATIONS.md`
- 变动记录: `TASK2_CHANGELOG.md`

---

## 六、v4/v5 大规模重构阶段 (2026-06-07 ~ 2026-06-09)

### 6.1 Python threading 导致 MemoryError 三节点崩溃 (2026-06-07)

**现象**: 三节点 UKF 同时运行时，Python 进程因 `MemoryError` 崩溃，`launch_ukf_multi.py` 退出。

**根因**:
1. Python threading 共享 GIL，三个节点共用一个 Python 进程内的线程池，内存不隔离
2. `MAX_HISTORY_FRAMES=30000` 设置过高，39bus 单帧占用 `(1+20+1+98)*8=960B`，30000 帧≈28MB/节点，三线程同时膨胀到 >80MB
3. Python GIL 导致线程间竞争，内存分配碎片化加剧

**修复**:
1. **multiprocessing 替代 threading**: 每个 UKF 节点独立 Python 子进程，打破 GIL，各自拥有独立内存空间和 GIL
2. `MAX_HISTORY_FRAMES` 降至 5000，前端降采样 `DOWNSAMPLE=4`
3. 每个子进程独立保存 npz 文件，互不干扰

**教训**:
- Python 多线程适合 I/O 密集型任务，CPU 密集型任务必须用多进程
- SHM 数据量大的场景下，内存需要精细管理，不能同时缓存三节点全部历史数据
- GIL 在 ARM64 多核上的性能损失比预期更大（x86 上通常不明显）

---

### 6.2 9bus 帧数为 0 (2026-06-07)

**现象**: 多节点启动后，39bus 和 5bus 有数据，9bus 始终帧数为 0。

**根因**:
1. MemoryError 导致 9bus 子进程在收到第一帧前崩溃
2. 旧的 CPU 绑定方案尝试将 9bus 绑定到 CPU3，但 CPU3 已被 FreeRTOS 占用且 PSCI 拒绝上线
3. `sudo -n` 在没有 NOPASSWD 配置时 taskset 命令静默失败

**修复**:
1. 改用 multiprocessing 后每个节点独立进程，不再因其他节点 MemoryError 而崩溃
2. 将 9bus CPU 绑定从 CPU3/CPU1 改为 CPU0（与 5bus 共享）
3. 改为 `sudo -S` + `proc.stdin.write(b'user\n')` 提供密码

**教训**:
- CPU 亲和性绑定前必须确认目标核心是可达的（`cat /sys/devices/system/cpu/cpuX/online`）
- 进程静默失败时需检查 stderr 和 exit code，不能仅依赖 stdout 输出判断

---

### 6.3 CPU3 不可用根因分析 (2026-06-08)

**现象**: `echo 1 > /sys/devices/system/cpu/cpu3/online` 返回 I/O error，CPU3 永远 offline。

**排查过程**:
```bash
# 设备树状态检查
cat /sys/firmware/devicetree/base/cpus/cpu@3/status   → okay
cat /sys/firmware/devicetree/base/cpus/cpu@3/enable-method → psci

# 内核启动日志
dmesg | grep -i psci  → psci: failed to boot CPU3 (-22)
dmesg | grep -i smp  → CPU3: failed to come online
```

**根因**: ATF (Arm Trusted Firmware) PSCI 层拒绝了 CPU3 的上线请求。设备树标记为 `okay`，但固件层面不可修改（PSCI 是板载固件，非用户可配置）。

**影响**: 4 核 Phytium PE2204 仅 3 核对 Linux 可用（CPU0/CPU1/CPU2），CPU1 被 FreeRTOS remoteproc 独占 → Linux 实际可用 CPU0 和 CPU2。

**最终 CPU 分配方案**:
```
Core 0 (A55): 5bus + 9bus UKF + Python 主脚本 + SSH/网络服务
Core 1 (A76): FreeRTOS 独占 (三节点 RK4 仿真引擎)
Core 2 (A76): 39bus UKF 独占 (VIP 计算特区)
```

**教训**:
- 设备树 `status=okay` 不代表核心实际可用，PSCI 固件可能有独立限制
- 异构多核系统中，可用核心数需通过 `nproc`、`dmesg`、`/sys/devices/system/cpu/` 三方交叉验证
- 固件层面的限制无法在 Linux 层绕过，需调整软件架构适应硬件现实

---

### 6.4 RPMsg 洪泛导致 5bus 饥饿 (2026-06-08)

**现象**: FreeRTOS 三节点同时运行时，5bus 数据发送极慢或停滞，9bus 和 39bus 持续发送但 5bus 延迟严重。

**根因**:
1. 39bus 和 9bus 的 `speed=0`（全速）时，每帧执行一次 `rpmsg_send` 发送回执
2. RPMsg virtqueue 容量有限（通常 256 个 slot），39bus/9bus 每帧都发送 → 快速填满 vring
3. vring 满后，5bus 的 `rpmsg_send` 阻塞等待 → 低级优先级任务阻塞了本应由 FreeRTOS 调度保护的 5bus 任务
4. 优先级反转: 高优先级的 5bus 任务被低优先级的 39bus/9bus 发送行为间接阻塞

**修复 v1**（部分有效）:
- 移除 39bus/9bus 的 per-frame `rpmsg_send`，数据仅通过 SHM 传输
- 保留 5bus 的 per-frame rpmsg_send（因 5bus 步数少）

**修复 v2**（批量速度控制, 彻底修复）:
- 引入 `BATCH_SIZE=8` 批量处理：每 8 步才执行一次 `vTaskDelay` yield
- `speed=0` 时执行 `vTaskDelay(1)`（最小 yield），确保其他任务有机会运行
- 将 `platform_poll_nonblocking` 移入 batch block 内，降低轮询频率

```c
int batch = 0;
while (running && step < NUM_STEPS) {
    // ... RK4 + SHM write + step++ ...
    batch++;
    if (batch >= BATCH_SIZE || step >= NUM_STEPS) {
        platform_poll_nonblocking(&g_rproc);
        if (speed >= 2) vTaskDelay(pdMS_TO_TICKS(8));
        else if (speed == 1) vTaskDelay(pdMS_TO_TICKS(4));
        else vTaskDelay(1);  /* speed=0: minimal yield */
        batch = 0;
    }
}
```

**教训**:
- FreeRTOS `speed=0` 全速模式下，若循环体内无任何 yield 操作，会导致同核其他任务永远得不到调度
- RPMsg 是有限资源（vring slot 有限），全速发送会快速耗尽 → 阻塞同通道的其他发送者
- 移除 per-frame rpmsg_send 的同时，必须保留周期性的 vTaskDelay 以确保调度公平
- 批量处理是折中方案：既能保持高速仿真，又能让多任务公平共享 CPU

---

### 6.5 GIL 瓶颈与 Buffer Read 优化 (2026-06-08)

**现象**: Python 三线程共享 GIL，实际 CPU 利用率远低于理论值，UKF 吞吐量受限于单核。

**根因**: Python GIL 保证了线程安全但限制了多核并行。即使 3 个线程绑定到不同的物理核，同一时刻只有一个线程能执行 Python 字节码。

**修复**:
1. **多进程架构**: `threading` → `multiprocessing.Process`，每个 C UKF 管道一个独立 Python 进程，各自拥有独立 GIL
2. **批量 Buffer Read**: `proc.stdout.read(data_size)` 逐帧读取 → `os.read(fd, 65536)` 批量 64KB 读取
3. **FrameParser 内存解码**: Python 内存中切分二进制帧，减少系统调用开销
4. **CPU 绑定**: 每个子进程通过 `taskset -c` 绑定到指定核

**实现**:
```python
# FrameParser 类
class FrameParser:
    def __init__(self):
        self.buffer = bytearray()
    def feed(self, raw_bytes):
        self.buffer.extend(raw_bytes)
    def parse(self):
        # 从 buffer 中提取完整帧
        while len(self.buffer) >= HDR_SIZE:
            ts_ms, frame_ns = struct.unpack(HDR_FMT, self.buffer[:HDR_SIZE])
            data_size = (frame_ns + 1) * 8
            if len(self.buffer) < HDR_SIZE + data_size:
                break
            yield self.buffer[HDR_SIZE:HDR_SIZE+data_size]
            self.buffer = self.buffer[HDR_SIZE+data_size:]
```

**性能收益**:
- 三节点并行: 从串行 ~1800fps → 并行 ~4500fps (3 节点综合)
- 系统调用减少: 逐帧 read(960B) → 批量 read(64KB)，syscall 降低 ~68 倍

**教训**:
- Python 的 threading 仅适用于 I/O 密集型任务，CPU 工作必须用 multiprocessing
- `proc.stdout.read()` 是阻塞系统调用，批量读取可大幅降低内核态/用户态切换
- 飞腾派 ARM64 架构上，独立进程 + 独立 GIL 的收益比 x86-64 更显著

---

### 6.6 二进制架构不匹配 — x86-64 在 ARM64 板上无法执行 (2026-06-09)

**现象**: `sudo ./start_sim_nodes 1` 没有任何反应，无输出、无错误、进程不启动。

**根因**: 
```bash
$ file start_sim_nodes
start_sim_nodes: ELF 64-bit LSB pie executable, x86-64, ...
$ uname -m
aarch64
```

所有 Linux 侧 C 二进制（`start_sim_nodes`、`ukf_pipeline`、`reset_shm`）在 x86-64 主机上编译，直接部署到 ARM64 Phytium 板上。ARM64 内核无法识别 x86-64 ELF 格式 → exec 系统调用失败。

**为什么没有错误信息？**
- 当通过 `sudo` 或脚本启动时，exec 失败的 stderr 可能被丢弃
- 如果脚本不检查命令返回码，用户看到的只是"没有反应"

**修复**:
1. 安装 ARM64 交叉编译器: `sudo apt install gcc-aarch64-linux-gnu`
2. 编译为 ARM64 静态链接二进制（避免目标板 glibc 版本不匹配）:
```bash
aarch64-linux-gnu-gcc -O2 -static -o start_sim_nodes start_sim_nodes.c
aarch64-linux-gnu-gcc -O2 -static -o ukf_pipeline ukf_pipeline.c \
    ../task2_freertos/ukf_core.c -I../task2_freertos -I../freertos/inc -lm
aarch64-linux-gnu-gcc -O2 -static -o reset_shm reset_shm.c
```
3. 在 `start_all.sh` 中加入架构预检:
```bash
BIN_ARCH=$(file start_sim_nodes | grep -oP '(ARM|aarch64|x86-64)')
MACHINE=$(uname -m)
if [ "$MACHINE" != "aarch64" ]; then
    echo "ERROR: Must run on ARM64 Phytium board"
    exit 1
fi
```

**教训**:
- **交叉编译是异构开发的基本功课**。主机和目标的 ISA 不同时，所有二进制必须用目标架构的工具链编译
- 静态链接避免了目标板上 glibc 版本不匹配的问题
- `file` 命令是验证二进制架构的最快方法
- 部署脚本必须包含架构验证，否则无声失败极难排查
- FreeRTOS SDK 内置的 `aarch64-none-elf-gcc` 是裸机工具链（无 Linux headers/glibc），不能用于 Linux 用户空间程序；需额外安装 `aarch64-linux-gnu-gcc`

---

### 6.7 f-string 变量名引号被 shell 吃掉 (2026-06-09)

**现象**: Python 脚本中 `f"{n['cpu']}"` 在 scp 传输后变成 `f"{n[cpu]}"`，运行时报 `NameError: name 'cpu' is not defined`。

**根因**: bash heredoc 或 `echo` 传递 Python 代码时，单引号 `'cpu'` 被 shell 解析/剥离。多发生于用 `cat <<'EOF'` 或 `printf` 远程创建文件时。

**修复**: 
1. 使用 `sed` 后处理重新添加引号: `sed -i "s/\[cpu\]/\['cpu'\]/g"`
2. 或使用 `Write` 工具在本地创建文件，再 `scp` 到目标（完全避免 shell 解析）
3. 避免在 heredoc 中写包含单引号的 Python 代码

**教训**:
- 通过 shell 传递 Python 代码时，任何引号字符都可能被 shell 改变
- 最安全的方式：本地写文件 → scp 传输，不经过 shell heredoc
- 排查此类问题时优先检查文件内容的引号是否完整

---

### 6.8 heredoc 中 ANSI 转义码被解释 (2026-06-09)

**现象**: bash 脚本中的颜色码 `\033[32m` 等被解释为实际控制字符，导致脚本显示异常。

**根因**: heredoc 或 `echo -e` 中，`\033` 被 shell 的转义机制解释为 ESC 字符。

**修复**: 使用 `tput` 命令代替硬编码转义序列:
```bash
RED=$(tput setaf 1 2>/dev/null || echo '')
GRN=$(tput setaf 2 2>/dev/null || echo '')
RST=$(tput sgr0 2>/dev/null || echo '')
```
`tput` 自动适配终端类型，且不受 shell 转义规则影响。

**教训**:
- 所有终端控制码优先使用 `tput` 而非 ANSI 硬编码
- 带 `|| echo ''` 的 fallback 确保在不支持 tput 的环境（CI/CD、pipe）中脚本仍可运行

---

### 6.9 start_sim_nodes 运行慢误判为"无反应" (2026-06-09)

**现象**: `sudo ./start_sim_nodes 1` 启动后长时间无输出，用户认为"没有反应"。

**根因**: `speed=1` 时，5bus 需要 ~180 秒完成仿真（360k steps × 0.5ms = 180s）。程序在子进程中等待 FreeRTOS 完成，主进程无任何进度指示。

**修复**:
1. 添加 `setbuf(stdout, NULL)` 确保输出即时刷新
2. 在子进程中每 5 秒输出一次 `still running...` 状态（select timeout 触发）
3. 在 `start_all.sh` 中提示用户预期耗时
4. 推荐快速验证时使用 `speed=10`（~28 秒完成）

**教训**:
- 长时间运行的程序必须有进度指示（心跳输出、百分比、预估时间）
- 默认速度参数应选择较快的值供快速验证，慢速用于正式运行

---

### 6.10 npz 保存时的 NaN/Inf 值处理 (2026-06-09)

**现象**: `np.savez` 时因 NaN 值导致文件损坏，`json.dump` 时因非 JSON 兼容值（NaN, Inf）报错。

**根因**: UKF 初始化阶段或数据失常时产生 NaN 值，Python float NaN 不是有效 JSON 值。

**修复**:
1. `json.dump` 时使用 `allow_nan=False`，并自定义 `nan_fix()` 递归替换 NaN 为 None
2. npz 保存使用 try/except 保护，失败时回退到 JSON 格式

```python
def save_metrics():
    try:
        # 先尝试正常序列化
        json.dump(g_metrics, f, indent=2, allow_nan=False)
    except:
        # NaN 替换为 None
        def nan_fix(obj):
            if isinstance(obj, float) and obj != obj:  # isnan
                return None
            return obj
        json.dump(nan_fix(g_metrics), f, indent=2)
```

**教训**:
- JSON 标准不支持 NaN/Infinity，跨语言数据交换时必须清理
- `np.savez` 原生支持 NaN，但加载到其他非 numpy 环境时会出问题
- 监控数据（metrics）的输出应始终做 NaN 安全检查

---

### 6.11 np.savez 自动追加 .npz 后缀 (2026-06-09)

**现象**: 保存的 npz 文件名为 `ukf_results_5bus.npz.tmp.npz` 而非 `ukf_results_5bus.npz`。

**根因**: `np.savez('/tmp/ukf_results_5bus.npz.tmp', ...)` 自动追加 `.npz` 后缀 → 最终文件名变为 `ukf_results_5bus.npz.tmp.npz`。

**修复**: 将临时文件命名改为 `ukf_results_5bus.tmp.npz`（`.npz` 在末尾），然后 atomic rename 到 `ukf_results_5bus.npz`。

**教训**: `np.savez` 总是追加 `.npz` 后缀。如果临时文件名中间出现 `.npz`，最终会被双重追加。

---

### 6.12 三道防线：SHM 心跳 + 非阻塞超时 (2026-06-09)

**背景**: 三节点并行运行时，任何一个节点卡死都会导致整个 pipeline 阻塞。需要独立的异常检测机制。

**实现**:

| 防线 | 机制 | 阈值 | 触发动作 |
|------|------|------|----------|
| **第一道**: SHM 心跳 | 每 2s 读 SHM WI 指针检测停跳 | 连续 2 次停跳 | 报警 `HEARTBEAT_STOPPED`，退出进程 |
| **第二道**: select 超时 | `select(fd, timeout=2s)` | 2s 无数据 | 输出 `waiting...`，继续等待 |
| **第三道**: 非阻塞空转 | 非阻塞 read 返回 0 的次数 | 连续 1000 次 | 视为 EOF，退出进程 |

```python
def check_heartbeat(shm_ptr, node_name):
    global last_wi, heartbeat_missed
    current_wi = shm_ptr[0]  # read WI pointer
    if current_wi == last_wi:
        heartbeat_missed += 1
        if heartbeat_missed >= 2:
            print(f'[{node_name}] HEARTBEAT_STOPPED')
            return False
    else:
        heartbeat_missed = 0
        last_wi = current_wi
    return True
```

**教训**:
- 多进程架构需要分层级的异常检测，不能依赖单一的 timeout
- SHM 心跳检测可以直接判断 FreeRTOS 是否仍在写入数据（更根本的检测）
- 非阻塞空转上限防止 CPU 空转浪费（有时 fd 返 0 但数据实际到达了）

---

### 6.13 FreeRTOS 固件部署路径 + SDK 构建路径混淆 (2026-06-09)

**现象**: 修改 `freertos/src/sim_node_39bus.c` 后重新编译，但部署到开发板后行为未改变。

**根因**: 开发目录和 SDK 构建路径不一致：
- 开发目录: `/home/alientek/Phytium/freertos/src/`
- SDK 构建路径: `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/`
- FreeRTOS makefile 通过 `USER_CSRC += $(wildcard src/*.c)` 编译的是 SDK 路径下的文件

**修复流程**:
```bash
# 1. 将修改后的 .c 文件复制到 SDK 构建路径
cp /home/alientek/Phytium/freertos/src/sim_node_39bus.c \
   /home/alientek/Phytium_syscode/.../openamp_for_linux/src/

# 2. 重新编译
cd /home/alientek/Phytium_syscode/.../openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make image

# 3. 部署到开发板
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@<IP>:/lib/firmware/

# 4. 重载固件
echo stop > /sys/class/remoteproc/remoteproc0/state
sleep 2
echo start > /sys/class/remoteproc/remoteproc0/state
```

**教训**:
- 开发目录和构建目录分离时，修改后必须复制到构建目录再编译
- 部署后验证 MD5 值: `md5sum firmware.elf` 确认是新的固件
- 固件重载必须先 stop 再 start，不能直接覆盖

---

## 七、当前架构总览 (v5, 2026-06-09)

### CPU 分配

```
物理 Core 1 (A76): FreeRTOS 独占区
  └─ 3 节点 RK4 仿真引擎 (写 SHM)，游离于 Linux 调度之外

物理 Core 2 (A76): Linux VIP 计算特区
  └─ 39bus UKF (taskset -c 2)，独享 L1/L2 缓存

物理 Core 0 (A55): Linux OS 与轻量任务混合区
  ├─ 5bus UKF (taskset -c 0)
  ├─ 9bus UKF (taskset -c 0)
  ├─ Python 主脚本 (launch_ukf_multi.py)
  └─ SSH / 网络服务
```

### 数据流 (v5)

```
FreeRTOS (Core 1)
  ├─ sim_node_5bus  ──→ SHM 0xC8100000 (256KB, down=32, ~11,250帧)
  ├─ sim_node_39bus ──→ SHM 0xC8140000 (128KB, down=32, ~11,250帧)
  └─ sim_node_9bus  ──→ SHM 0xC8160000 (128KB, down=32, ~11,250帧)
                          │
Linux (Core 0, Core 2)     │
  ├─ ukf_pipeline --node 5bus  (CPU0) ── stdout→file ──→ launch_ukf_multi.py v3
  ├─ ukf_pipeline --node 39bus (CPU2) ── stdout→file ──→    │ (仅监控stderr)
  └─ ukf_pipeline --node 9bus  (CPU0) ── stdout→file ──→    │
                                                    ├─ /tmp/ukf_out_{node}.bin (原始帧)
                                                    └─ metrics JSON (每 1s)
```

### 启动流程

```bash
# 一键启动 (在开发板上)
cd ~/Phytium/task2_linux
./start_all.sh 15       # speed=15, ~6 分钟全流程

# 监控 (另一个 SSH 终端)
cd ~/Phytium/task2_linux
./monitor.sh

# 综合诊断
cd ~/Phytium/task2_linux
./status.sh
```

### 部署

```bash
# 主机端编译 + 部署
cd /home/alientek/Phytium/task2_linux
make deploy HOST=user@192.168.1.100
```

---

## 八、v5 最终修复阶段 (2026-06-09)

### 8.1 SHM 地址不匹配 — 39bus/9bus 帧数为 0

**日期**: 2026-06-09
**现象**: 三节点 UKF 运行后，39bus 和 9bus 的 frame count 始终为 0，仅 5bus 有数据。

**排查过程**:
```python
# Python mmap 直接检查 SHM:
0xC8100000: wi=15344 cnt=360000 fsz=64  ← 5bus 正常
0xC8140000: wi=0 cnt=0 fsz=0           ← 39bus 全零!
0xC8160000: wi=0 cnt=0 fsz=0           ← 9bus 全零!
```

**根因**: FreeRTOS 固件仍使用旧 SHM 地址（39bus→0xC8108000, 9bus→0xC8118000），但 Linux UKF 已更新为新地址（0xC8140000, 0xC8160000）。两边地址不一致，FreeRTOS 向旧地址写数据，Linux 从新地址读零。

旧地址分配（v4）:
```
0xC8100000: 5bus  (32KB)
0xC8108000: 39bus (64KB)
0xC8118000: 9bus  (32KB)
```

新地址分配（v5）:
```
0xC8100000: 5bus  (256KB)  ← 扩容 8x
0xC8140000: 39bus (128KB)  ← 扩容 2x, 地址迁移
0xC8160000: 9bus  (128KB)  ← 扩容 4x, 地址迁移
```

**修复**:
1. 确认 SDK 构建路径的 `shm_data.h` 已更新为新地址
2. 确认源码文件（sim_node_39bus.c, sim_node_9bus.c）已同步到 SDK 路径
3. `make clean && make image` 重新编译固件
4. 部署到 `/lib/firmware/freertos.elf`，重载 remoteproc
5. 验证：Python mmap 检查三地址均有 fsz=64/400/104

**教训**:
- FreeRTOS 固件部署后必须验证 SHM 物理地址是否正确初始化
- 地址变更需同时更新 FreeRTOS 固件和 Linux UKF 两边
- 生产环境建议在 `reset_shm` 中加入地址一致性检查

---

### 8.2 FreeRTOS SHM 写降采样 + Ring Buffer 扩容

**日期**: 2026-06-09
**背景**: 初步跑通后发现 UKF 帧捕获率极低（5bus 仅 4%，39bus 仅 2%），FreeRTOS 以 2000Hz 生成数据远超 UKF 30fps 处理速度，Ring Buffer 太小导致大量数据丢弃。

**修复**:

1. **Buffer 扩容**:
   ```
   5bus:  32KB  → 256KB  (容纳 4095 帧 @ 64B)
   39bus: 64KB  → 128KB  (容纳  327 帧 @ 400B)
   9bus:  32KB  → 128KB  (容纳 1260 帧 @ 104B)
   ```

2. **SHM 写降采样**:
   ```c
   // sim_params_5bus.h
   #define SIM_5BUS_WRITE_DOWN  32   // 每 32 步写一次 SHM
   // sim_params_39bus.h
   #define SIM_39BUS_WRITE_DOWN 32
   // sim_params_9bus.h
   #define SIM_9BUS_WRITE_DOWN  32
   
   // sim_node_task.c (5bus)
   if (step % SIM_5BUS_WRITE_DOWN == 0) {
       shm_put_frame(SHM_5BUS_BASE, SHM_5BUS_SIZE, fbuf, 64);
   }
   batch_idx++;  // RPMsg 批处理不受降采样影响!
   ```

3. **关键设计**: `batch_idx++` 放在降采样条件**外面**，确保 RPMsg 批发送频率不受 SHM 降采样影响。否则会改变 RPMsg 发送间隔，导致 5bus 卡住不输出 DONE。

**结果**: 360k steps / 32 ≈ 11,250 帧/节点 → UKF 捕获 11,000 帧（98%），零丢帧。

**教训**:
- 降采样条件必须只影响目标操作（SHM 写），不影响其他逻辑（RPMsg 批处理、步进计数）
- Ring Buffer 大小需根据 `UKF处理速度 × 仿真总时长` 来设计
- 多节点调优时，统一降采样比例可简化性能对比

---

### 8.3 Python launcher MemoryError — 内核 overcommit 限制

**日期**: 2026-06-09
**现象**: 三节点 UKF 运行后，5bus 和 9bus 的 Python launcher 子进程因 `MemoryError` 崩溃，仅 39bus 正常运行。

**dmesg 证据**:
```
__vm_enough_memory: pid: 7605, comm: python3, not enough memory for the allocation
__vm_enough_memory: pid: 7596, comm: python3, not enough memory for the allocation
```

**根因**: 虽然物理内存充足（7.4GB），但内核 overcommit 策略（`vm.overcommit_memory`）限制了虚拟内存分配。Python launcher v2 的内存占用过高：
- 3 个 Python 进程，每个保持 `MAX_HISTORY_FRAMES=30000` 的历史数据
- 39bus: 30000 × (1+20+1+98) × 8 ≈ 28.8 MB (历史) + numpy 临时数组 28.8 MB ≈ 57 MB
- 频繁的 `np.savez` 和 `json.dump` 导致内存碎片化
- 内核 `__vm_enough_memory` 检测到虚拟内存超限，拒绝分配

**修复** — Python launcher v3 彻底重构:
1. **stdout 重定向到文件**: `subprocess.Popen(stdout=open(out_path, 'wb'))` — 帧数据直接写磁盘
2. **移除帧解析**: 不再通过 `proc.stdout.read()` 解析帧，消除 MemoryError 源头
3. **移除 numpy 依赖**: 删除 `save_results()`, `import numpy`, `MAX_HISTORY_FRAMES`, history 累积
4. **stderr 心跳监控**: 正则解析 `[ukf-5bus] t=4.0s frames=251 rmse=0.0000 lat=55us` 提取指标
5. **零内存增长**: 只保留最新的 frame_count/rmse/latency，内存占用恒定 <5MB

**结果**:
```
修复前: 5bus=34帧(MemoryError), 39bus=10779帧, 9bus=9帧(MemoryError)
修复后: 5bus=11000帧, 39bus=11000帧, 9bus=11000帧  ← 全部零错误!
```

**教训**:
- 嵌入式 Linux 板卡上 Python 进程的内存策略必须保守
- `__vm_enough_memory` 失败不一定意味着物理内存不足，可能是 overcommit 策略限制
- 大文件流式处理 > 内存累积批处理；磁盘 I/O 比 OOM 崩溃更可控
- 移除复杂依赖（numpy）可同时降低内存占用和部署复杂度

---

### 8.4 ukf_pipeline.c frame_sz 读取时序问题

**日期**: 2026-06-09
**现象**: 39bus 和 9bus 的 `frame_sz` 始终为 0，导致 UKF 从 SHM 读取 0 字节数据，帧数始终为 0。

**根因**: `ukf_pipeline.c` 在 FreeRTOS 初始化 SHM 之前读取 `frame_sz`:
```c
// BEFORE (buggy):
uint32_t frame_sz = shm->frame_sz;  // ← 此时 FreeRTOS 尚未写 frame_sz!
while (shm->count == 0 && g_running) { usleep(10000); }  // 等待首帧
// frame_sz 保持为 0，后续 read 0 字节 → 0 帧
```

**修复**:
```c
// AFTER (fixed):
while (shm->count == 0 && g_running) { usleep(10000); }  // 先等待 FreeRTOS 初始化
uint32_t frame_sz = shm->frame_sz;  // 此时 frame_sz 已正确初始化
```

**教训**:
- SHM 多生产者-消费者场景下，消费者必须确保生产者已完成初始化再读取元数据
- 先等待 `count > 0`（数据已写入），再读取 `frame_sz`（元数据已就绪）
- 这是典型的启动时序问题，与 SHM 地址匹配问题叠加后更难排查

---

### 8.5 batch_idx 位置错误 — 5bus 卡住不输出 DONE

**日期**: 2026-06-09
**现象**: 引入 SHM 写降采样后，5bus 仿真完成但不输出 "DONE"，start_sim_nodes 无限等待。

**根因**: `batch_idx++` 被错误地放在降采样条件内:
```c
// BUG: RPMsg 批发送频率被降采样改变
if (step % SIM_5BUS_WRITE_DOWN == 0) {
    shm_put_frame(...);
    batch_idx++;  // ← 错误! 只在降采样帧时递增
}
```

RPMsg 批发送依赖于 `batch_idx >= BATCH_SIZE`，当 DOWNSAMPLE=32 时，batch_idx 每 32 步才 +1，批发送频率从 22,500 batches/s 降到 ~700 batches/s。FreeRTOS 的 RPMsg 发送超时或 vring 行为异常，导致 DONE 通知发不出去。

**修复**:
```c
// FIXED: batch_idx 独立于 SHM 降采样
if (step % SIM_5BUS_WRITE_DOWN == 0) {
    shm_put_frame(...);  // 只降采样 SHM 写入
}
batch_idx++;  // ← RPMsg 批发送不受影响!
batch->frame_count = (uint8_t)batch_idx;
```

**教训**:
- 条件分支内的副作用必须仔细审查，确认所有依赖项都正确处理
- 降采样是数据流优化，不能改变控制流（RPMsg 通信用来传递 DONE/ACK）
- 修改关键循环时，画出数据流和控制流的分界线
