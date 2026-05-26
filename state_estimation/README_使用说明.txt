================================================================
电力系统动态状态估计 - UKF算法 - 文件说明
================================================================

目录说明：
----------

一、终端端（仿真/测量端） - MATLAB版本
----------------------------------
运行：terminal_node.m

终端端功能：
  - 电力系统潮流计算（runpf）
  - 生成测量数据（含故障场景）
  - 导出TXT和MAT文件
  - 按发电机区域分配节点数据

终端端需要的文件：
  terminal_node.m          (运行脚本)
  initialize_system.m      (系统初始化 + 潮流计算)
  generate_true_values.m   (生成真值和测量值)
  dynamic_system.m         (动态方程)
  case9_new_Sauer.m        (系统数据 - 9节点系统)
  Ybus_new.m               (导纳矩阵计算)

终端端输出文件：
  system_params.mat        (系统参数：YBUS, RV, E_abs, PM, M, D等)
  true_states.csv          (真实状态：δ₁, δ₂, δ₃, ω₁, ω₂, ω₃)
  True_Values.mat          (MATLAB完整数据)
  
  node1_measurements.txt   (节点1数据 - Generator 1区域)
  node2_measurements.txt   (节点2数据 - Generator 2区域)
  node3_measurements.txt   (节点3数据 - Generator 3区域)


二、终端端（仿真/测量端） - Python版本
----------------------------------
运行：python terminal_node.py

终端端功能：
  - 电力系统潮流计算（使用pandapower）
  - 生成测量数据（支持多故障场景）
  - 导出TXT和MAT文件
  - 按发电机区域分配节点数据
  - 仿真时长：3分钟（180秒）
  - 多故障配置：5秒和15秒各发生一次故障

终端端需要的文件：
  terminal_node.py          (运行脚本)
  initialize_system.py      (系统初始化 + 潮流计算)
  generate_true_values.py   (生成真值和测量值)
  dynamic_system.py         (动态方程)
  case9_sauer.py            (系统数据 - 9节点系统)
  ybus_new.py               (导纳矩阵计算)

Python依赖安装：
  pip install numpy scipy matplotlib pandas pandapower -i https://pypi.tuna.tsinghua.edu.cn/simple

终端端输出文件：
  system_params.mat        (系统参数：YBUS, RV, E_abs, PM, M, D等)
  true_states.csv          (真实状态：δ₁, δ₂, δ₃, ω₁, ω₂, ω₃)
  
  node1_measurements.txt   (节点1数据 - Generator 1区域)
  node2_measurements.txt   (节点2数据 - Generator 2区域)
  node3_measurements.txt   (节点3数据 - Generator 3区域)


三、主控端（估计/处理端）- MATLAB版本
-------------------------------------
运行：terminal_controller.m

主控端功能：
  - 从TXT文件读取3个节点数据
  - 按时间戳组合测量数据
  - 执行UKF状态估计
  - 绘制结果曲线

主控端需要的文件：
  terminal_controller.m    (运行脚本)
  ukf_estimation.m         (UKF估计算法)
  plot_results.m           (结果绘图)
  RK4.m                    (四阶Runge-Kutta积分)
  dynamic_system.m         (动态方程)

主控端输入文件：
  system_params.mat        (系统参数 - 预置)
  node1_measurements.txt   (节点1数据 - LoRa传输)
  node2_measurements.txt   (节点2数据 - LoRa传输)
  node3_measurements.txt   (节点3数据 - LoRa传输)
  true_states.csv          (可选，用于验证)

主控端输出文件：
  UKF_Estimation_Results.mat  (估计结果)


四、主控端（估计/处理端） - Python版本
-------------------------------------
运行：python terminal_controller.py

主控端功能：
  - 从TXT文件读取3个节点数据
  - 按时间戳组合测量数据
  - 执行UKF状态估计（支持多故障场景）
  - 绘制结果曲线（显示多个故障区域）

主控端需要的文件：
  terminal_controller.py    (运行脚本)
  ukf_estimation.py         (UKF估计算法)
  plot_results.py           (结果绘图)
  RK4.py                    (四阶Runge-Kutta积分)
  dynamic_system.py         (动态方程)

主控端输入文件：
  system_params.mat        (系统参数 - 使用scipy.io.loadmat读取)
  node1_measurements.txt   (节点1数据 - LoRa传输)
  node2_measurements.txt   (节点2数据 - LoRa传输)
  node3_measurements.txt   (节点3数据 - LoRa传输)
  true_states.csv          (可选，用于验证)

Python依赖安装：
  pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple


五、一键运行（Python版本）
------------------------
运行：python run_all.py

功能：
  - 自动运行 terminal_node.py 生成数据
  - 自动运行 terminal_controller.py 执行UKF估计
  - 显示结果图表


六、调试/完整运行（MATLAB）
---------------------------
运行：main_ukf.m
一次性运行终端端+主控端，方便调试


七、模块文件说明
----------------
通用模块（终端端和主控端都需要）：
  dynamic_system.m/py      - 电力系统动态方程（转子运动方程）
  RK4.m/py                - 四阶Runge-Kutta积分

终端端专用：
  terminal_node.m/py      - 终端端主程序
  initialize_system.m/py  - 系统初始化 + 潮流计算
  generate_true_values.m/py  - 生成测量值和真值
  case9_sauer.m/py        - 9节点系统数据
  ybus_new.m/py           - 导纳矩阵计算

主控端专用（MATLAB）：
  terminal_controller.m   - 主控端主程序
  ukf_estimation.m        - UKF估计算法
  plot_results.m          - 结果绘图
  RK4.m                   - 四阶Runge-Kutta积分

主控端专用（Python）：
  terminal_controller.py  - 主控端主程序
  ukf_estimation.py       - UKF估计算法
  plot_results.py         - 结果绘图
  RK4.py                  - 四阶Runge-Kutta积分
  dynamic_system.py       - 动态方程

辅助文件：
  main_ukf.m              - MATLAB完整流程一键运行
  run_all.py              - Python完整流程一键运行


八、使用流程
------------
Python版本推荐（更现代）：

  【方案一：一键运行（最方便）】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib pandas pandapower -i https://pypi.tuna.tsinghua.edu.cn/simple
  
  2. 一键运行：
     python run_all.py
  
  3. 查看图表结果


  【方案二：分步运行】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib pandas pandapower -i https://pypi.tuna.tsinghua.edu.cn/simple
  
  2. 赛前准备（预置system_params.mat）：
     python terminal_node.py  # 生成一次数据，保留system_params.mat
  
  3. 比赛运行：
     a. 终端端运行：python terminal_node.py  # 生成节点数据
     b. 通过LoRa传输3个txt文件到主控端
     c. 主控端运行：python terminal_controller.py  # 执行UKF估计


  【MATLAB版本】
  比赛场景：
  1. 终端端运行：terminal_node.m
     → 生成3个txt文件（带时间戳）
  2. 通过LoRa传输以下文件给主控：
     - system_params.mat（系统参数）
     - node1_measurements.txt（节点1数据）
     - node2_measurements.txt（节点2数据）
     - node3_measurements.txt（节点3数据）
  3. 主控端运行：terminal_controller.m
     → 读取txt文件，按时间戳组合，执行UKF，绘图

  调试场景：
  直接运行：main_ukf.m  # 自动完成所有步骤


九、故障说明
------------
仿真故障：母线8-9之间三相短路

时间线（采样频率1000Hz，总仿真时间180秒=3分钟）：
  - 正常状态: 0 - 5秒
  - 故障1: 5秒 - 5.3秒（持续0.3秒）
  - 正常状态: 5.3秒 - 15秒
  - 故障2: 15秒 - 15.3秒（持续0.3秒）
  - 正常状态: 15.3秒 - 180秒（故障切除后持续运行）


十、数据格式说明
-----------------

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


十一、重要说明
--------------
  ★ 主控端不需要潮流计算！所有系统参数从system_params.mat读取
  ★ 主控端可以完全用Python实现，不需要MATPOWER
  ★ LoRa只需传输3个txt文件，system_params.mat可赛前预置
  ★ Python读取MAT文件：使用scipy.io.loadmat('system_params.mat')
  ★ 故障在5秒和15秒各发生一次，每次持续0.3秒
  ★ 总仿真时间3分钟（180秒），故障切除后系统继续运行


十二、文件清单
-------------
终端端文件（MATLAB）：
  terminal_node.m
  initialize_system.m
  generate_true_values.m
  dynamic_system.m
  case9_new_Sauer.m
  Ybus_new.m

终端端文件（Python）：
  terminal_node.py
  initialize_system.py
  generate_true_values.py
  dynamic_system.py
  case9_sauer.py
  ybus_new.py

主控端文件（MATLAB）：
  terminal_controller.m
  ukf_estimation.m
  plot_results.m
  RK4.m

主控端文件（Python）：
  terminal_controller.py
  ukf_estimation.py
  plot_results.py
  RK4.py
  dynamic_system.py

辅助文件：
  main_ukf.m
  run_all.py
  README_使用说明.txt
================================================================

十三、FreeRTOS LoRa 主控移植进度 — 2026-05-27
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
================================================================
