# Task2 代码索引 & 目录结构

> 集中维护目录：`/home/alientek/Phytium/task2_linux/`
> 所有 Task2 相关开发、调试、部署的入口。

---

## 一、VM 端核心目录

### 1. 本目录 `/home/alientek/Phytium/task2_linux/`

| 文件 | 说明 |
|------|------|
| `stress_monitor.c` | **高并发压力测试 + perf 监控**（阶段3核心） |
| `ukf_pipeline.c` | UKF 流水线主程序 |
| `ukf_pipeline_online.c` | 在线版 UKF 流水线 |
| `ukf_core.c` / `ukf_core.h` | UKF 核心算法 |
| `ukf_online_core.h` | 在线版 UKF 核心头文件 |
| `start_sim_nodes.c` | 仿真节点启动器 |
| `shm2csv.c` | 共享内存 → CSV 导出 |
| `reset_shm.c` | 共享内存重置 |
| `sim_params_5bus.h` / `sim_params_9bus.h` / `sim_params_39bus.h` | 各节点仿真参数 |
| `Makefile` | 编译 Makefile |
| `deploy_to_board.sh` | 部署到开发板脚本 |
| `run_deploy.sh` | 一键部署 & 运行 |
| `reload_firmware.sh` | FreeRTOS 固件热加载 |
| `start_all.sh` / `status.sh` / `monitor.sh` | 启动/状态/监控脚本 |
| `launch_ukf_multi.py` | 多节点 UKF Python 启动器 |
| `dashboard_server_v2.py` | Web 面板后端 |
| `CODE_INDEX.md` | **本文件：代码索引** |
| `STRESS_TEST_REPORT.md` | **阶段3压力测试报告** |
| `deploy_fc_lib.sh` | **fc_lib 飞腾优化库一键部署脚本** |

### 1.1 eBPF 监控 `/home/alientek/Phytium/task2_linux/ebpf/`

| 文件 | 说明 |
|------|------|
| `ukf_monitor.bpf.c` | eBPF 内核态：调度延迟 + IRQ延迟 + 软中断延迟 |
| `ukf_monitor.c` | eBPF 用户态加载器，终端实时显示 |
| `ukf_monitor.h` | 共享头文件（BPF maps 定义） |
| `Makefile` | 编译（含 x86_64 / ARM64 交叉编译） |
| `vmlinux.h` | 内核 BTF 类型定义（由 bpftool 生成） |

### 2. 多节点 UKF 控制器 `/home/alientek/Phytium/multi_node/`

| 子目录 | 节点 | 版本 |
|--------|------|------|
| `DSE_Case5_Overbye_3min_Online_C/` | 5bus | `controller_online_5bus_opt.c` (标准版), `controller_online_5bus_ft.c` (FT优化版) |
| `DSE_Calculation_UKF_9case_3minc_implementation/` | 9bus | `controller_online_9bus_opt.c` (标准版), `controller_online_9bus_ft.c` (FT优化版) |
| `DSE_Case39_3min_Online_C/` | 39bus | `controller_online_39bus_opt.c` (标准版), `controller_online_39bus_ft.c` (FT优化版) |
| `shm_direct.h` | 公共 | SHM 直接读取头文件，定义各节点 SHM 地址和帧大小 |

### 3. FreeRTOS 固件 `/home/alientek/Phytium/freertos/`

| 文件 | 说明 |
|------|------|
| `main.c` | FreeRTOS 主入口 |
| `src/sim_node_5bus.c` / `sim_node_9bus.c` / `sim_node_39bus.c` | 各节点仿真模型 |
| `src/sim_node_task.c` | 仿真任务调度 |
| `inc/shm_data.h` | SHM 数据布局定义 |
| `inc/sim_params_*.h` | 仿真参数 |
| `TASK2_DEVELOPMENT_PLAN.md` | 开发计划（CPU分配方案） |
| `TASK2_DEBUG_LOG.md` | 调试日志 |

### 4. FT 飞腾优化库 `/home/alientek/Phytium/fc_lib/`

| 库 | 路径 | 内容 |
|----|------|------|
| **BLAS-FT** | `BLAS-FT_v1.5.0/` | `include/cblas.h`, `lib/libblas_ft.so`, `lib/libblas_ft.a` |
| **LAPACK-FT** | `LAPACK-FT_v1.4.0/` | `include/lapacke.h`, `include/lapack.h`, `lib/liblapack.so` |
| **VML-FT** | `VML-FT_v1.4.0/` | `include/vml-ft.h`, `lib/libvml-ft.so` |
| **VSIPL-FT** | `VSIPL-FT_v1.13.0_2.28/` | `include/vsip.h`, `include/VU.h`, `lib/libvsip.so`, `lib/libVU.a` |

> FT 版本（`*_ft.c`）编译需要 BLAS-FT + LAPACK-FT + VML-FT 头文件和库，目录由此提供。

### 5. 文档 `/home/alientek/Phytium/docs/`

| 文件 | 说明 |
|------|------|
| `architecture.md` | 系统架构 |
| `communication-flow.md` | 通信流程 |
| `setup-guide.md` | 环境搭建 |
| `operations-guide.md` | 操作指南 |
| `debug-log.md` | 调试记录 |
| `knowledge-base.md` | 知识库 |

### 6. 部署脚本 `/home/alientek/Phytium/scripts/`

| 文件 | 说明 |
|------|------|
| `deploy.sh` | 通用部署 |
| `deploy_and_start.sh` | 部署并启动 |
| `start-openamp.sh` / `stop-openamp.sh` | OpenAMP 启停 |

---

## 二、开发板端目录结构

| 路径 | 说明 |
|------|------|
| `/home/user/` | **主工作目录** |
| `/home/user/stress_monitor` | 压力测试程序（编译后） |
| `/home/user/stress_monitor.c` | 压力测试源码 |
| `/home/user/controller_online_5bus_opt` | 5bus UKF 二进制 |
| `/home/user/controller_online_9bus_opt` | 9bus UKF 二进制 |
| `/home/user/controller_online_39bus_opt` | 39bus UKF 二进制 |
| `/home/user/ukf/` | UKF 头文件目录 |
| ├─ `ukf/shm_direct.h` | SHM 头文件 |
| ├─ `ukf/include/` | fc_lib 头文件（cblas.h, lapacke.h, vml-ft.h） |
| ├─ `ukf/ukf_core_5.h` | 5bus UKF 核心 |
| ├─ `ukf/ukf_core_9_opt.h` | 9bus UKF 核心 |
| ├─ `ukf/ukf_core_39_opt.h` | 39bus UKF 核心 |
| `/home/user/build5/` / `build9/` / `build39/` | 各节点编译目录 |
| `/home/user/Phytium/task2_linux/` | 部署脚本目录 |
| ├─ `reload_firmware.sh` | 固件热加载 |
| ├─ `freertos_v2.elf` / `freertos_v3.elf` | FreeRTOS ELF 固件 |
| `/lib/firmware/openamp_core0.elf` | 当前加载的 FreeRTOS 固件 |
| `/sys/class/remoteproc/remoteproc0/` | remoteproc 控制接口 |

---

## 三、CPU 分配方案

| CPU | 用途 | 状态 |
|-----|------|------|
| CPU0 | UKF 进程 | 可用 |
| CPU1 | FreeRTOS 固件（remoteproc） | **保留，不可用** |
| CPU2 | UKF 进程 | 可用 |
| CPU3 | — | **离线，不可用** |

---

## 四、编译命令速查

```bash
# FT版本（线性代数优化，需要fc_lib）
gcc -D_GNU_SOURCE -O2 -std=c99 \
  -I/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0/include \
  -I/home/alientek/Phytium/fc_lib/LAPACK-FT_v1.4.0/include \
  -I/home/alientek/Phytium/fc_lib/VML-FT_v1.4.0/include \
  -o controller controller.c \
  -L/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0/lib -lblas_ft \
  -L/home/alientek/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib -llapack \
  -L/home/alientek/Phytium/fc_lib/VML-FT_v1.4.0/lib -lvml-ft \
  -lm

# OPT版本（纯C实现，无需外部库）
gcc -D_GNU_SOURCE -O2 -std=c99 -o controller controller.c -lm
```