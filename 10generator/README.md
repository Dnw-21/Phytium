================================================================
电力系统动态状态估计 - UKF算法 - IEEE 39节点10机系统
================================================================

目录说明：
----------

一、终端端（仿真/测量端） - Python版本
----------------------------------
运行：python terminal_node_39.py

终端端功能：
  - 电力系统初始化（含预置潮流解）
  - 生成测量数据（含单故障场景）
  - 导出测量数据（完整Z向量）、真实状态、系统参数
  - 仿真时长：3分钟（180秒）

终端端需要的文件：
  terminal_node_39.py        (运行脚本)
  initialize_system_39.py    (系统初始化 + 导纳矩阵计算)
  case39_system.py           (系统数据 - IEEE 39节点)
  dynamic_system.py          (动态方程 + 矢量化RK4积分)
  ybus_new.py                (导纳矩阵计算)

Python依赖安装：
  pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

终端端输出文件：
  system_params.mat          (系统参数：YBUS, RV, E_abs, PM, M, D, X_0等)
  true_states.csv            (真实状态：10δ + 10ω = 20维 × 360,000行)
  measurements.txt           (完整测量向量：时间戳 + 98维 Z × 360,000行)


二、主控端（估计/处理端）- Python版本
-------------------------------------
运行：python terminal_controller_39.py

主控端功能：
  - 从measurements.txt读取完整测量数据（无需分节点）
  - 按时间戳还原Z测量向量
  - 执行UKF状态估计
  - 绘制结果曲线（含故障区域标识）

主控端需要的文件：
  terminal_controller_39.py  (运行脚本)
  ukf_estimation_39.py       (UKF估计算法)
  plot_results_39.py         (结果绘图)
  RK4.py                     (矢量化四阶Runge-Kutta积分器)
  dynamic_system.py          (动态方程)

主控端输入文件：
  system_params.mat          (系统参数 - 赛前预置)
  measurements.txt           (完整测量数据 - LoRa/网络传输)
  true_states.csv            (可选，用于验证/画图对比)

主控端输出文件：
  ukf_results_generator1_39.png  ~ ukf_results_generator10_39.png
                               (每台发电机角度+转速对比图)
  ukf_results_all_generators_39.png  (10台发电机汇总图)
  ukf_results_rmse_39.png     (RMSE曲线)

Python依赖安装：
  pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple


三、一键运行（Python版本）
------------------------
运行：python run_all_39.py

功能：
  - 自动运行 terminal_node_39.py 生成数据
  - 自动运行 terminal_controller_39.py 执行UKF估计
  - 输出全部结果图表


四、一体化运行（调试用）
-------------------------
运行：python main_ukf_3min.py
一次性完成终端端+主控端所有步骤，方便调试


五、MATLAB参考版本
------------------
文件：DSE_Calculation_UKF_3min.m
运行：MATLAB中直接运行（需要MATPOWER），作为Python版本的验证参考


六、模块文件说明
----------------
通用模块（终端端和主控端都需要）：
  dynamic_system.py          - 电力系统动态方程（转子摆动方程）+ 矢量化RK4
  RK4.py                     - 矢量化四阶Runge-Kutta积分器（UKF sigma点传播用）
  ybus_new.py                - 母线导纳矩阵 Ybus 计算

终端端专用：
  terminal_node_39.py        - 终端端主程序
  initialize_system_39.py    - 系统初始化（潮流解、导纳矩阵、初值计算）
  case39_system.py           - IEEE 39节点系统数据、Ybus构建、潮流解加载

主控端专用：
  terminal_controller_39.py  - 主控端主程序
  ukf_estimation_39.py       - UKF估计算法
  plot_results_39.py         - 结果绘图

辅助文件：
  run_all_39.py              - 终端→主控一键运行脚本
  main_ukf_3min.py           - 一体化运行脚本（调试用）
  DSE_Calculation_UKF_3min.m - MATLAB参考版本


七、使用流程
------------

  【方案一：一键运行（最方便）】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

  2. 一键运行：
     python run_all_39.py

  3. 查看输出图表


  【方案二：分步运行（比赛模式）】
  1. 安装Python依赖：
     pip install numpy scipy matplotlib -i https://pypi.tuna.tsinghua.edu.cn/simple

  2. 赛前准备（预置 system_params.mat 到主控端）：
     python terminal_node_39.py
     → 生成 system_params.mat、measurements.txt、true_states.csv
     → 将 system_params.mat 拷贝到主控端

  3. 比赛运行：
     a. 终端端运行：python terminal_node_39.py
        → 生成 measurements.txt（包含全部测量数据，无需分节点）

     b. 传输 measurements.txt 到主控端

     c. 主控端运行：python terminal_controller_39.py
        → 读取 measurements.txt → 还原Z向量 → UKF估计 → 绘图


  【MATLAB验证版本】
  1. 打开 MATLAB，确保已安装 MATPOWER
  2. 运行：DSE_Calculation_UKF_3min.m
  3. 查看 MATLAB Figure 输出，与 Python 生成的 PNG 对比


八、故障说明
------------
仿真故障：母线4三相短路，切除线路4-14

时间线（采样频率2000Hz，时间步长0.0005s，总仿真时间180秒=3分钟）：
  - 正常状态: 0 - 5.0秒
  - 故障状态: 5.0秒 - 5.3秒（三相短路，持续0.3秒）
  - 故障后状态: 5.3秒 - 180秒（线路4-14切除后运行）


九、数据格式说明
-----------------

系统数据：
  IEEE 39节点系统 (New England 39-Bus System)
  10台发电机，46条支路（含12台变压器）
  基准功率：100 MVA
  额定频率：60 Hz

发电机参数（按照MATLAB case39数据）：
  发电机母线：Bus 30, 31, 32, 33, 34, 35, 36, 37, 38, 39
  暂态电抗 Xd = [0.006, 0.0697, 0.0531, 0.0436, 0.132, 0.05, 0.049, 0.057, 0.057, 0.031] pu
  惯性常数 H = [500, 30.3, 35.8, 28.6, 26, 34.8, 26.4, 24.3, 34.5, 42] s
  阻尼系数 D = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] / w_syn
  定子电阻 R = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] pu

measurements.txt 格式（CSV，逗号分隔）：

  列头（99列）：
    timestamp,PG1,PG2,...,PG10,
              QG1,QG2,...,QG10,
              V1,V2,...,V39,
              angle1,angle2,...,angle39

  数据行（360,000行，每行一个时间戳）：
    0.000000,2.455715,...,9.840487,...

  Z向量组成（98维）：
    Z[0:10]   = PG₁ ~ PG₁₀     (发电机有功功率, pu)
    Z[10:20]  = QG₁ ~ QG₁₀     (发电机无功功率, pu)
    Z[20:59]  = V₁ ~ V₃₉       (母线电压幅值, pu)
    Z[59:98]  = θ₁ ~ θ₃₉       (母线电压相角, rad)

true_states.csv 格式（CSV，逗号分隔）：

  列头（20列）：
    delta1,delta2,...,delta10,omega1,omega2,...,omega10

  数据行（360,000行）：
    各时间戳对应的真实状态（20维）

  状态向量组成（20维）：
    X[0:10]  = δ₁ ~ δ₁₀  (转子角度, rad)
    X[10:20] = ω₁ ~ ω₁₀  (转速, rad/s)

system_params.mat 包含变量：
  YBUS   - 降阶导纳矩阵 (10×10×3, 故障前/中/后)
  RV     - 电压恢复矩阵 (39×10×3)
  E_abs  - 内电势幅值 (10×1)
  PM     - 机械功率 (10×1)
  M      - 惯性常数 (10×1, M = 2H/w_syn)
  D      - 阻尼系数 (10×1)
  n      - 发电机数 = 10
  s      - 母线数 = 39
  fs     - 采样频率 = 2000 Hz
  t_SW   - 故障开始时间 = 5.0 s
  t_FC   - 故障清除时间 = 5.3 s
  X_0    - 初始状态 (20维)
  sig    - UKF噪声标准差 = 0.01
  gen_bus- 发电机母线编号


十、主控端数据流详解
------------------

  比赛场景中，主控端不需要做任何潮流计算或系统建模，
  所有系统信息从 system_params.mat 读取：

  启动流程：
  1. loadmat('system_params.mat') → 读取系统参数
  2. 读取 measurements.txt → 按列还原 Z[98×N] 矩阵
  3. 逐时间步运行 UKF：
     a. 根据当前时间 k 判断系统状态 (正常/故障/故障后)
     b. 选择对应的 YBUS 和 RV 矩阵
     c. Sigma点采样 → 预测 (RK4) → 测量预测 → 更新 (Kalman增益)
  4. 输出 20维状态估计 + RMSE
  5. 与 true_states.csv 对比画图


十一、仿真参数一览
-----------------
  系统         : IEEE 39节点 (10台发电机, 39条母线, 46条支路)
  算法         : Unscented Kalman Filter (UKF)
  仿真时长     : 180秒 (3分钟)
  采样频率     : 2000 Hz
  时间步长     : 0.0005 秒
  总采样点数   : 360,000
  状态向量维度 : 20维 (10δ + 10ω)
  测量向量维度 : 98维 (10PG + 10QG + 39Vmag + 39Vangle)
  UKF噪声设置  : P0 = Q = R = (0.01)^2 * I
  Sigma点数量  : 2 * ns = 40


十二、文件清单
-------------
终端端文件：
  terminal_node_39.py
  initialize_system_39.py
  case39_system.py
  dynamic_system.py
  ybus_new.py

主控端文件：
  terminal_controller_39.py
  ukf_estimation_39.py
  plot_results_39.py
  RK4.py
  dynamic_system.py

辅助文件：
  run_all_39.py
  main_ukf_3min.py
  DSE_Calculation_UKF_3min.m
  README.md
================================================================
