# 飞腾 PE2204 UKF 多节点状态估计演示操作指南

> 版本: v1.0  
> 日期: 2026-06-18  
> 适用平台: Phytium PE2204 (ARMv8-A, 4 核 A76/A55 异构)  
> 操作系统: Linux 6.6.63-phytium-embedded-v3.2  
> 演示代码路径: `/home/user/Phytium/task2_linux/`  

---

## 1. 演示目标

本指南用于指导从开发板上电到完成多节点 UKF 状态估计演示的全过程，适用于录制演示视频。演示将展示：

1. 开发板启动并加载 FreeRTOS 固件；
2. Linux 侧启动 5bus / 9bus / 39bus 三种 UKF 状态估计节点；
3. 通过降频与只读实例技术扩展并发节点数；
4. 实时观察 CPU 占用、处理帧数、RMSE 精度与延迟；
5. 通过热重载更新 FreeRTOS 固件。

---

## 2. 硬件与上电准备

### 2.1 硬件连接

| 项目 | 说明 |
|------|------|
| 开发板 | Phytium PE2204 / PhytiumPi |
| 电源 | 12V DC 适配器，确认接口牢固 |
| 串口 | USB 转串口连接板载 DEBUG UART（用于观察启动日志） |
| 网口 | 连接至与 PC 同网段的交换机/路由器，确保 PC 可 SSH 登录 |
| 存储 | 已烧录好 Linux 根文件系统的 SD/eMMC |

### 2.2 上电启动

1. 接通电源，开发板开始启动。
2. 在串口终端（波特率通常为 115200）可看到 U-Boot、Kernel、systemd 启动日志。
3. 等待登录提示出现：

```text
phytiumpi login: user
Password: (输入用户密码)
```

> 本指南默认使用 `user` 账户登录，所有需要 root 权限的操作均通过 `sudo` 执行。

### 2.3 确认网络

登录后确认 IP 地址，确保 PC 可远程 SSH：

```bash
ip addr show eth0
```

示例输出：

```text
3: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ...
    inet 192.168.88.10/24 brd 192.168.88.255 scope global dynamic eth0
```

从 PC 侧测试连通性：

```bash
ping 192.168.88.10
```

### 2.4 多终端操作说明

演示过程中需要同时观察多个窗口，建议打开 **3 个 SSH 终端**（Windows 可用 PowerShell、Windows Terminal、MobaXterm 等，每个窗口独立）：

| 终端 | 用途 | 常驻命令 |
|------|------|----------|
| 终端 1 | 运行 FreeRTOS 数据桥接 `start_sim_nodes` | `sudo ./start_sim_nodes 1` |
| 终端 2 | 启动/停止 UKF 节点、执行测试脚本 | `./ukf_pipeline_*`、`./multi_node_combo_test.sh` |
| 终端 3 | 观察实时日志与 CPU 占用 | `tail -f`、`top -1`、`cat /tmp/combo_*.log` |

> 每个 SSH 会话相互独立，可以在不同窗口同时登录同一台开发板。录制视频时，建议提前调整好三个窗口的大小和布局，方便切换。

---

## 3. 进入演示目录并检查环境

### 3.1 进入工作目录

```bash
cd /home/user/Phytium/task2_linux
ls
```

关键文件说明：

| 文件 | 作用 |
|------|------|
| `ukf_pipeline_5bus` | 5bus 状态估计程序 |
| `ukf_pipeline_9bus` | 9bus 状态估计程序 |
| `ukf_pipeline_39bus` | 39bus 状态估计程序（大系统） |
| `start_sim_nodes` | 启动 FreeRTOS RPMsg 数据桥接 |
| `reset_shm` | 复位共享内存计数器 |
| `reload_firmware.sh` | 热重载 FreeRTOS 固件 |
| `multi_node_combo_test.sh` | 单组多节点组合测试 |
| `run_combo_suite_safe.sh` | 批量多节点组合测试套件 |
| `FT_UKF_OPTIMIZATION_REPORT.md` | 技术报告 |

### 3.2 确认 FreeRTOS 远程处理器状态

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

正常应输出：

```text
running
```

如果显示 `offline`，说明 FreeRTOS 未启动，可执行：

```bash
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

### 3.3 确认当前固件版本（频率配置）

通过 remoteproc trace 查看 39bus 输出频率：

```bash
sudo cat /sys/kernel/debug/remoteproc/remoteproc0/trace0 | grep -i "39bus\|WRITE_DOWN\|FS" | head -20
```

默认演示固件配置：

- 5bus / 9bus：`WRITE_DOWN=4`，等效 **500Hz**
- 39bus：`WRITE_DOWN=8`，等效 **250Hz**

---

## 4. 基础三节点演示

### 4.1 清理旧进程

每次演示前建议先清理：

```bash
cd /home/user/Phytium/task2_linux
sudo pkill -9 -f ukf_pipeline || true
sudo pkill -9 -x start_sim_nodes || true
sudo ./reset_shm
```

### 4.2 启动数据桥接

```bash
sudo ./start_sim_nodes 1
```

该程序负责建立 Linux 与 FreeRTOS 之间的 RPMsg 通道。保持前台运行，打开第二个终端继续后续操作。

### 4.3 启动三个 UKF 节点

在第二个终端中执行：

```bash
cd /home/user/Phytium/task2_linux

# 5bus / 9bus 运行在 CPU0，数据周期 0.002s (500Hz)
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_5bus > /tmp/ukf_5bus.csv 2> /tmp/ukf_5bus.log &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_9bus > /tmp/ukf_9bus.csv 2> /tmp/ukf_9bus.log &

# 39bus 运行在 CPU2，数据周期 0.004s (250Hz)
nohup sudo taskset -c 2 env UKF_DELTT=0.004 ./ukf_pipeline_39bus > /tmp/ukf_39bus.csv 2> /tmp/ukf_39bus.log &
```

### 4.4 观察运行状态

#### 4.4.1 查看进程是否运行

```bash
ps -eo pid,psr,comm,pcpu,args | grep "ukf_pipeline" | grep -v grep
```

预期输出示例：

```text
PID   PSR COMM         %CPU ARGS
1234    0 ukf_pipeline_5b  12.1 ./ukf_pipeline_5bus
1235    0 ukf_pipeline_9b  18.3 ./ukf_pipeline_9bus
1236    2 ukf_pipeline_39  54.5 ./ukf_pipeline_39bus
```

#### 4.4.2 实时查看日志

```bash
# 5bus
tail -f /tmp/ukf_5bus.log

# 9bus
tail -f /tmp/ukf_9bus.log

# 39bus
tail -f /tmp/ukf_39bus.log
```

日志中关键字段：

- `frames`：已处理帧数
- `rmse`：当前均方根误差
- `lat`：单帧处理延迟（微秒）

#### 4.4.3 查看 CPU 占用

```bash
# 总体 CPU 占用
top -1 -n 1 | head -20

# 或查看 /proc/stat
awk '/^cpu[0-2] /{print $0}' /proc/stat
```

在基础三节点场景下，预期：

- CPU0：约 95%–100%（5bus + 9bus + 系统任务）
- CPU1：约 95%–100%（FreeRTOS 数据生成）
- CPU2：约 95%–100%（39bus UKF + 系统任务）

> 说明：虽然总体占用高，但 39bus UKF 单进程占用已从 2kHz 时的约 99% 降至约 54%，不再由单一进程独占核心。

### 4.5 停止演示

```bash
sudo pkill -9 -f ukf_pipeline
sudo pkill -9 -x start_sim_nodes
```

---

## 5. 多节点组合压力测试演示

### 5.1 运行单组组合测试

以“轻量配网”场景为例（3×5bus + 1×9bus + 1×39bus）：

```bash
cd /home/user/Phytium/task2_linux
sudo ./multi_node_combo_test.sh 2 0 0 25
```

参数含义：

- `2`：额外 2 个 5bus 只读实例（总 3 个）
- `0`：额外 0 个 9bus 只读实例
- `0`：额外 0 个 39bus 只读实例
- `25`：测试时长 25 秒

脚本会自动：

1. 清理旧 UKF 进程；
2. **热重载 FreeRTOS 固件**（避免 SHM 写入停滞）；
3. 复位共享内存；
4. 启动基础节点与额外只读实例；
5. 运行 25 秒；
6. 采样 CPU 占用并提取 RMSE / 帧数；
7. 输出到 `/tmp/combo_2_0_0_25.log`。

### 5.2 查看测试结果

```bash
cat /tmp/combo_2_0_0_25.log
```

关键观察点：

- `=== CPU 占用 ===`：三核占用
- `=== 进程 CPU 占用 ===`：各 UKF 实例占用
- `=== 各实例指标 ===`：帧数、平均 RMSE、最终 RMSE、延迟

### 5.3 批量运行多种场景

运行完整组合套件（约 8 组，每组 25 秒 + 热重载时间）：

```bash
sudo ./run_combo_suite_safe.sh 25
```

批量结果汇总：

```bash
cat /tmp/combo_summary_safe.txt
```

示例输出：

```text
E5 E9 E39 DURATION CPU0 CPU1 CPU2
0 0 0 25 97.0 96.5 99.9
2 0 0 25 98.2 97.2 99.9
5 0 0 25 99.7 97.4 99.9
0 3 0 25 99.1 97.3 99.9
3 1 0 25 99.3 97.6 100.0
1 1 0 25 98.2 97.1 99.9
0 0 1 25 98.0 100.0 100.0
1 0 1 25 98.3 100.0 100.0
```

### 5.4 推荐演示场景

| 场景 | 命令 | 说明 |
|------|------|------|
| 轻量配网 | `sudo ./multi_node_combo_test.sh 2 0 0 25` | 3×5bus + 1×9bus + 1×39bus，负载最平稳 |
| 馈线 + 变电站混合 | `sudo ./multi_node_combo_test.sh 1 1 0 25` | 2×5bus + 2×9bus + 1×39bus，均衡 |
| 大量小馈线 | `sudo ./multi_node_combo_test.sh 5 0 0 25` | 6×5bus + 1×9bus + 1×39bus，CPU0 接近满载 |
| 双主干网（反例） | `sudo ./multi_node_combo_test.sh 0 0 1 25` | 1×5bus + 1×9bus + 2×39bus，展示丢帧风险 |

---

## 6. 结果解读与演示解说词

### 6.1 CPU 占用怎么看

| CPU | 主要任务 | 健康范围 | 说明 |
|-----|----------|----------|------|
| CPU0 | 5bus / 9bus UKF、Linux 用户态任务 | < 100% | 接近满载说明小节点已很多，但只要有少量余量就不易卡死 |
| CPU1 | FreeRTOS 数据生成 | < 100% | 负责仿真积分与 SHM 写入 |
| CPU2 | 39bus UKF、Linux 后台任务 | < 100% | 39bus 单实例约占 54%，其余为系统开销 |

> 演示时重点强调：**39bus 从 2kHz 降到 250Hz 后，UKF 进程本身不再独占 CPU2，避免了单一进程把核心吃到 100% 而卡死的风险。**

### 6.2 RMSE 精度怎么看

- **5bus**：平均 RMSE 约 0.526，最终 RMSE 约 0.527。
- **9bus**：平均 RMSE 约 0.648，最终 RMSE 约 0.652。
- **39bus**：平均 RMSE 约 0.863，最终 RMSE 约 0.869。

解说要点：

> “这些 RMSE 数值与 2kHz 基准处于同一数量级，说明降频到 500Hz/250Hz 后状态估计精度没有退化，满足工程应用要求。”

### 6.3 帧数与延迟怎么看

| 节点 | 频率 | 25 秒期望帧数 | 实际帧数 | 单帧延迟 |
|------|------|--------------|----------|----------|
| 5bus | 500Hz | 12,500 | 13,000–14,000 | ~130–160μs |
| 9bus | 500Hz | 12,500 | 10,000–10,500 | ~400–520μs |
| 39bus | 250Hz | 6,250 | ~5,000 | ~3,000–3,300μs |

- 实际帧数略低于期望值是因为测试包含启动预热和进程调度开销。
- 39bus 单实例延迟约 3ms，小于 4ms 周期，实时性满足。
- **双 39bus 时延迟升至约 5.5ms，超过 4ms 周期，因此丢帧，不推荐。**

### 6.4 稳定与不稳定场景对比

| 场景 | CPU0 | CPU2 | 39bus 延迟 | 结论 |
|------|------|------|------------|------|
| 3×5bus + 1×9bus + 1×39bus | ~98% | ~99% | ~3.0ms | 推荐，稳定 |
| 2×5bus + 2×9bus + 1×39bus | ~98% | ~99% | ~3.1ms | 推荐，稳定 |
| 1×5bus + 1×9bus + 2×39bus | ~98% | 100% | ~5.5ms | 不推荐，丢帧 |

---

## 7. 热重载 FreeRTOS 固件演示

### 7.1 何时需要热重载

- 修改了 `sim_params_5bus.h`、`sim_params_9bus.h` 或 `sim_params_39bus.h` 中的频率参数；
- 连续运行多组测试后 SHM 写入停滞；
- 希望在不重启 Linux 的情况下恢复 FreeRTOS 初始状态。

### 7.2 热重载命令

```bash
cd /home/user/Phytium/task2_linux
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

预期输出：

```text
============================================
  FreeRTOS 固件热重载
============================================

  新固件: /lib/firmware/openamp_core0.elf
  目标:   /sys/class/remoteproc/remoteproc0

[1/5] 清理持有 RPMsg 的进程...
[2/5] 停止 remoteproc (CPU1)...
[3/5] 替换固件文件...
[4/5] 等待资源释放...
[5/5] 启动 remoteproc (CPU1)...
      状态: running

============================================
  SUCCESS: FreeRTOS 已重新启动
============================================
```

### 7.3 验证热重载成功

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

输出 `running` 表示成功。

---

## 8. 完整演示流程示例（推荐视频脚本）

### 8.1 开场（约 30 秒）

1. 展示开发板、电源、串口、网线连接。
2. 上电，串口显示启动日志。
3. SSH 登录开发板，展示工作目录。

### 8.2 环境检查（约 1 分钟）

```bash
cat /sys/class/remoteproc/remoteproc0/state
cd /home/user/Phytium/task2_linux && ls
```

解说：确认 FreeRTOS 已运行，UKF 程序已就绪。

### 8.3 基础三节点演示（约 2 分钟）

1. 启动 `start_sim_nodes`。
2. 启动 5bus / 9bus / 39bus UKF。
3. 用 `ps`、`tail -f`、CPU 占用命令展示实时运行。
4. 解说：三种节点对应不同规模电网，39bus 是大系统主干网。

### 8.4 多节点扩展演示（约 4 分钟）

1. 停止基础节点。
2. 运行推荐场景：

```bash
sudo ./multi_node_combo_test.sh 2 0 0 25
```

3. 展示 `/tmp/combo_2_0_0_25.log`：CPU 占用、帧数、RMSE。
4. 解说：通过降频和只读实例，一个核心可支持多个小节点。

### 8.5 批量组合测试演示（约 2 分钟）

```bash
sudo ./run_combo_suite_safe.sh 25
cat /tmp/combo_summary_safe.txt
```

解说：批量验证不同真实场景的稳定性和 CPU 占用。

### 8.6 双 39bus 反例演示（约 1 分钟）

```bash
sudo ./multi_node_combo_test.sh 0 0 1 25
grep "39bus" /tmp/combo_0_0_1_25.log
```

解说：双 39bus 延迟超过周期，出现丢帧，说明不能在同一核心上无限叠加大系统。

### 8.7 收尾（约 30 秒）

1. 停止所有进程。
2. 总结：FT 库加速大系统、降频扩展多节点、找到匹配的真实场景。

---

## 9. 常见问题与处理

### 9.1 SSH 登录失败

- 确认网线连接和 IP 地址。
- 确认 PC 与开发板在同一网段。

### 9.2 `start_sim_nodes` 无法启动或 RPMsg 报错

- 检查 remoteproc 状态是否为 `running`。
- 执行热重载：

```bash
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

### 9.3 5bus 或 9bus 帧数为 0

- 可能是 SHM 计数器未清空或 FreeRTOS 写入停滞。
- 执行：

```bash
sudo pkill -9 -f ukf_pipeline
sudo pkill -9 -x start_sim_nodes
sudo ./reset_shm
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

然后重新启动。

### 9.4 CPU 占用长时间 100%

- 正常现象：CPU0/CPU1/CPU2 在演示中可能接近满载。
- 若系统明显卡死（SSH 无响应、日志停滞），执行热重载或重启开发板。
- 为避免风险，演示时优先选择 L1 / L5 等负载较平稳的场景。

### 9.5 如何切换回 2kHz 原始频率

修改 FreeRTOS 头文件：

```text
sim_params_5bus.h   -> SIM_5BUS_WRITE_DOWN = 1
sim_params_9bus.h   -> SIM_9BUS_WRITE_DOWN = 1
sim_params_39bus.h  -> SIM_39BUS_WRITE_DOWN = 2
```

重新编译固件、复制到 `/lib/firmware/openamp_core0.elf` 并热重载。Linux 侧启动时设置：

```bash
UKF_DELTT=0.0005  # 5bus/9bus/39bus 均为 2kHz
```

---

## 10. 附录：关键文件与命令速查

### 10.1 关键文件

| 文件 | 路径 |
|------|------|
| FreeRTOS 39bus 参数 | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/sim_params_39bus.h` |
| FreeRTOS 5bus 参数 | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/sim_params_5bus.h` |
| FreeRTOS 9bus 参数 | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/sim_params_9bus.h` |
| UKF 主程序 | `/home/user/Phytium/task2_linux/ukf_pipeline_online.c` |
| FT 库路径 | `/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0`、`/home/alientek/Phytium/fc_lib/LAPACK-FT_v1.4.0` |

### 10.2 常用命令

```bash
# 查看 remoteproc 状态
cat /sys/class/remoteproc/remoteproc0/state

# 查看 FreeRTOS 数据桥接输出（前台运行）
sudo ./start_sim_nodes 1

# 查看 UKF 实时日志
tail -f /tmp/ukf_5bus.log
tail -f /tmp/ukf_9bus.log
tail -f /tmp/ukf_39bus.log

# 查看 CPU 占用
top -1

# 查看测试结果
cat /tmp/combo_2_0_0_25.log
cat /tmp/combo_summary_safe.txt

# 热重载固件
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf

# 单组测试
sudo ./multi_node_combo_test.sh 2 0 0 25

# 批量测试
sudo ./run_combo_suite_safe.sh 25
```

---

## 11. 结束语

本指南覆盖了从开发板上电、环境检查、基础三节点演示、多节点组合测试到结果解读的完整流程。录制视频时，建议按照第 8 节的“完整演示流程示例”进行，重点展示：

1. FT 库对 39bus 大系统的加速效果；
2. 降频后 39bus 单实例 CPU 占用明显下降；
3. 多节点真实场景（配网、变电站群、混合）下的稳定运行；
4. 双 39bus 过载反例，说明资源边界。

如需调整频率或布局，请参考 [FT_UKF_OPTIMIZATION_REPORT.md](file:///home/alientek/Phytium/task2_linux/FT_UKF_OPTIMIZATION_REPORT.md) 中的技术细节。
