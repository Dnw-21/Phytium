# Phytium UKF Distributed State Estimation

基于飞腾 PE2204 异构多核平台的电力系统分布式无迹卡尔曼滤波（UKF）实时状态估计系统。

---

## 项目简介

本项目实现了一套可在飞腾 PE2204 开发板上实时运行的多节点状态估计系统：

- **AMP 异构架构**：CPU1 运行 FreeRTOS 实时仿真，CPU0/CPU2 运行 Linux UKF 状态估计。
- **多规模电网模型**：同时支持 5bus、9bus、39bus 三种 IEEE 标准测试系统。
- **FT 加速库优化**：可选使用飞腾自主 BLAS-FT / LAPACK-FT 库加速 39bus 大系统矩阵运算。
- **降频扩展**：通过降低数据生成频率释放 CPU 资源，在单 Linux 节点上叠加多个并发 UKF 实例。
- **真实场景覆盖**：提供 8 种典型节点布局组合测试，筛选推荐部署方案。

---

## 目录结构

```text
Phytium_UKF_Distributed_State_Estimation/
├── README.md                       # 本文件
├── linux/                          # Linux 侧代码
│   ├── src/                        # 源代码
│   │   ├── ukf_pipeline_online.c   # UKF 主程序入口
│   │   ├── ukf_online_core.h       # UKF 核心算法头文件
│   │   ├── start_sim_nodes.c       # FreeRTOS RPMsg 数据桥接
│   │   ├── reset_shm.c             # 共享内存复位工具源码
│   │   └── Makefile                # Linux 侧编译脚本
│   ├── scripts/                    # 运行脚本
│   │   ├── multi_node_combo_test.sh    # 单场景多节点测试
│   │   ├── run_combo_suite_safe.sh     # 批量场景测试套件
│   │   └── reload_firmware.sh          # FreeRTOS 固件热重载
│   └── bin/                        # 预编译可执行文件
│       ├── ukf_pipeline_5bus       # 普通版 5bus UKF
│       ├── ukf_pipeline_9bus       # 普通版 9bus UKF
│       ├── ukf_pipeline_39bus      # 普通版 39bus UKF
│       ├── ukf_pipeline_5bus_ft    # FT 优化版 5bus UKF
│       ├── ukf_pipeline_9bus_ft    # FT 优化版 9bus UKF
│       ├── ukf_pipeline_39bus_ft   # FT 优化版 39bus UKF
│       ├── start_sim_nodes         # RPMsg 数据桥接
│       └── reset_shm_arm64         # 共享内存复位工具
├── freertos/                       # FreeRTOS 侧代码
│   ├── main.c                      # FreeRTOS 入口（openamp_for_linux 示例）
│   ├── inc/                        # 头文件
│   │   ├── sim_params_5bus.h       # 5bus 仿真参数（本项目修改）
│   │   ├── sim_params_9bus.h       # 9bus 仿真参数（本项目修改）
│   │   ├── sim_params_39bus.h      # 39bus 仿真参数（本项目修改）
│   │   ├── sim_node_task.h         # 仿真节点任务接口
│   │   ├── sim_node_9bus.h         # 9bus 节点接口
│   │   ├── sim_node_39bus.h        # 39bus 节点接口
│   │   ├── sim_math.h              # 仿真数学工具
│   │   ├── data_frame.h            # RPMsg 数据帧格式
│   │   └── ...                     # openamp_for_linux 示例其他头文件
│   └── src/                        # 源文件
│       ├── sim_node_task.c         # 仿真节点任务主控
│       ├── sim_node_5bus.c         # 5bus 数据生成
│       ├── sim_node_9bus.c         # 9bus 数据生成
│       ├── sim_node_39bus.c        # 39bus 数据生成
│       ├── sim_math.c              # 仿真数学工具
│       ├── data_frame.c            # RPMsg 数据帧处理
│       └── ...                     # openamp_for_linux 示例其他源文件
└── firmware/                       # FreeRTOS 固件
    └── openamp_core0.elf           # 需要放置到 /lib/firmware/（运行时）
```

---

## 文件关系说明

```text
[FreeRTOS 侧]
    sim_params_*.h  →  控制三种电网的仿真步长、SHM 写入间隔
           ↓
    openamp_core0.elf  →  编译后的 FreeRTOS 固件，运行在 CPU1
           ↓ (RPMsg / shared memory)
[Linux 侧]
    start_sim_nodes  →  打开 RPMsg 通道，启动三种仿真节点
           ↓ (写入 /tmp/ukf_*.csv 与 /tmp/ukf_*.log)
    ukf_pipeline_5bus/9bus/39bus  →  读取共享数据，执行 UKF 状态估计
           ↓
    multi_node_combo_test.sh  →  启动基础节点 + N 个只读实例，汇总单场景结果
           ↓
    run_combo_suite_safe.sh  →  批量调用 multi_node_combo_test.sh，生成汇总表
```

---

## 运行环境

- 硬件：飞腾 PE2204 开发板
- Linux：6.6.63-phytium-embedded-v3.2（或兼容版本）
- FreeRTOS：Phytium FreeRTOS SDK AMP 示例
- FT 库（可选）：`/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0` 与 `LAPACK-FT_v1.4.0`

---

## 快速开始

### 1. 上电并登录

```bash
ssh user@192.168.88.10
cd /home/user/Phytium_UKF_Distributed_State_Estimation/linux
```

### 2. 部署固件（首次或修改参数后）

将 `firmware/openamp_core0.elf` 放置到开发板 `/lib/firmware/openamp_core0.elf`，然后执行：

```bash
sudo ./scripts/reload_firmware.sh /lib/firmware/openamp_core0.elf
```

### 3. 启动数据桥接

```bash
sudo ./bin/start_sim_nodes 1
```

### 4. 启动基础三节点

```bash
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./bin/ukf_pipeline_5bus  > /tmp/ukf_5bus.csv  2> /tmp/ukf_5bus.log  &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./bin/ukf_pipeline_9bus  > /tmp/ukf_9bus.csv  2> /tmp/ukf_9bus.log  &
nohup sudo taskset -c 2 env UKF_DELTT=0.004 ./bin/ukf_pipeline_39bus > /tmp/ukf_39bus.csv 2> /tmp/ukf_39bus.log &
```

### 5. 查看结果

```bash
tail -f /tmp/ukf_5bus.log /tmp/ukf_9bus.log /tmp/ukf_39bus.log
top -1 -n 1 | head -10
```

### 6. 多节点组合测试

```bash
sudo ./scripts/multi_node_combo_test.sh 2 0 0 25   # 3×5bus + 1×9bus + 1×39bus
cat /tmp/combo_2_0_0_25.log
```

批量测试：

```bash
sudo ./scripts/run_combo_suite_safe.sh 25
cat /tmp/combo_summary_safe.txt
```

---

## 编译说明

### Linux 侧

在 `linux/src/` 目录下：

```bash
cd linux/src
make clean
make
```

- 默认编译普通版可执行文件：`ukf_pipeline_5bus`、`ukf_pipeline_9bus`、`ukf_pipeline_39bus`
- 默认同时编译 FT 优化版可执行文件：`ukf_pipeline_5bus_ft`、`ukf_pipeline_9bus_ft`、`ukf_pipeline_39bus_ft`
- 若要编译 FT 优化版，需确保已安装 BLAS-FT / LAPACK-FT，路径默认指向 `/home/alientek/Phytium/fc_lib`

编译产物在当前目录，可手动复制到 `linux/bin/`，或直接执行 `make deploy` 部署到开发板。

### FreeRTOS 侧

将 `freertos/inc/sim_params_*.h` 覆盖到 Phytium FreeRTOS SDK 对应目录：

```text
phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/
```

然后按 SDK 说明重新编译 `openamp_for_linux` 示例，生成新的 `openamp_core0.elf`。

> 说明：`freertos/src/` 与 `freertos/inc/` 中除 `sim_params_*.h` 外的其他文件，均来自 Phytium FreeRTOS SDK 的 `example/system/amp/openamp_for_linux` 示例，未做修改。本项目的核心修改为 `sim_params_*.h` 中的数据生成频率参数。

---

## 关键参数说明

| 参数 | 文件 | 含义 |
|------|------|------|
| `SIM_5BUS_WRITE_DOWN` | `freertos/inc/sim_params_5bus.h` | 5bus 每 N 步写入 SHM，当前为 4，对应 500Hz |
| `SIM_9BUS_WRITE_DOWN` | `freertos/inc/sim_params_9bus.h` | 9bus 每 N 步写入 SHM，当前为 4，对应 500Hz |
| `SIM_39BUS_WRITE_DOWN` | `freertos/inc/sim_params_39bus.h` | 39bus 每 N 步写入 SHM，当前为 8，对应 250Hz |
| `UKF_DELTT` | Linux 环境变量 | UKF 时间步长，0.002=2ms，0.004=4ms |
| `UKF_READONLY` | Linux 环境变量 | 1=只读实例，消费数据但不更新 SHM 读指针 |

---

## 注意事项

1. **CPU3 不可用**：PE2204 的 CPU3 被底层 ATF/U-Boot 固件保留，当前 Linux 只能使用 CPU0/CPU1/CPU2。
2. **避免持续满载**：虽然 CPU 占用可能很高，但应保证没有单核持续 100% 导致系统无响应。
3. **FT 库路径**：编译 FT 版本时需要确保 `BLAS-FT_v1.5.0` 和 `LAPACK-FT_v1.4.0` 已正确安装在 `/home/alientek/Phytium/fc_lib/` 目录下。
4. **固件来源**：`firmware/openamp_core0.elf` 需要自行放置到开发板 `/lib/firmware/` 目录，或从开发板中导出。

---

## 作者

好运爆棚队
