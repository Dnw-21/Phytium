================================================================
电力系统动态状态估计 - UKF算法 - 文件说明
================================================================

项目目录结构（2026-05-27 更新）：
----------

  /home/alientek/Phytium/
  ├── state_new/              ← 核心算法与数据（模型+仿真+UKF估计）
  │   ├── case9_sauer.py        9节点系统参数
  │   ├── ybus_new.py           导纳矩阵计算
  │   ├── initialize_system.py  系统初始化+潮流计算
  │   ├── dynamic_system.py     转子运动方程
  │   ├── generate_true_values.py  RK4积分生成真值+测量数据
  │   ├── RK4.py                UKF专用向量化RK4（sigma点并行积分）
  │   ├── ukf_estimation.py     UKF估计主算法
  │   ├── terminal_node.py      终端端主程序（数据生成）
  │   ├── terminal_controller.py 主控端主程序（UKF+绘图）
  │   ├── plot_results.py       matplotlib结果绘图
  │   ├── run_all.py            一键运行
  │   ├── system_params.mat     系统参数数据
  │   ├── true_states.csv       真实状态数据
  │   ├── node1_measurements.txt 节点1测量数据
  │   ├── node2_measurements.txt 节点2测量数据
  │   └── node3_measurements.txt 节点3测量数据
  │
  └── state_estimation/        ← Dashboard面板（Web展示层）
      ├── dashboard_server.py    Flask主服务（UKF引擎+API路由+灾害仿真）
      ├── templates/
      │   └── dashboard.html     前端面板（Chart.js图表+控制+天气/风险）
      ├── weather_service.py     心知天气API（酒泉实时天气+3天预报）
      ├── risk_assessment.py     自然灾害风险评估引擎（6类灾害加权评分）
      ├── decision_support.py    辅助决策建议（结合天气风险+UKF状态）
      ├── feishu_notifier.py     飞书机器人推送（故障告警+天气预警）
      ├── wechat_notifier.py     Server酱微信推送（每日限5次）
      ├── weather_cache.json     天气数据缓存
      ├── ukf_cache.npz          UKF预计算结果缓存
      └── README_使用说明.txt    本文件


================================================================
一、系统架构
================================================================

  terminal_node.py  →  数据文件  →  dashboard_server.py  →  浏览器前端
  (终端端/数据生成)    (中间文件)    (主控端/UKF+Web服务)    (Chart.js显示)

  dashboard_server.py 通过 sys.path.insert 调用 state_new/ukf_estimation.py
  dashboard_server.py 通过 os.chdir 切换到 state_new/ 读取数据文件


================================================================
二、运行方式
================================================================

  【方案一：Dashboard面板（推荐）】

  1. 生成数据（首次或需要更新数据时）：
     cd /home/alientek/Phytium/state_new
     python terminal_node.py

  2. 启动Dashboard：
     cd /home/alientek/Phytium/state_estimation
     python dashboard_server.py

  3. 浏览器访问：
     http://localhost:5000

  4. 开发板接显示屏方案：
     方案A：开发板运行浏览器，访问 http://localhost:5000
     方案B：其他电脑通过局域网访问 http://开发板IP:5000


  【方案二：命令行一键运行（无面板）】

  cd /home/alientek/Phytium/state_new
  python run_all.py

  自动执行 terminal_node.py → terminal_controller.py，生成matplotlib图表


  【方案三：分步运行（比赛场景）】

  1. 赛前准备：
     cd /home/alientek/Phytium/state_new
     python terminal_node.py    # 生成数据文件

  2. 比赛运行：
     a. 终端端运行：python terminal_node.py  # 生成节点数据
     b. 通过LoRa传输3个txt文件到主控端
     c. 主控端运行：python terminal_controller.py  # 执行UKF估计


  Python依赖安装：
  pip install numpy scipy matplotlib flask requests -i https://pypi.tuna.tsinghua.edu.cn/simple


================================================================
三、Dashboard面板显示内容
================================================================

1. 侧边栏
--------
  - 系统品牌标识：UKF动态状态估计
  - 系统参数面板：
    · 发电机数量：3
    · 节点数量：9
    · 采样频率：1000 Hz
    · 仿真时长：180s（3分钟）
    · 故障1：5.0s - 5.3s
    · 故障2：15.0s - 15.3s
  - 节点数据传输量：实时统计3个节点的数据字节数
  - 运行控制按钮：开始 / 暂停 / 停止

2. 主图区域 — 转子角度 δ 和转速 ω 实时曲线
------------------------------------------
  - 3台发电机的转子角度 δ₁, δ₂, δ₃（rad）
  - 3台发电机的转速 ω₁, ω₂, ω₃（rad/s）
  - 实线 = 真实值（来自true_states.csv）
  - 虚线 = UKF估计值（来自ukf_estimation计算）
  - 固定5秒滑动窗口，随仿真时间滚动
  - 故障区域用红色阴影标记

3. 整体趋势图
------------
  - 显示全部数据范围（0到当前仿真时间）
  - 仅显示真实值，用于观察全局趋势

4. 故障回放面板
-------------
  - 自动捕获故障前后±1s的数据快照
  - 显示故障期间的 δ 和 ω 详细变化
  - 横坐标根据回放数据长度自动调整
  - 不被后续数据覆盖

5. 状态指示
---------
  - 正常运行 → 绿色指示灯
  - 故障检测 → 红色指示灯 + 闪电图标
  - 故障切除 → 绿色指示灯 + 勾号

6. 天气与风险评估
---------------
  - 当前天气：温度、天气状况、湿度、风力风向、能见度
  - 3天天气预报
  - 综合风险等级：低风险/中风险/高风险/紧急
  - 6类灾害独立评分：
    · 沙尘暴 — 光伏板积尘、线路闪络
    · 雷暴 — 直击户外设备、感应过电压
    · 暴雨/冰雹 — 设备进水、光伏板损坏
    · 极端温差 — 设备热胀冷缩疲劳
    · 冻雨/覆冰 — 线路断线、铁塔倒塌
    · 高温 — 光伏板效率下降、设备过热
  - 风险评估算法：加权多灾害综合风险指数（参考IPCC AR6/FEMA HAZUS）

7. 自然灾害仿真模拟
------------------
  - 6种预设灾害场景一键模拟：
    · 沙尘暴 / 雷暴 / 暴雨冰雹 / 极端温差 / 冻雨覆冰 / 极端高温
  - 模拟后自动评估风险等级
  - 自动推送飞书+微信告警
  - 可清除模拟恢复真实天气数据

8. 辅助决策建议
-------------
  - 根据天气风险等级生成操作建议
  - 结合UKF状态估计结果（故障检测、功率摆荡）
  - 按优先级分类：normal / attention / urgent / immediate

9. 告警推送
---------
  - 飞书机器人：故障告警卡片 + 天气风险预警卡片
    · 故障告警：故障前后状态对比、严重程度
    · 天气预警：当前天气+风险摘要+灾害详情+应对建议
    · 限流：天气5分钟/次，故障1分钟/次
  - Server酱微信推送：故障告警 + 天气预警
    · 每日限制5次，与飞书同步推送


================================================================
四、API接口说明
================================================================

  控制接口：
    POST /api/start       启动仿真
    POST /api/pause       暂停仿真
    POST /api/resume      恢复仿真
    POST /api/stop        停止仿真

  数据接口：
    GET  /api/status      当前状态（含UKF估计值、天气风险等）
    GET  /api/history     历史数据（δ/ω的时间序列）
    GET  /api/fault_info  故障检测信息
    GET  /api/fault_replays 故障回放快照
    GET  /api/config      系统配置参数
    GET  /api/weather     天气数据+风险评估

  仿真接口：
    POST /api/simulate_disaster  灾害模拟（disaster: sandstorm/thunderstorm/rain_hail/extreme_temp/icing/heatwave/clear）

  兼容接口（旧版）：
    GET  /status, /history, /events
    POST /control/start, /control/pause, /control/reset, /control/speed


================================================================
五、故障说明
================================================================

仿真故障：母线8-9之间三相短路

时间线（采样频率1000Hz，总仿真时间180秒=3分钟）：
  - 正常状态: 0 - 5秒
  - 故障1: 5秒 - 5.3秒（持续0.3秒）
  - 正常状态: 5.3秒 - 15秒
  - 故障2: 15秒 - 15.3秒（持续0.3秒）
  - 正常状态: 15.3秒 - 180秒（故障切除后持续运行）


================================================================
六、数据格式说明
================================================================

节点数据文件格式（Tab分隔，每个节点3个电压幅值和相角）：

  node1_measurements.txt：
    时间戳\tPG1\tQG1\tV1\tV4\tV5\tangle1\tangle4\tangle5

  node2_measurements.txt：
    时间戳\tPG2\tQG2\tV2\tV6\tV7\tangle2\tangle6\tangle7

  node3_measurements.txt：
    时间戳\tPG3\tQG3\tV3\tV8\tV9\tangle3\tangle8\tangle9

测量数据维度（24维向量）：
  [PG1; PG2; PG3; 
   QG1; QG2; QG3; 
   V1; V2; V3; V4; V5; V6; V7; V8; V9; 
   angle1; angle2; angle3; angle4; angle5; angle6; angle7; angle8; angle9]

状态向量维度（6维）：
  [δ₁; δ₂; δ₃; ω₁; ω₂; ω₃]
  δ = 转子角度（rad）
  ω = 转速（rad/s）


================================================================
七、UKF计算逻辑
================================================================

  - 故障判断：get_system_state(k) 根据 fault_times 判断当前时刻系统状态
    fault_times = [(5.0, 5.3), (15.0, 15.3)]
    ps=0 正常状态, ps=1 故障状态, ps=2 故障后恢复

  - 导纳矩阵切换：Ybusm = YBUS[:, :, ps], RVm = RV[:, :, ps]
    正常时用 YBUS[:,:,0], 故障时用 YBUS[:,:,1], 故障后用 YBUS[:,:,2]

  - 测量数据来源：Z_mes[:, idx]
    完全来自 terminal_node.py 生成的 nodeX_measurements.txt

  - 真实状态来源：X_true[:, idx]
    完全来自 terminal_node.py 生成的 true_states.csv

  - UKF预计算：dashboard_server.py 启动时后台线程自动执行UKF
    结果缓存到 ukf_cache.npz，下次启动直接加载

  - 1秒步进：前端每秒请求一次，显示该秒起始点的估计值和真实值


================================================================
八、重要说明
================================================================

  ★ dashboard_server.py 不自己生成任何数据
  ★ 所有算法模块从 state_new/ 导入（通过 sys.path.insert）
  ★ 所有数据文件从 state_new/ 读取（通过 os.chdir）
  ★ 修改 state_new/ 下的代码后需重启 dashboard_server.py
  ★ 重新生成数据后需删除 ukf_cache.npz 再重启
  ★ 主控端不需要潮流计算！所有系统参数从 system_params.mat 读取
  ★ LoRa只需传输3个txt文件，system_params.mat可赛前预置


================================================================
九、FreeRTOS LoRa 主控移植进度 — 2026-05-27
================================================================

当前状态：主控侧架构移植完成，通信链路双向通，帧解析待验证

【已完成】
1. 数据结构对齐新版GD32
   - NodeSample_t(8 x int16+timestamp, 20B), NodeUploadHeader_t(统一版)
   - MASTER_MAX_NODES=3, MASTER_NODE_UPLOAD_POINTS=40, NODE_SAMPLE_RATE=1000

2. 任务架构对齐新版GD32 (active polling)
   - master_poll_task: Tier1 CMD_POLL_STATUS + Tier2 CMD_REQUEST_WAVEFORM
   - master_judge_task: 超时离线检测(15s)

3. AT配置对齐GD32 LoRa_Init
   - 调度器启动前完成(忙等替代vTaskDelay)
   - ADDR=0x000A, WLRATE=23,7(62.5kbps), PACKSIZE=3(240B), TMODE=1

4. 通信验证
   - 主->节点: Poll命令到达 ✓
   - 节点->主: ISR收到数据(isr=58~116) ✓
   - NODE_HEAD帧(enc=16B)正确解析 ✓

5. 最新修改(待验证)
   - ring_recv_frame: 状态机逐字节接收, 消除payload内假AA55帧头
   - wave_decode_packet: 差分编码解码已集成

【待完成】
- NODE_RAW帧(enc=200B)解析验证(状态机方案待测试)
- 端到端集成压力测试 / RPMsg数据通道完善

【编译部署】
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make all -j$(nproc)
# 部署后必须 sudo reboot (不能用remoteproc stop/start)

【关键文件】
freertos/main.c             — AT配置, ring_recv_frame状态机
freertos/src/master_poll_task.c — 两层轮询
freertos/src/wave_decode.c  — 差分编码解码

【已知问题】
1. reboot后模块射频缓冲区可能残留旧数据包(模块不断电)
2. remoteproc stop/start后AT命令无响应, 必须sudo reboot
3. send_lora_cmd的len=1问题待排查
