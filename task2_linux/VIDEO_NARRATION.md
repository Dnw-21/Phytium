# 演示视频完整旁白

> 适用：飞腾 PE2204 + Linux 6.6 + FreeRTOS AMP  
> 场景：FT 库优化 + 降频扩展 + 多节点并发状态估计  
> 建议：三窗口同时录制，旁白按终端 1/2/3 切换讲解

---

## 开场白

"各位评委老师好，接下来演示的是利用飞腾优化库对UKF状态估计的试验，以及在多种真实工程场景下的多节点组合稳定性。"

---

## 第 1 步：登录开发板

**画面**：Windows 终端，输入 SSH 命令。

**命令**：

```bash
ssh user@192.168.88.10
```

**预期结果**：

```text
Linux phytiumpi 6.6.63-phytium-embedded-v3.2 ...
Last login: ...
user@phytiumpi:~$
```

**旁白**：

"首先，开发板已经上电并通过网线连接电脑，IP 地址是 192.168.88.10。我们通过 SSH 登录到飞腾派。登录成功后，系统运行的是 Linux 6.6.63-phytium-embedded 内核。"

---

## 第 2 步：进入项目目录

**画面**：终端中执行 cd 和 ls。

**命令**：

```bash
cd /home/user/Phytium/task2_linux
ls
```

**预期结果**（截取）：

```text
ukf_pipeline_5bus     ukf_pipeline_5bus_ft
ukf_pipeline_9bus     ukf_pipeline_9bus_ft
ukf_pipeline_39bus    ukf_pipeline_39bus_ft
start_sim_nodes       multi_node_combo_test.sh
run_combo_suite_safe.sh  DEMO_GUIDE.md
```

**旁白**：

"进入项目目录后，我们可以看到几个核心组件：三种规模的 UKF 状态估计程序，分别是 5bus、9bus 和 39bus，每个都有普通版本和 FT 优化版本；`start_sim_nodes` 是 Linux 与 FreeRTOS 之间的 RPMsg 数据桥接程序；`multi_node_combo_test.sh` 和 `run_combo_suite_safe.sh` 是我们用来做多节点并发测试的脚本。"

---

## 第 3 步：检查 FreeRTOS 状态

**画面**：终端中执行 remoteproc 状态检查。

**命令**：

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

**预期结果**：

```text
running
```

**旁白**：

"本系统采用 AMP 架构，CPU1 运行 FreeRTOS ，负责电力系统仿真和数据生成。通过 `/sys/class/remoteproc/remoteproc0/state` 可以看到当前状态为 `running`，说明 FreeRTOS 固件已经在 CPU1 上正常运行。"

---

## 第 4 步：热重载 FreeRTOS 固件

**画面**：终端中执行 reload_firmware.sh。

**命令**：

```bash
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf
```

**预期结果**：

```text
[1/5] 停止 remoteproc (CPU1)...
[2/5] 替换固件...
[3/5] 启动 remoteproc (CPU1)...
[4/5] 等待固件运行...
[5/5] 启动成功，state=running
```

**旁白**：

"在正式演示之前，我们先通过热重载脚本将 FreeRTOS 固件恢复到初始状态。这个脚本可以在不重启 Linux 的情况下，停止 CPU1 上的 remoteproc，替换 `/lib/firmware/openamp_core0.elf` 固件，再重新启动。整个过程只需要几秒钟，比整机重启高效很多，也方便我们在不同测试之间清理状态。"

---

## 第 5 步：启动 FreeRTOS 数据桥接（终端 1）

**画面**：切换到终端 1，执行 start_sim_nodes。

**命令**：

```bash
sudo ./start_sim_nodes 1
```

**预期结果**：

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

**旁白**：

"现在，在终端 1 启动 FreeRTOS 数据桥接程序。我们可以看到三条 RPMsg 通道分别对应 5bus、9bus 和 39bus 成功建立。`SPEED=1 START sent` 表示仿真已经开始，`still running...` 表示 FreeRTOS 正在持续向共享内存写入三种电网的仿真数据。这个终端需要一直运行，负责把数据从 FreeRTOS 送到 Linux 端。"

---

## 第 6 步：启动三个 UKF 状态估计节点（终端 2）

**画面**：切换到终端 2，依次输入三条 nohup 命令。

**命令**：

```bash
cd /home/user/Phytium/task2_linux
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_5bus  > /tmp/ukf_5bus.csv  2> /tmp/ukf_5bus.log  &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_9bus  > /tmp/ukf_9bus.csv  2> /tmp/ukf_9bus.log  &
nohup sudo taskset -c 2 env UKF_DELTT=0.004 ./ukf_pipeline_39bus > /tmp/ukf_39bus.csv 2> /tmp/ukf_39bus.log &
```

**预期结果**：

```text
[1] 1421
[2] 1422
[3] 1423
```

**旁白**：

"在终端 2 启动三个 UKF 状态估计节点。这里把 5bus 和 9bus 绑定到 CPU0，对应500Hz 数据周期，把 39bus 绑定到 CPU2。`UKF_DELTT=0.002` 对应 5bus 和 9bus 的 对应250Hz 数据周期。返回的三个进程号 1421、1422、1423 说明三个 UKF 节点都已经后台启动。"

---

## 第 7 步：验证 UKF 进程

**画面**：终端 2 中执行 ps 验证。

**命令**：

```bash
ps aux | grep ukf_pipeline | grep -v grep
```

**预期结果**：

```text
root      1421  ...  ./ukf_pipeline_5bus
root      1422  ...  ./ukf_pipeline_9bus
root      1423  ...  ./ukf_pipeline_39bus
```

**旁白**：

"我们用 `ps` 命令确认一下，三个 UKF 进程确实都在运行，到这里，基础三节点的状态估计系统已经全部跑起来了。"

---

## 第 8 步：查看实时日志（终端 3）

**画面**：切换到终端 3，执行 tail -f。

**命令**：

```bash
tail -f /tmp/ukf_5bus.log /tmp/ukf_9bus.log /tmp/ukf_39bus.log
```

**预期结果**：

```text
==> /tmp/ukf_5bus.log <==
[ukf-5bus] t=10.0s frames=5000 X=[-1.0161,-0.1333] rmse=0.5250 lat=129us

==> /tmp/ukf_9bus.log <==
[ukf-9bus] t=10.0s frames=5000 X=[111.3083,-2386.6837,...] rmse=0.6494 lat=508us

==> /tmp/ukf_39bus.log <==
[ukf-39bus] t=10.0s frames=2500 X=[-24026.0410,6147.6313,...] rmse=0.8688 lat=3150us
```

**旁白**：

"在终端 3 实时查看三个 UKF 节点的日志。我们可以关注三个关键字段：第一个是 `frames`，代表已经处理的数据帧数，可以看到帧数一直在增加，说明 UKF 正在实时消费数据；第二个是 `rmse`，代表状态估计的均方根误差，三个节点分别稳定在 0.53、0.65 和 0.87 左右，与 2kHz 基准相比没有明显退化；第三个是 `lat`，代表单帧处理延迟，分别是 129 微秒、508 微秒和 3150 微秒，都小于各自的数据周期 2 毫秒和 4 毫秒，因此实时性满足。"

---

## 第 9 步：退出日志查看

**画面**：按 Ctrl+C。

**操作**：`Ctrl+C`

**预期结果**：退出 `tail -f`，回到命令提示符。

**旁白**：

"日志确认没有问题后，我们退出实时日志查看，准备看一下系统整体的 CPU 占用情况。"

---

## 第 10 步：查看 CPU 占用（终端 3）

**画面**：终端 3 执行 top。

**命令**：

```bash
top -1 -n 1 | head -10
```

**预期结果**：

```text
%Cpu0  : 97.7 us,  0.0 sy,  0.0 ni,  2.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu1  : 98.9 us,  0.0 sy,  0.0 ni,  1.1 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu2  : 64.7 us,  0.0 sy,  0.0 ni, 35.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
%Cpu3  :  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
```

**旁白**：

"通过 `top` 查看系统整体 CPU 占用。可以看到之前的firefox 跑到 116.7%，说明它大概占用了 1.16 个物理核的算力，而目前的状态估计它占用了大约 0.7 个核心的算力"

---

## 第 11 步：停止当前进程（终端 2）

**画面**：切换到终端 2，执行停止命令。

**命令**：

```bash
sudo pkill -9 -f ukf_pipeline
sudo pkill -9 -x start_sim_nodes
```

**预期结果**：命令执行后没有报错，终端 1 的 `start_sim_nodes` 进程也退出。

**旁白**：

"基础三节点演示完成。现在停止所有 UKF 进程和 FreeRTOS 数据桥接，为下一部分多节点并发测试做准备。"

---

## 第 12 步：验证进程已停止

**画面**：终端 2 执行 ps 验证。

**命令**：

```bash
ps aux | grep -E "ukf_pipeline|start_sim_nodes" | grep -v grep
```

**预期结果**：没有任何输出。

**旁白**：

"用 `ps` 再次验证，所有 UKF 进程和节点仿真都已经停止。接下来我们开始多节点组合测试。"

---

## 第 13 步：多节点组合测试——轻量配网（终端 1 + 终端 2 + 终端 3）

### 13.1 重新启动数据桥接（终端 1）

**命令**：

```bash
sudo ./start_sim_nodes 1
```

**预期结果**：同第 5 步，显示三条 RPMsg 通道建立成功和 `still running...`。

**旁白**：

"首先重新启动 FreeRTOS 数据桥接。接下来要演示的是多节点并发场景，第一个场景是轻量配网：3 个 5bus 小节点、1 个 9bus 中节点和 1 个 39bus 大节点同时运行。"

### 13.2 启动测试脚本（终端 2）

**命令**：

```bash
cd /home/user/Phytium/task2_linux
sudo ./multi_node_combo_test.sh 2 0 0 25
```

**预期结果**：

```text
=== 多节点组合测试: 基础(1+1+1) + 额外(2×5bus + 0×9bus + 0×39bus), 25s ===
    总节点数: 1×5bus + 1×9bus + 1×39bus
[*] 热重载 FreeRTOS 固件，清理状态...
[*] 运行 25s...

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

**旁白**：

"在终端 2 执行 `multi_node_combo_test.sh 2 0 0 25`，其中 2 表示额外启动 2 个 5bus 只读实例，0 0 表示不额外启动 9bus 和 39bus，25 表示测试 25 秒。脚本会自动热重载固件、清理共享内存、启动基础节点和只读实例，最后汇总结果。"

### 13.3 查看结果（终端 3）

**命令**：

```bash
cat /tmp/combo_2_0_0_25.log
```

**预期结果**：同 13.2。

**旁白**：

"在终端 3 查看测试结果。首先看 CPU 占用：CPU0 总占用 98.2%，CPU1 97.2%，CPU2 99.9%。再看进程级占用：CPU0 上有 1 个 9bus 和 3 个 5bus 实例，每个 5bus 只占约 10%，9bus 占约 16%；CPU2 上 39bus 占约 55%。最后看各实例指标：5bus 处理 13500 帧，RMSE 约 0.525；9bus 处理 10000 帧，RMSE 约 0.647；39bus 处理 5000 帧，RMSE 约 0.862。所有指标都正常，说明这个轻量配网场景非常稳定。"

---

## 第 14 步：多节点组合测试——混合场景

### 14.1 启动测试脚本（终端 2）

**命令**：

```bash
sudo ./multi_node_combo_test.sh 1 1 0 25
```

**预期结果**：

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

**旁白**：

"第二个场景是馈线和变电站均衡混合：2 个 5bus、2 个 9bus 和 1 个 39bus。执行 `multi_node_combo_test.sh 1 1 0 25`，额外启动 1 个 5bus 和 1 个 9bus 只读实例。这个场景下，CPU0 上跑 2 个 5bus 和 2 个 9bus，每个 9bus 约占 16%，整体负载均衡；39bus 仍然独占 CPU2。测试结果显示，5bus 13500 帧、9bus 10000 帧、39bus 5000 帧，RMSE 与之前一致，系统依然稳定。这是我们推荐的典型混合场景。"

### 14.2 查看结果（终端 3）

**命令**：

```bash
cat /tmp/combo_1_1_0_25.log
```

**预期结果**：同 14.1。

**旁白**：

"同样地，我们在终端 3 查看 `combo_1_1_0_25.log`。可以看到所有节点的帧数和 RMSE 都保持正常，验证了在 CPU0 上叠加多个小节点、在 CPU2 上运行单个大节点的布局是可行的。"

---

## 第 15 步：批量组合测试

### 15.1 启动批量测试套件（终端 2）

**命令**：

```bash
sudo ./run_combo_suite_safe.sh 25
```

**预期结果**：终端依次显示 8 组测试的标题和完成标志。

**旁白**：

"为了更全面地评估不同节点布局，我们使用 `run_combo_suite_safe.sh` 批量运行 8 种典型场景。这个脚本会自动遍历轻量、密集、混合和双大系统等布局，每组测试 25 秒。录制时可以看到终端依次输出每组测试的标题、CPU 占用和完成标志。"

### 15.2 查看汇总结果（终端 3）

**命令**：

```bash
cat /tmp/combo_summary_safe.txt
```

**预期结果**：

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

**旁白**：

"批量测试完成后，我们查看汇总表。表头中 `E5`、`E9`、`E39` 分别表示额外启动的 5bus、9bus、39bus 只读实例数，加上基础节点就是总节点数。`CPU0`、`CPU1`、`CPU2` 分别是三核的总占用。可以看到，前 6 组场景虽然 CPU 占用很高，但都没有让三个核心同时达到 100%，系统仍然稳定运行。最后两组是双 39bus 场景，CPU1 和 CPU2 都达到了 100%，此时处理延迟超过 4 毫秒周期，出现丢帧，因此不推荐。这个汇总表帮助我们快速筛选出适合实际部署的节点布局。"

---

## 第 16 步：结尾总结

**画面**：可以停留在汇总表或切回桌面。

**旁白**：

"总结一下本次演示。第一，通过引入飞腾 FT 加速库，39bus 大系统的 UKF 状态估计延迟大幅降低，处理帧率得到提升，且精度保持不变。第二，测试了CPU0 上可稳定支持 6 个小节点并发。第三，我们测试了 8 种真实工程场景，验证了不同布局的稳定性
以上就是本次演示的全部内容，谢谢各位评委老师。"

---

## 附：讲解节奏建议

| 段落 | 建议时长 | 重点 |
|------|----------|------|
| 开场白 | 30 秒 | 点明三部分内容 |
| 第 1-4 步 | 1 分钟 | 登录、检查、热重载 |
| 第 5-7 步 | 1 分钟 | 启动三节点并验证 |
| 第 8-10 步 | 2 分钟 | 日志字段、CPU 占用 |
| 第 11-14 步 | 3 分钟 | 多节点组合测试 |
| 第 15 步 | 1.5 分钟 | 批量汇总表解读 |
| 结尾总结 | 1 分钟 | 结论和后续方向 |
| **总计** | **约 10 分钟** | - |
