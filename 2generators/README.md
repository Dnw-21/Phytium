================================================================
电力系统动态状态估计 - UKF算法 - IEEE 5节点2机系统 (Overbye)
================================================================

目录说明：
----------

一、终端端（仿真/测量端） - Python版本
----------------------------------
运行：python terminal_node_5.py

终端端功能：
  - 电力系统初始化（含预置潮流解）
  - 生成测量数据（含单故障场景）
  - 导出测量数据（完整Z向量）、真实状态、系统参数
  - 仿真时长：3分钟（180秒）

终端端需要的文件：
  terminal_node_5.py            (运行脚本)
  initialize_system_5.py        (系统初始化 + 导纳矩阵计算)
  case5_system.py               (系统数据 - Overbye 5节点)
  dynamic_system.py             (动态方程 + 矢量化RK4积分)
  ybus_new.py                   (导纳矩阵计算)

Python依赖安装：
  pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

终端端输出文件：
  system_params.mat             (系统参数：YBUS, RV, E_abs, PM, M, D, X_0等)
  true_states.csv               (真实状态：2δ + 2ω = 4维 × 360,000行)
  measurements.txt              (完整测量向量：时间戳 + 14维 Z × 360,000行)


二、主控端（估计/处理端）- Python版本
-------------------------------------
运行：python terminal_controller_5.py

主控端功能：
  - 从measurements.txt读取完整测量数据（无需分节点）
  - 按时间戳还原Z测量向量
  - 执行UKF状态估计
  - 绘制结果曲线（含故障区域标识）

主控端需要的文件：
  terminal_controller_5.py      (运行脚本)
  ukf_estimation_5.py           (UKF估计算法)
  plot_results_5.py             (结果绘图)
  RK4.py                        (矢量化四阶Runge-Kutta积分器)
  dynamic_system.py             (动态方程)

主控端输入文件：
  system_params.mat             (系统参数 - 赛前预置)
  measurements.txt              (完整测量数据 - LoRa/网络传输)
  true_states.csv               (可选，用于验证/画图对比)

主控端输出文件：
  ukf_results_generator1_5.png  ~ ukf_results_generator2_5.png
                                (每台发电机角度+转速对比图)
  ukf_results_all_generators_5.png  (2台发电机汇总图)
  ukf_results_rmse_5.png        (RMSE曲线)

Python依赖安装：
  pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple


三、一键运行（Python版本）
------------------------
运行：python run_all_5.py

功能：
  - 自动运行 terminal_node_5.py 生成数据
  - 自动运行 terminal_controller_5.py 执行UKF估计
  - 输出全部结果图表


四、一体化运行（调试用）
-------------------------
运行：python main_ukf_5.py
一次性完成终端端+主控端所有步骤，方便调试


五、MATLAB参考版本
------------------
文件：DSE_Calculation_UKF_5bus_3min.m
运行：MATLAB中直接运行（需要MATPOWER），作为Python版本的验证参考


六、模块文件说明
----------------
通用模块（终端端和主控端都需要）：
  dynamic_system.py             - 电力系统动态方程（转子摆动方程）+ 矢量化RK4
  RK4.py                        - 矢量化四阶Runge-Kutta积分器（UKF sigma点传播用）
  ybus_new.py                   - 母线导纳矩阵 Ybus 计算

终端端专用：
  terminal_node_5.py            - 终端端主程序
  initialize_system_5.py        - 系统初始化（潮流解、导纳矩阵、初值计算）
  case5_system.py               - IEEE 5节点Overbye系统数据、Ybus构建、潮流解加载

主控端专用：
  terminal_controller_5.py      - 主控端主程序
  ukf_estimation_5.py           - UKF估计算法
  plot_results_5.py             - 结果绘图

辅助文件：
  run_all_5.py                  - 终端→主控一键运行脚本
  main_ukf_5.py                 - 一体化运行脚本（调试用）
  DSE_Calculation_UKF_5bus_3min.m - MATLAB参考版本


七、使用流程
------------

  【方案一：一键运行（最方便）】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

  2. 一键运行：
     python run_all_5.py

  3. 查看输出图表


  【方案二：分步运行（比赛模式）】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

  2. 赛前准备（预置 system_params.mat 到主控端）：
     python terminal_node_5.py
     → 生成 system_params.mat、measurements.txt、true_states.csv
     → 将 system_params.mat 拷贝到主控端

  3. 比赛运行：
     a. 终端端运行：python terminal_node_5.py
        → 生成 measurements.txt（包含全部测量数据，无需分节点）

     b. 传输 measurements.txt 到主控端

     c. 主控端运行：python terminal_controller_5.py
        → 读取 measurements.txt → 还原Z向量 → UKF估计 → 绘图


  【MATLAB验证版本】
  1. 打开 MATLAB，确保已安装 MATPOWER
  2. 运行：DSE_Calculation_UKF_5bus_3min.m
  3. 查看 MATLAB Figure 输出，与 Python 生成的 PNG 对比


八、故障说明
------------
仿真故障：母线2三相短路，切除线路2-4

时间线（采样频率2000Hz，时间步长0.0005s，总仿真时间180秒=3分钟）：
  - 正常状态: 0 - 5.0秒
  - 故障状态: 5.0秒 - 5.3秒（三相短路，持续0.3秒）
  - 故障后状态: 5.3秒 - 180秒（线路2-4切除后运行）


九、数据格式说明
-----------------

系统数据：
  Overbye 5节点系统 (IEEE 5-Bus Overbye System)
  2台发电机，7条支路
  基准功率：100 MVA
  额定频率：60 Hz

发电机参数（按照MATLAB case5_Overbye数据）：
  发电机母线：Bus 1 (Slack), Bus 3 (PV)
  暂态电抗 Xd = [0.05, 0.025] pu
  惯性常数 H = [5.0, 3.0] s
  阻尼系数 D = [0, 0]
  定子电阻 R = [0, 0] pu

潮流解说明：
  潮流解数据来自 PYPOWER runpf(case5_Overbye)，已预置在 case5_system.py 中：
    母线1 (Slack): Vm=1.060,   Va=   0.00°, Pg=842.94 MW, Qg=340.58 MVar
    母线2 (PQ):    Vm=0.8395,  Va= -23.12°
    母线3 (PV):    Vm=1.000,   Va= -47.74°, Pg=350.00 MW, Qg=573.21 MVar
    母线4 (PQ):    Vm=0.8776,  Va= -48.57°
    母线5 (PQ):    Vm=0.8633,  Va= -22.51°

  注意：此潮流解中Gen2无功力率(573 MVar)超过其Qmax限值(300 MVar)，
  这是因为PYPOWER默认不强制Q限值。MATLAB/MATPOWER的runpf默认会强制
  Q限值，导致Gen 2转换为PQ节点（Q固定为300 MVar），电压降低。
  两种解的初始工作点不同，因此故障前后的动态轨迹会有差异，但Python版本
  的初始化是自洽的（PM-Pe ≈ 10⁻¹⁵），不会产生人工漂移。

measurements.txt 格式（CSV，逗号分隔）：

  列头（15列）：
    timestamp,PG1,PG2,
              QG1,QG2,
              V1,V2,V3,V4,V5,
              angle1,angle2,angle3,angle4,angle5

  数据行（360,000行，每行一个时间戳）：
    0.000000,2.455715,...,-0.027287,...

  Z向量组成（14维）：
    Z[0:2]   = PG₁ ~ PG₂       (发电机有功功率, pu)
    Z[2:4]   = QG₁ ~ QG₂       (发电机无功功率, pu)
    Z[4:9]   = V₁ ~ V₅         (母线电压幅值, pu)
    Z[9:14]  = θ₁ ~ θ₅         (母线电压相角, rad)

true_states.csv 格式（CSV，逗号分隔）：

  列头（4列）：
    delta1,delta2,omega1,omega2

  数据行（360,000行）：
    各时间戳对应的真实状态（4维）

  状态向量组成（4维）：
    X[0:2]  = δ₁ ~ δ₂  (转子角度, rad)
    X[2:4]  = ω₁ ~ ω₂  (转速, rad/s)

system_params.mat 包含变量：
  YBUS   - 降阶导纳矩阵 (2×2×3, 故障前/中/后)
  RV     - 电压恢复矩阵 (5×2×3)
  E_abs  - 内电势幅值 (2×1)
  PM     - 机械功率 (2×1)
  M      - 惯性常数 (2×1, M = 2H/w_syn)
  D      - 阻尼系数 (2×1)
  n      - 发电机数 = 2
  s      - 母线数 = 5
  fs     - 采样频率 = 2000 Hz
  t_SW   - 故障开始时间 = 5.0 s
  t_FC   - 故障清除时间 = 5.3 s
  X_0    - 初始状态 (4维)
  sig    - UKF噪声标准差 = 0.01
  gen_bus- 发电机母线编号


十、主控端数据流详解
------------------

  比赛场景中，主控端不需要做任何潮流计算或系统建模，
  所有系统信息从 system_params.mat 读取：

  启动流程：
  1. loadmat('system_params.mat') → 读取系统参数
  2. 读取 measurements.txt → 按列还原 Z[14×N] 矩阵
  3. 逐时间步运行 UKF：
     a. 根据当前时间 k 判断系统状态 (正常/故障/故障后)
     b. 选择对应的 YBUS 和 RV 矩阵
     c. Sigma点采样 → 预测 (RK4) → 测量预测 → 更新 (Kalman增益)
  4. 输出 4维状态估计 + RMSE
  5. 与 true_states.csv 对比画图


十一、与Case39（10机39节点）对比
-------------------------------

  参数                | Case39 (10机)        | Case5 Overbye (2机)
  --------------------|----------------------|---------------------
  系统                | IEEE 39节点          | Overbye 5节点
  发电机数            | 10                   | 2
  母线数              | 39                   | 5
  状态向量维度        | 20维 (10δ+10ω)       | 4维 (2δ+2ω)
  测量向量维度        | 98维                 | 14维
  故障位置            | 母线4                | 母线2
  切除线路            | 线路4-14             | 线路2-4
  Sigma点数量         | 40                   | 8

  发电机参数对比：
  参数      | Case39 (10机)                              | Case5 (2机)
  ----------|-------------------------------------------|-------------
  Xd (pu)   | [0.006,0.0697,0.0531,0.0436,0.132,...]   | [0.05, 0.025]
  H (s)     | [500,30.3,35.8,28.6,26,34.8,26.4,...]    | [5.0, 3.0]


十二、仿真参数一览
-----------------
  系统         : Overbye 5节点 (2台发电机, 5条母线, 7条支路)
  算法         : Unscented Kalman Filter (UKF)
  仿真时长     : 180秒 (3分钟)
  采样频率     : 2000 Hz
  时间步长     : 0.0005 秒
  总采样点数   : 360,000
  状态向量维度 : 4维 (2δ + 2ω)
  测量向量维度 : 14维 (2PG + 2QG + 5Vmag + 5Vangle)
  UKF噪声设置  : P0 = Q = R = (0.01)^2 * I
  Sigma点数量  : 2 * ns = 8


十三、输出图表示例
-----------------

运行 python main_ukf_5.py 后生成以下图表：

  ukf_results_generator1_5.png   - 发电机1 (Bus 1) 转子角度+转速 实际值vs UKF估计
  ukf_results_generator2_5.png   - 发电机2 (Bus 3) 转子角度+转速 实际值vs UKF估计
  ukf_results_all_generators_5.png - 2台发电机汇总图
  ukf_results_rmse_5.png         - UKF估计误差协方差 (RMSE) 曲线

  图表特点：
  - 红色虚线 = UKF估计值
  - 蓝色实线 = 真实值（来自动态仿真）
  - 红色阴影区域 = 故障期间 (5.0s - 5.3s)
  - UKF估计在故障发生和切除时能快速跟踪系统状态变化


十四、文件清单
-------------
终端端文件：
  terminal_node_5.py
  initialize_system_5.py
  case5_system.py
  dynamic_system.py
  ybus_new.py

主控端文件：
  terminal_controller_5.py
  ukf_estimation_5.py
  plot_results_5.py
  RK4.py
  dynamic_system.py

辅助文件：
  run_all_5.py
  main_ukf_5.py
  DSE_Calculation_UKF_5bus_3min.m
  Ybus_new.m
  README.md
================================================================
