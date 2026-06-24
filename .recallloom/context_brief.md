<!-- recallloom:file=context_brief version=1.0 lang=zh-CN -->
<!-- file-state: revision=6 | updated-at=2026-06-02T18:00:00+08:00 | writer-id=TraeAI | base-workspace-revision=21 -->

<!-- section: mission -->
# 项目使命

在飞腾派 CEK8903 开发板上构建 LoRa 主控与电力状态估计系统：一条路线负责把 GD32 模拟主控工程移植到飞腾派 FreeRTOS 侧并接收真实 LoRa 数据，另一条路线负责用 UKF Dashboard 展示状态估计、故障预警、自然灾害预警和通知推送。

<!-- section: audience_stakeholders -->
# 受众与相关方

- 开发者主要在虚拟机上交叉编译、通过 SSH 部署到飞腾派开发板。
- 飞腾派 PE2204 SoC：Linux 主侧负责接收、记录和后续面板接入；FreeRTOS 主控侧实际运行在 CPU1，但设备树和 remoteproc 配置仍写 CPU3。
- GD32L233 终端节点和队友提供的 GD32 模拟主控工程是 LoRa 主控移植路线的参考来源。
- UKF Dashboard 面向演示和算法验证，当前使用模拟数据，后续接入真实 LoRa 数据。

<!-- section: current_phase -->
# 当前阶段

- **LoRa 主控移植已完成**：以 GD32L233C_Prj_Master_v3 为基准，全部业务逻辑已移植到 Phytium FreeRTOS 侧，编译通过（ELF 707K）。
- 任务架构：6 任务（rpm_task, aux_task, master_recv_task, master_process_task, master_judge_task, master_poll_task），recv/process 分离，两层轮询（Tier1+Tier2）。
- 终端节点工程（/home/alientek/Phytium/GD32L233C_Prj）已确认通信协议完全兼容。
- **实物联调尚未进行**：等待开发板上电 + LoRa 模块 + 终端节点。
- UKF Dashboard 当前唯一服务是 state_estimation 的 dashboard_server，端口 5000，使用模拟数据。
- 飞书和微信通知、故障预警、自然灾害预警属于 Dashboard 路线。
- 待实现：Linux 侧 RPMsg 接收 + 数据桥接（int16→float）→ UKF 面板接入真实数据。

<!-- section: scope -->
# 范围

1. FreeRTOS 主控侧：LoRa UART2 接收、GD32 主控逻辑移植、数据解析处理、混沌解密、分时接收、两层轮询、共享内存和 RPMsg 输出。
2. Linux 主侧：RPMsg 或共享内存数据接收、记录、数据桥接（int16/10000→float），并作为后续 Dashboard 真实数据入口。
3. UKF Dashboard：基于模拟数据或后续真实数据做状态估计展示、故障预警、自然灾害预警、飞书和微信推送。
4. 状态估计模型：维护 state_new 的电力系统动态模型、模拟数据和算法基准。

不包含：终端节点硬件设计、LoRa 物理层协议本身、把旧 8000 或 8080 板端 Dashboard 作为当前路线继续维护。

<!-- section: source_of_truth -->
# 事实来源

- 当前项目事实优先看 README、PROJECT_INFO 和 docs 中 2026-05-28 后统一过的文档。
- 本次移植的基准工程：/home/alientek/Phytium/GD32L233C_Prj_Master_v3
- 终端节点工程：/home/alientek/Phytium/GD32L233C_Prj
- LoRa 串口当前口径是 UART2，历史 UART3 记录仅可作为调试背景。
- FreeRTOS 核心口径是实际 CPU1 与设备树 CPU3 同时成立。
- Dashboard 当前事实以 state_estimation 的 dashboard_server 和 state_new 模拟数据为准。
- RecallLoom 由 TraeAI 直接维护（读写 .recallloom/ 目录）。

<!-- section: core_workflow -->
# 核心工作流

1. LoRa 主控移植：已完成。以 GD32L233C_Prj_Master_v3 为基准，移植到 Phytium FreeRTOS。
2. 编译部署：`cd /home/alientek/Phytium/freertos && bash deploy.sh`（一键编译+传输+安全启动）。
3. 真实链路验证：GD32 终端节点通过 LoRa 发送，ATK-MWCC68D 模块经 UART2 到 FreeRTOS 主控侧，再输出给 Linux。
4. Dashboard 运行：进入 state_estimation，运行 dashboard_server，浏览器访问 5000 端口。
5. 数据打通目标：Linux 侧 RPMsg 接收 CMD_NODE_STATUS → 数据桥接（int16/10000→float）→ UKF 输入。
6. 每次恢复工作时先用 RecallLoom 快速恢复项目口径。

<!-- section: boundaries -->
# 边界与约束

- 后续协作必须小步读取，避免默认读取大型测量数据、PDF 和 trace 文件。
- 图片访问限制只针对 GPT-5.5：不要访问、读取或解析图片文件；其他支持图像理解的模型可以访问和解析图片。
- trace_reader 是开发板 Linux 侧读取共享内存 trace 输出的程序，不是虚拟机本机程序。
- 当前只保留 state_estimation Dashboard；旧 8000 或 8080 面板相关内容应删除或标记历史。
- 修改 RecallLoom 托管文件必须使用 helper，不能手写 sidecar。