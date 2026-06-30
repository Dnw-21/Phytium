# UKF 多节点演示极简操作指南

> 适用：飞腾 PE2204 + Linux 6.6 + FreeRTOS AMP  
> 目标：录制演示视频，快速上手  

---

## 终端规划

建议开 **3 个 SSH 窗口**同时登录开发板：

| 终端 | 用途 | 是否常驻 |
|------|------|----------|
| 终端 1 | 运行 FreeRTOS 数据桥接 | 是 |
| 终端 2 | 启动/停止 UKF、执行测试脚本 | 否 |
| 终端 3 | 看日志、看 CPU | 否 |

---

## 1. 登录开发板

Windows 命令行（PowerShell / CMD / Windows Terminal）：

```bash
ssh user@192.168.88.10
```

预期结果（输入密码后）：

```text
Linux phytiumpi 6.6.63-phytium-embedded-v3.2 ...
Last login: ...
user@phytiumpi:~$
```

> 说什么：已经通过 SSH 登录到飞腾 PE2204 开发板，接下来演示 UKF 多节点状态估计。

---

## 2. 进入工作目录

**终端 1/2/3 均可**

```bash
cd /home/user/Phytium/task2_linux
ls
```

预期结果（截取关键文件）：

```text
ukf_pipeline_5bus     ukf_pipeline_5bus_ft
ukf_pipeline_9bus     ukf_pipeline_9bus_ft
ukf_pipeline_39bus    ukf_pipeline_39bus_ft
start_sim_nodes       multi_node_combo_test.sh
run_combo_suite_safe.sh  DEMO_GUIDE.md
```

> 说什么：这是项目目录，包含三个 UKF 状态估计程序、FT 优化版本、FreeRTOS 桥接和测试脚本。

---

## 3. 检查 FreeRTOS 状态

**终端 1**

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

预期结果：

```text
running
```

> 说什么：FreeRTOS 固件已经在 CPU1 上运行，负责三路桥接的数据生成。如果显示 offline，后面会用热重载脚本重新加载。

---

## 4. 启动 FreeRTOS 数据桥接

**终端 1（常驻）**

```bash
sudo ./start_sim_nodes 1
```

预期结果（截取）：

```text
[master] Starting 3 sim nodes sequentially (speed=1)...
[5bus] Creating endpoint (ch=rpmsg-sim-5bus, dst=0)...
[5bus] Opened /dev/rpmsg9
[39bus] Creating endpoint (ch=rpmsg-sim-39bus, dst=10)...
[39bus] Opened /dev/rpmsg10
[9bus] Creating endpoint (ch=rpmsg-sim-9bus, dst=20)...
[9bus] Opened /dev/rpmsg11
[5bus] SPEED=1 START sent
[39bus] SPEED=1 START sent
[9bus] SPEED=1 START sent
[5bus] still running...
[39bus] still running...
[9bus] still running...
```

> 说什么：三个 RPMsg 通道都已建立，FreeRTOS 仿真任务开始持续向 Linux 写入 5bus、9bus、39bus 数据。这个终端需要一直挂着。

---

## 5. 基础三节点演示

### 5.1 启动三个 UKF

**终端 2**

```bash
cd /home/user/Phytium/task2_linux
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_5bus  > /tmp/ukf_5bus.csv  2> /tmp/ukf_5bus.log  &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_9bus  > /tmp/ukf_9bus.csv  2> /tmp/ukf_9bus.log  &
nohup sudo taskset -c 2 env UKF_DELTT=0.004 ./ukf_pipeline_39bus > /tmp/ukf_39bus.csv 2> /tmp/ukf_39bus.log &
```

预期结果：

```text
[1] 1421
[2] 1422
[3] 1423
```

这是 bash 返回的后台任务编号和进程 PID，说明三个 UKF 都已经后台启动。

可以再验证一下：

```bash
ps aux | grep ukf_pipeline | grep -v grep
```

预期结果：

```text
root      1421  ...  ./ukf_pipeline_5bus
root      1422  ...  ./ukf_pipeline_9bus
root      1423  ...  ./ukf_pipeline_39bus
```

> 说什么：三个 UKF 状态估计节点已经在后台启动。5bus/9bus 绑定在 CPU0，39bus 绑定在 CPU2；39bus 因为数据量大，已经降频到 250Hz。看到三个进程号就说明启动成功。

---

### 5.2 查看实时日志

**终端 3**

```bash
tail -f /tmp/ukf_5bus.log /tmp/ukf_9bus.log /tmp/ukf_39bus.log
```

预期结果（截取）：

```text
==> /tmp/ukf_5bus.log <==
[ukf-5bus] t=10.0s frames=5000 X=[-1.0161,-0.1333] rmse=0.5250 lat=129us

==> /tmp/ukf_9bus.log <==
[ukf-9bus] t=10.0s frames=5000 X=[111.3083,-2386.6837,...] rmse=0.6494 lat=508us

==> /tmp/ukf_39bus.log <==
[ukf-39bus] t=10.0s frames=2500 X=[-24026.0410,6147.6313,...] rmse=0.8688 lat=3150us
```

按 `Ctrl+C` 退出。

> 说什么：看日志里的三个关键字段。`frames` 在涨，说明 UKF 正在实时消费数据；`rmse` 稳定在 0.5~0.9 之间，说明状态估计精度没有退化；`lat` 分别是 129us、508us、3150us，都小于各自的数据周期（2ms 或 4ms），所以实时性满足。

---

### 5.3 查看 CPU

**终端 3**

```bash
top -1 -n 1 | head -10
```

预期结果（截取）：

```text
%Cpu0  : 97.7 us,  0.0 sy,  0.0 ni,  2.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu1  : 98.9 us,  0.0 sy,  0.0 ni,  1.1 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu2  : 64.7 us,  0.0 sy,  0.0 ni, 35.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu3  :  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
```

> 说什么：CPU0 和 CPU1 接近满载，这很正常；关键是 CPU2 上 39bus 单进程只占约 65%，剩下 35% 是空闲余量。这说明把 39bus 从 2kHz 降到 250Hz 后，系统不再由单一 UKF 进程把核心吃满，演示时不会出现卡死。CPU3 显示 100% idle，是因为底层固件没有释放它给 Linux。

---

### 5.4 停止

**终端 2**

```bash
sudo pkill -9 -f ukf_pipeline
sudo pkill -9 -x start_sim_nodes
```

预期结果：命令执行后没有报错，三个 UKF 进程和 `start_sim_nodes` 都会退出。

可以再验证：

```bash
ps aux | grep -E "ukf_pipeline|start_sim_nodes" | grep -v grep
```

预期结果：没有输出，说明进程已全部停止。

> 说什么：演示完基础三节点后，先停掉所有 UKF 进程和 FreeRTOS 桥接，为下一步多节点测试做准备。

---

## 6. 多节点组合测试（推荐场景）

### 6.1 轻量配网：3×5bus + 1×9bus + 1×39bus

**终端 1**：如果 `start_sim_nodes` 已停，重新执行：

```bash
sudo ./start_sim_nodes 1
```

**终端 2**：

```bash
cd /home/user/Phytium/task2_linux
sudo ./multi_node_combo_test.sh 2 0 0 25
```

**终端 3**：查看结果：

```bash
cat /tmp/combo_2_0_0_25.log
```

预期结果（截取关键部分）：

```text
=== 多节点组合测试: 基础(1+1+1) + 额外(2×5bus + 0×9bus + 0×39bus), 25s ===

=== CPU 占用 ===
CPU0 total: 98.2%
CPU1 total: 97.2%
CPU2 total: 99.9%

=== 进程 CPU 占用 ===
   4890   0 ukf_pipeline_9b 16.3 ./ukf_pipeline_9bus
   4885   0 ukf_pipeline_5b  9.9 ./ukf_pipeline_5bus
   4892   0 ukf_pipeline_5b  9.8 ./ukf_pipeline_5bus
   4893   0 ukf_pipeline_5b  9.8 ./ukf_pipeline_5bus
   4889   2 ukf_pipeline_39 54.7 ./ukf_pipeline_39bus

=== 各实例指标 ===
--- 5bus base ---
frames=13500 avg_rmse=0.525057 final_rmse=0.52499117
--- 9bus base ---
frames=10000 avg_rmse=0.647146 final_rmse=0.64942973
--- 39bus base ---
frames=5000 avg_rmse=0.862479 final_rmse=0.86882117

=== 测试完成: 2_0_0_25 ===
```

> 说什么：这是推荐场景 3×5bus + 1×9bus + 1×39bus。可以看到 CPU0 上有 3 个 5bus 实例和 1 个 9bus 实例共享运行，每个 5bus 只占约 10%，39bus 在 CPU2 上占约 55%。三个节点帧数和 RMSE 都正常，系统稳定。

---

### 6.2 混合场景：2×5bus + 2×9bus + 1×39bus

**终端 2**：

```bash
sudo ./multi_node_combo_test.sh 1 1 0 25
```

**终端 3**：

```bash
cat /tmp/combo_1_1_0_25.log
```

预期结果（截取）：

```text
=== CPU 占用 ===
CPU0 total: 98.2%
CPU1 total: 97.1%
CPU2 total: 99.9%

=== 进程 CPU 占用 ===
   5120   0 ukf_pipeline_9b 16.2 ./ukf_pipeline_9bus
   5123   0 ukf_pipeline_9b 16.1 ./ukf_pipeline_9bus
   5117   0 ukf_pipeline_5b 10.0 ./ukf_pipeline_5bus
   5126   0 ukf_pipeline_5b  9.9 ./ukf_pipeline_5bus
   5114   2 ukf_pipeline_39 54.5 ./ukf_pipeline_39bus

=== 各实例指标 ===
--- 5bus base ---
frames=13500 avg_rmse=0.525102 final_rmse=0.52499117
--- 9bus base ---
frames=10000 avg_rmse=0.647892 final_rmse=0.64942973
--- 39bus base ---
frames=5000 avg_rmse=0.861034 final_rmse=0.86882117
```

> 说什么：这是推荐场景 2×5bus + 2×9bus + 1×39bus，馈线和变电站数量均衡，负载最平稳。每个 9bus 约占 16%，两个 5bus 各占约 10%，39bus 仍占 CPU2 约 55%，整体稳定。

---

### 6.3 反例：双 39bus（会丢帧）

**终端 2**：

```bash
sudo ./multi_node_combo_test.sh 0 0 1 25
```

**终端 3**：

```bash
cat /tmp/combo_0_0_1_25.log
```

预期结果（截取）：

```text
=== CPU 占用 ===
CPU0 total: 98.0%
CPU1 total: 100.0%
CPU2 total: 100.0%

=== 进程 CPU 占用 ===
   5234   0 ukf_pipeline_9b 16.2 ./ukf_pipeline_9bus
   5231   0 ukf_pipeline_5b 10.1 ./ukf_pipeline_5bus
   5237   2 ukf_pipeline_39 47.2 ./ukf_pipeline_39bus
   5240   2 ukf_pipeline_39 46.8 ./ukf_pipeline_39bus

=== 各实例指标 ===
--- 39bus base ---
frames=4500 avg_rmse=0.871034 final_rmse=0.87882117
[ukf-39bus] t=0.0s frames=4500 X=[...] rmse=0.8788 lat=5520us
```

> 说什么：双 39bus 共享 CPU2，每个 39bus 各占约 47%，但处理延迟达到了 5520us，超过 4ms 数据周期，所以帧数从 5000 降到 4500，出现丢帧。这说明大系统节点不能在同一核心上无限叠加。

---

## 7. 批量组合测试

**终端 2**：

```bash
sudo ./run_combo_suite_safe.sh 25
```

**终端 3**：

```bash
cat /tmp/combo_summary_safe.txt
```

预期结果：

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

> 说什么：这是批量跑了 8 种真实场景的汇总表。`E5/E9/E39` 是额外实例数，加上基础节点就是总节点数。CPU0/CPU1/CPU2 分别是三核占用。可以看到前 6 组 CPU 都没有全部达到 100%，是推荐场景；最后两组双 39bus 导致 CPU1/CPU2 满载，所以不推荐。

---

## 8. 热重载 FreeRTOS 固件

如果 FreeRTOS 异常或修改了频率参数，执行：

**终端 2**（先停掉 `start_sim_nodes` 和 UKF）：

```bash
sudo pkill -9 -f ukf_pipeline
sudo pkill -9 -x start_sim_nodes
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

然后 **终端 1** 重新启动：

```bash
sudo ./start_sim_nodes 1
```

预期结果：

```text
[5/5] 启动 remoteproc (CPU1)...
[master] Starting 3 sim nodes sequentially (speed=1)...
[5bus] Creating endpoint ...
...
```

> 说什么：不需要重启 Linux，直接热重载 FreeRTOS 固件即可恢复初始状态。整个热重载过程大概几秒钟，比整机重启快很多。

---

## 9. 讲解要点速记

| 阶段 | 大概说什么 |
|------|------------|
| 上电登录 | 开发板上电，SSH 登录。 |
| 检查 remoteproc | FreeRTOS 在 CPU1 上跑，负责仿真数据生成。 |
| start_sim_nodes | 建立 Linux 与 FreeRTOS 的 RPMsg 通道。 |
| 启动三节点 | 三个 UKF 分别处理 5bus/9bus/39bus，39bus 已降频到 250Hz。 |
| 看日志 | frames 在涨、rmse 稳定、lat 小于周期。 |
| 看 CPU | 39bus 单进程只占 CPU2 约 54%，不是单一进程把核心吃满。 |
| 多节点测试 | 通过只读实例模拟多个节点并发。 |
| 推荐场景 | 3×5bus+1×9bus+1×39bus 和 2×5bus+2×9bus+1×39bus 稳定。 |
| 反例 | 双 39bus 延迟超标，丢帧。 |
| CPU3 | 这块板子 CPU3 被底层固件保留给 Linux，所以扩展只能在 CPU0+CPU2 上做。 |
| 总结 | FT 库加速大系统，降频扩展多节点，找到匹配的真实场景。 |
