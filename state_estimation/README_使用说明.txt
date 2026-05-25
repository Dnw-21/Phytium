================================================================
电力系统动态状态估计 - UKF算法 - 文件说明
================================================================

目录说明：
----------

一、终端端（仿真/测量端）
-------------------------
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


二、主控端（估计/处理端）- MATLAB版本
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


三、主控端（估计/处理端）- Python版本
-------------------------------------
运行：python terminal_controller.py

主控端功能：
  - 从TXT文件读取3个节点数据
  - 按时间戳组合测量数据
  - 执行UKF状态估计
  - 绘制结果曲线

主控端需要的文件：
  terminal_controller.py   (运行脚本)
  ukf_estimation.py        (UKF估计算法)
  plot_results.py          (结果绘图)
  RK4.py                   (四阶Runge-Kutta积分)
  dynamic_system.py        (动态方程)

主控端输入文件：
  system_params.mat        (系统参数 - 使用scipy.io.loadmat读取)
  node1_measurements.txt   (节点1数据 - LoRa传输)
  node2_measurements.txt   (节点2数据 - LoRa传输)
  node3_measurements.txt   (节点3数据 - LoRa传输)
  true_states.csv          (可选，用于验证)

Python依赖安装：
  pip install numpy scipy matplotlib


四、调试/完整运行
-----------------
运行：main_ukf.m
一次性运行终端端+主控端（MATLAB），方便调试


五、模块文件说明
----------------
通用模块（终端端和主控端都需要）：
  dynamic_system.m/py      - 电力系统动态方程（转子运动方程）

终端端专用：
  terminal_node.m          - 终端端主程序
  initialize_system.m      - 系统初始化 + 潮流计算
  generate_true_values.m   - 生成测量值和真值
  case9_new_Sauer.m        - 9节点系统数据
  Ybus_new.m               - 导纳矩阵计算

主控端专用（MATLAB）：
  terminal_controller.m    - 主控端主程序
  ukf_estimation.m         - UKF估计算法
  plot_results.m           - 结果绘图
  RK4.m                    - 四阶Runge-Kutta积分

主控端专用（Python）：
  terminal_controller.py   - 主控端主程序
  ukf_estimation.py        - UKF估计算法
  plot_results.py          - 结果绘图
  RK4.py                   - 四阶Runge-Kutta积分
  dynamic_system.py        - 动态方程

其他：
  main_ukf.m               - 完整流程一键运行（调试用）


六、使用流程
------------
比赛场景（推荐）：

1. 赛前准备：
   └── 将 system_params.mat 和 true_states.csv 预置到主控端

2. 比赛时：
   ├── 终端端运行：terminal_node.m
   │   └── 生成3个节点数据文件（带时间戳）
   │
   ├── LoRa传输：
   │   ├── node1_measurements.txt
   │   ├── node2_measurements.txt
   │   └── node3_measurements.txt
   │
   └── 主控端运行：terminal_controller.py（推荐）或 terminal_controller.m
       ├── 读取3个节点的.txt文件
       ├── 按时间戳组合数据（去除时间戳）
       ├── 执行UKF状态估计
       └── 绘制结果曲线

调试场景：
   直接运行：main_ukf.m（MATLAB）
   └── 自动完成所有步骤，方便算法调试


七、故障说明
------------
仿真故障：母线8-9之间三相短路

时间线（采样频率1000Hz）：
  - 正常状态：0 - 60ms    (0 - 59采样点)
  - 故障状态：60 - 80ms   (60 - 79采样点)
  - 故障后状态：80ms之后  (80采样点之后)


八、数据格式说明
-----------------

节点数据文件格式（Tab分隔，每个节点3个电压）：

  node1_measurements.txt：
    时间戳    PG1    QG1    V1    V4    V5    angle1    angle4    angle5

  node2_measurements.txt：
    时间戳    PG2    QG2    V2    V6    V7    angle2    angle6    angle7

  node3_measurements.txt：
    时间戳    PG3    QG3    V3    V8    V9    angle3    angle8    angle9

测量数据维度（24维向量）：
  [PG1; PG2; PG3; 
   QG1; QG2; QG3; 
   V1; V2; V3; V4; V5; V6; V7; V8; V9; 
   angle1; angle2; angle3; angle4; angle5; angle6; angle7; angle8; angle9]

状态向量维度（6维）：
  [δ₁; δ₂; δ₃; ω₁; ω₂; ω₃]
  δ = 转子角度（rad）
  ω = 转速（rad/s）


九、重要说明
------------
★ 主控端不需要潮流计算！所有系统参数从 system_params.mat 读取
★ 主控端可以完全用Python实现，不需要MATPOWER
★ LoRa只需传输3个.txt文件，system_params.mat可赛前预置
★ Python读取MAT文件：scipy.io.loadmat('system_params.mat')


十、文件清单
------------
├── 终端端文件（MATLAB）
│   ├── terminal_node.m
│   ├── initialize_system.m
│   ├── generate_true_values.m
│   ├── dynamic_system.m
│   ├── case9_new_Sauer.m
│   └── Ybus_new.m
│
├── 主控端文件（MATLAB）
│   ├── terminal_controller.m
│   ├── ukf_estimation.m
│   ├── plot_results.m
│   └── RK4.m
│
├── 主控端文件（Python）
│   ├── terminal_controller.py
│   ├── ukf_estimation.py
│   ├── plot_results.py
│   ├── RK4.py
│   └── dynamic_system.py
│
└── 输出文件（终端端生成）
    ├── system_params.mat
    ├── true_states.csv
    ├── True_Values.mat
    ├── node1_measurements.txt
    ├── node2_measurements.txt
    └── node3_measurements.txt

================================================================