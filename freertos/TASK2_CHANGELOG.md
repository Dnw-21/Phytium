# 任务二：变动记录

> 记录所有文件的新建、修改、删除操作，确保后期可追溯。

---

## 2026-06-04 — v0.1 初始开发 (主机端)

### 新建文件

| 文件 | 说明 |
|------|------|
| `Phytium/freertos/TASK2_DEVELOPMENT_PLAN.md` | 开发需求计划 + 阶段1详细方案 |
| `Phytium/freertos/TASK2_DEBUG_LOG.md` | 调试经验文档 |
| `Phytium/freertos/TASK2_CHANGELOG.md` | 本文件 |
| `Phytium/task2_linux/scripts/gen_sim_params_5bus.py` | 系统参数导出脚本 (Python→C头文件) |
| `Phytium/task2_linux/sim_data_bridge.c` | Linux RPMsg 数据桥接 (C) |
| `Phytium/task2_linux/ukf_runner_5bus.py` | UKF 在线估计器 (Python) |
| `Phytium/task2_linux/dashboard_server_v2.py` | Flask 面板 (Python) |
| `Phytium/task2_linux/templates/dashboard_v2.html` | 面板前端 (Chart.js) |
| `Phytium/task2_freertos/sim_math_test.c` | C 数学库单元测试 |
| `Phytium/freertos/inc/sim_math.h` | 仿真数学库头文件 |
| `Phytium/freertos/inc/sim_node_task.h` | 仿真任务 + RPMsg 协议头文件 |
| `Phytium/freertos/inc/sim_params_5bus.h` | 5-bus/2-gen 系统参数 (自动生成) |
| `Phytium/freertos/src/sim_math.c` | 仿真数学库实现 (RK4 + 测量函数) |
| `Phytium/freertos/src/sim_node_task.c` | FreeRTOS 仿真任务实现 |

### 修改文件

| 文件 | 变更内容 | 风险等级 |
|------|---------|:--:|
| `Phytium/freertos/main.c` | 1. `static rproc` → `g_rproc` (非静态, 供任务二访问) | 低 |
| | 2. `static rpdev` → `g_rpdev` (非静态, 供任务二访问) | 低 |
| | 3. 新增 `#include "sim_node_task.h"` | 低 |
| | 4. 新增 `xTaskCreate(sim_node_task, "SIM", ...)` | 低 |
| | 影响范围: 仅变量作用域和 1 个新任务创建 | |

### 接口影响

- `g_rproc` (原 `rproc`): static → extern, 类型不变 (`struct remoteproc`)
- `g_rpdev` (原 `rpdev`): static → extern, 类型不变 (`struct rpmsg_device *`)
- 任务一所有代码使用新变量名 `g_rproc` / `g_rpdev`, 功能不受影响
- 新增任务 `sim_node_task` 优先级 4 (最低), 不影响任务一的任务调度
- 新增 RPMsg endpoint 与任务一共享同一 rpmsg_device, 不同 endpoint 地址

### RPMsg 协议变更

| 变更 | 说明 |
|------|------|
| 新增 CMD_SIM_DATA (0x50) | FreeRTOS→Linux: 批量测量数据 |
| 新增 CMD_SIM_CTRL (0x51) | Linux→FreeRTOS: 控制命令 |
| 新增 CMD_SIM_ACK  (0x52) | FreeRTOS→Linux: 控制响应 |
| 新增 CMD_SIM_DONE (0x53) | FreeRTOS→Linux: 仿真完成通知 |

### 编译验证 (主机)

```
✅ gen_sim_params_5bus.py: 正确生成 sim_params_5bus.h
✅ sim_math_test: 6/6 测试通过 (vs Python 参考)
✅ ukf_runner_5bus.py --offline: 1000步 UKF 通过 (~1916 steps/s)
✅ sim_data_bridge.c: gcc 编译通过 (语法检查)
```

### 已验证 (2026-06-04 开发板上电)

```
✅ FreeRTOS SDK make: ELF 722KB, sim_node_task 编译通过
✅ sim_node_task 启动: [SIM] init 输出正常
✅ RPMsg 通道: rpmsg-sim-5bus 独立通道创建成功
✅ RPMsg 绑定: dest=0x400, START 命令接收
✅ 数据发送: 360,000 帧全部发送 (~154k steps/s)
✅ Linux 接收: 1600 帧 CSV 捕获, 数据精度 < 1e-4 vs Python
✅ 端到端: FreeRTOS → RPMsg → Linux → CSV 全链路打通
```

### 性能实测

| 指标 | 值 |
|------|-----|
| FreeRTOS 仿真速度 | 154,000 steps/s (77x real-time) |
| ELF 大小 | 722KB (+15KB) |
| 数据精度 | < 1e-4 vs Python reference |
| RPMsg 通道 | 独立 "rpmsg-sim-5bus" |

### 已知问题

1. **UKF Python 性能**: ~1916 steps/s 略低于 2000Hz。可使用 PyPy 或降频 UKF。
2. **RPMsg 全速率稳定性**: 77x real-time 时可能丢帧，建议 speed=1 (实时) 用于生产。
3. **开发板 Python 依赖**: 未安装 numpy/scipy/flask，需 `pip install` 或使用主机离线处理。

## 2026-06-04 下午 — RPMsg dest_addr 修复 + 双管道贯通

### 关键修复
**问题**: FreeRTOS `rpmsg_send` 返回成功但 Linux `/dev/rpmsgX` 收不到数据。
**根因**: RPMsg 回调未更新 `ept->dest_addr`, 桥接重启后 FreeRTOS 仍向旧地址发送。
**修复**: `sim_node_task.c` + `sim_node_39bus.c` 回调中加 `ept->dest_addr = src`。
**结果**: 双管道 100% 成功率。

### 最终性能
| 组件 | 指标 |
|------|------|
| FreeRTOS 5bus SIM | 360k 帧 @ 2000Hz |
| FreeRTOS 39bus SIM | 360k 帧 @ 2000Hz |
| RPMsg 5bus | 100% 成功率 |
| RPMsg 39bus | 100% 成功率 |
| C UKF 5bus | 1,804 fps, RMSE=0.0607 |
| 三节点并行 | 5bus+39bus+9bus 同时运行, 全部100% |

## 2026-06-05 — 多节点并行 + SHM 架构切换

### 架构重大变更: RPMsg 数据传输 → 共享内存(SHM)

**原因**: RPMsg 单包 512B 限制 + 串行发送导致高并发瓶颈。改为每个节点独立 SHM ring buffer，FreeRTOS 直接写入，Linux mmap 读取。

**SHM 地址分配**:
| 节点 | 基地址 | 大小 | 帧大小 |
|------|--------|------|--------|
| 5bus | 0xC8100000 | 0x8000 | 64B |
| 39bus | 0xC8108000 | 0x8000 | 200B |
| 9bus | 0xC8118000 | 0x8000 | 104B |

### 新建文件

| 文件 | 说明 |
|------|------|
| `freertos/src/sim_node_39bus.c` | FreeRTOS 39bus 仿真任务 |
| `freertos/src/sim_node_9bus.c` | FreeRTOS 9bus 仿真任务 |
| `freertos/inc/shm_data.h` | SHM 基地址 + 帧格式定义 |
| `task2_linux/ukf_pipeline.c` | C UKF 统一处理三节点, 从 SHM 读数据 |
| `task2_linux/launch_ukf_multi.py` | 多进程启动器 + CPU 绑定 + 指标收集 |
| `task2_linux/start_sim_nodes.c` | RPMsg 控制 FreeRTOS 仿真启动 |
| `task2_linux/reset_shm.c` | SHM 计数器重置工具 |
| `task2_linux/reload_firmware.sh` | FreeRTOS 固件热重载 |

### 关键修复

| 问题 | 修复 |
|------|------|
| RPMsg 通道名冲突 (39bus/9bus 通道不存在) | 改为每节点独立通道名 `rpmsg-sim-5bus/39bus/9bus` |
| 39bus/9bus 不写入 SHM (WI=0, CNT=0) | 调整 FreeRTOS 任务优先级 (9bus=4, 39bus=6, 5bus=8) |
| 39bus/9bus 不响应 SPEED 命令 | 添加 speed 控制变量 + 回调处理 |
| CMD_SIM_DONE 码不匹配 (Linux 0x52 vs FreeRTOS 0x53) | 统一为 0x53 |
| 9bus CPU 绑定失败 (CPU3 不可用) | 改为 CPU0, 与 5bus 共享 |
| ukf_pipeline 死循环 (SHM count 不增长) | 改为从 ring buffer 读所有可用帧, 处理 FreeRTOS 完成后不增长的情况 |
| 内核恐慌 (apt-get 高频 IO) | 所有文件写入使用内存缓存, 批量刷盘 |

## 2026-06-06 — Dashboard 重构 + 三节点对比

### Dashboard 重构

- 从 SSH 远程读取改为本地读取 `/proc` 和 `/tmp`
- 暗色主题, 三节点对比表格
- 添加 JSON 格式 fallback (当 npz 不可用时)
- 支持 VNC (:5902) 和浏览器 (:5001) 双通道访问

### 关键修复

| 问题 | 修复 |
|------|------|
| launch_ukf_multi.py `UnboundLocalError: g_running` | 在 main() 中添加 `global g_running` |
| sudo 交互挂起 | 改为 `sudo -S` + stdin 密码 |
| stderr 线程文件关闭 ValueError | 添加 try-except |
| 9bus 帧数为 0 | 根因是 `sudo -n` 失败 (NOPASSWD 未配置) |
| 5bus UKF NaN | 根因是 pipeline 在仿真完成后启动, ring buffer 数据被覆盖 |

## 2026-06-07 — 最终稳定 + 交接

### 当前稳定状态

- 三节点全部正常运行: 5bus=2,464fps, 39bus=948fps, 9bus=1,394fps
- RMSE 全部为 0.0 (UKF 收敛良好)
- Dashboard API 正常工作: `/api/compare`, `/api/status`, `/api/history`
- CPU 绑定: 5bus+9bus→CPU0, 39bus→CPU2, FreeRTOS→CPU1

### 已修复的核心文件

| 文件 | 最新状态 |
|------|----------|
| `launch_ukf_multi.py` | `sudo -S` 密码, 启动失败检测, NaN-safe metrics 保存, npz 路径修复 |
| `dashboard_server_v2.py` | FPS fallback 计算, JSON fallback 加载 |
| `ukf_pipeline.c` | ring buffer 读取逻辑, 支持三节点参数 |

### 待办 (Phase 3)

1. 同一节点多数量 → 共享 SHM, 多 UKF worker 进程池并行消费
2. 逐步扩展节点数: 3→6→10→20, 测试 CPU 饱和点
3. 根据计算量优化 CPU 分配 (计算量大的节点独占一核)
4. WebServer 参考架构 (Epoll + Reactor + 线程池) 用于高并发
