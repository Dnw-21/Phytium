<!-- recallloom:file=rolling_summary version=1.0 lang=zh-CN -->
<!-- last-writer: [TraeAI] | 2026-06-02 -->
<!-- file-state: revision=12 | updated-at=2026-06-02T18:00:00+08:00 | writer-id=TraeAI | base-workspace-revision=21 -->

<!-- section: current_state -->
- 项目分为 LoRa 主控移植路线和 UKF 面板路线；两条路线目前分离。
- **LoRa 主控移植路线**：以 GD32L233C_Prj_Master_v3 为基准，已全部移植到 Phytium FreeRTOS 侧，编译通过（ELF 707K）。任务架构已调整为 6 任务（rpm, aux, master_recv, master_process, master_judge, master_poll）。
- 终端节点工程（/home/alientek/Phytium/GD32L233C_Prj）已确认与主控通信协议完全兼容：8字节 sync_code、52字节 NodeSample_t、CMD_POLL_STATUS(0x14)/CMD_REQUEST_FAULT_DATA(0x15) 一致。
- 主控使用两层轮询：Tier1(TIER1_TIMEOUT_MS=3s, CMD_POLL_STATUS) → 正常节点状态，Tier2(CMD_REQUEST_FAULT_DATA) → 故障节点快照。
- 终端节点不上传波形，只上传 3电机+9母线 的状态数据（普通20点/故障40点）。
- 混沌加密已启用：主控和终端统一使用 8字节 sync_code（uint64_t，组合 x/y 状态位）。
- 数据存储：共享内存 g_status_buf 模拟 GD32 Flash，Linux 侧通过 trace_reader 查看。
- RPMsg 已预留 CMD_NODE_STATUS(0x0025) 命令码，用于后续向 Linux 传输处理后的 NodeSample_t 数据。
- FreeRTOS 实际运行在 CPU1，但设备树/remoteproc 仍写 CPU3。
- 当前唯一 Dashboard 是 state_estimation 的 Flask Dashboard，端口 5000。
- Dashboard 当前使用 state_new 模拟数据，故障在 5s 和 15s 出现。
- 已清理旧文件：wave_decode, power_system, math_utils, test_control, sim_engine 等已从 freertos/src 和 SDK 中移除。

<!-- section: active_judgments -->
- LoRa 真实链路、UKF Dashboard 为两条独立路线，不应混淆各自的当前状态。
- 主控采用轮询模式：必须先发送 CMD_POLL_STATUS → 终端节点才响应上传数据，终端不会主动上报。
- 终端节点 NodeSample_t 数据格式（int16×10000）与 state_estimation 模拟数据（float）完全兼容，Linux 侧只需做 int16/10000 → float 转换。
- 项目文档中关于 UART3、CPU3 实际运行、USE_LORA_SIMULATION 当前主链路、8000/8080 Dashboard 的旧说法应视为历史或已废弃。
- trace_reader 是开发板 Linux 侧读取共享内存 trace 输出的可执行程序，不是虚拟机本机程序。
- 图片访问限制只针对 GPT-5.5：其他支持图像理解的模型可以访问和解析图片。
- RecallLoom 文件由 TraeAI 直接维护（读写 .recallloom/ 目录），无需安装 Claude Code CLI 插件。

<!-- section: risks_open_questions -->
- **实物联调未做**：开发板尚未上电部署，LoRa 模块 UART2 通信未验证，终端节点未连接。
- FreeRTOS → Linux 的 RPMsg 数据通道（CMD_NODE_STATUS）尚未实现 Linux 侧接收程序。
- 终端节点工程只有一个节点（GD32L233C_Prj），多节点并发测试需要更多 GD32 板子。
- 面板 UKF 路线尚未接入真实 LoRa 数据，当前仍使用模拟数据。
- 接收+处理任务分离引入了 g_recv_queue（RECV_QUEUE_LENGTH=16），队列满会导致丢帧，需实物测试验证。
- 软超时参数（stable=5ms 连续不变）需在真实 LoRa 环境下验证是否合理。

<!-- section: next_step -->
- **当前优先级 1**：等待用户开发板上电，进行实物联调。
- **当前优先级 2**：实现 Linux 侧 RPMsg 接收程序，将 CMD_NODE_STATUS 数据写入文件供 state_estimation 使用。
- **当前优先级 3**：在 Linux 侧实现数据桥接（int16/10000 → float），将接收数据转换为 state_estimation 可用的格式。
- 联调步骤：1) 开发板上电 + 部署固件；2) trace_reader 查看启动日志；3) 终端节点上电 + LoRa 连接；4) 观察 Poll 任务输出和状态接收。
- 后续工作：Linux 侧 RPMsg 接收 → 数据桥接 → UKF 面板接入真实数据。

<!-- section: recent_pivots -->
- 2026-06-02 完成 GD32L233C_Prj_Master_v3 向 Phytium FreeRTOS 的完整移植，编译通过。
- 2026-06-02 确认终端节点工程（GD32L233C_Prj）与主控通信协议完全兼容。
- 2026-06-02 清理旧文件（wave_decode, power_system, math_utils, test_control, sim_engine）。
- 2026-06-02 确认 data_frame.h 两端对齐：NodeSample_t(52B)、sync_code(uint64_t)、NORMAL_UPLOAD_POINTS=20、FAULT_UPLOAD_POINTS=40。
- 2026-06-02 确认 state_estimation 模拟数据格式与终端节点 NodeSample_t 格式兼容（int16×10000 ↔ float）。
- 2026-05-30 用户确认暂不处理数据生成迁移，等待队友多节点代码。
- 2026-05-28 用户确认 LoRa 串口为 UART2、FreeRTOS 实际 CPU1/设备树 CPU3、当前唯一 Dashboard 为 5000 端口。