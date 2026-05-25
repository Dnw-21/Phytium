#!/usr/bin/env python3
"""
主控端程序：UKF动态状态估计（Python版本）

功能：
1. 从 system_params.mat 读取系统参数（使用scipy.io.loadmat）
2. 读取三个节点的测量数据文件（node1/2/3_measurements.txt）
3. 读取真实状态文件（true_states.csv，用于验证）
4. 按时间戳组合测量数据
5. 执行UKF状态估计
6. 绘制结果

需要的文件（提前预置到主控端）：
- system_params.mat - 系统参数
- true_states.csv - 真实状态（用于验证，可选）

需要传输的文件：
- node1_measurements.txt - 节点1测量数据
- node2_measurements.txt - 节点2测量数据  
- node3_measurements.txt - 节点3测量数据

运行方式：
    python terminal_controller.py
"""

import numpy as np
import scipy.io as sio
import os

def main():
    print("===========================================")
    print("主控端：UKF动态状态估计（Python版本）")
    print("===========================================\n")
    
    # ==================== 第一步：加载系统参数 ====================
    print("正在加载系统参数...")
    
    if not os.path.exists('system_params.mat'):
        raise FileNotFoundError("错误：找不到 system_params.mat 文件！\n请确保已将系统参数文件预置到主控端")
    
    # 使用 scipy.io.loadmat 读取 .mat 文件
    data = sio.loadmat('system_params.mat')
    
    # 提取参数（注意：loadmat 返回的是字典，数组形状需要调整）
    YBUS = data['YBUS']
    RV = data['RV']
    E_abs = data['E_abs'].flatten()
    PM = data['PM'].flatten()
    M = data['M'].flatten()
    D = data['D'].flatten()
    n = int(data['n'][0, 0])
    s = int(data['s'][0, 0])
    fs = int(data['fs'][0, 0])
    num_normal = int(data['num_normal'][0, 0])
    num_fault = int(data['num_fault'][0, 0])
    t_SW = float(data['t_SW'][0, 0])
    t_FC = float(data['t_FC'][0, 0])
    
    print(f"系统参数加载完成！")
    print(f"  发电机数: {n}, 节点数: {s}")
    print(f"  采样频率: {fs} Hz")
    print(f"  正常状态点: {num_normal}, 故障状态点: {num_fault}\n")
    
    # ==================== 第二步：读取节点测量数据 ====================
    print("正在读取节点数据...")
    
    # 检查文件是否存在
    node_files = ['node1_measurements.txt', 'node2_measurements.txt', 'node3_measurements.txt']
    for f in node_files:
        if not os.path.exists(f):
            raise FileNotFoundError(f"错误：找不到 {f} 文件！\n请确保LoRa已接收到所有3个节点的数据文件")
    
    # 读取节点1数据：时间戳, PG1, QG1, V1, V4, V5, angle1, angle4, angle5
    node1_data = np.loadtxt('node1_measurements.txt', delimiter='\t')
    node1_time = node1_data[:, 0]
    node1_PG1 = node1_data[:, 1]
    node1_QG1 = node1_data[:, 2]
    node1_V1 = node1_data[:, 3]
    node1_V4 = node1_data[:, 4]
    node1_V5 = node1_data[:, 5]
    node1_angle1 = node1_data[:, 6]
    node1_angle4 = node1_data[:, 7]
    node1_angle5 = node1_data[:, 8]
    
    # 读取节点2数据：时间戳, PG2, QG2, V2, V6, V7, angle2, angle6, angle7
    node2_data = np.loadtxt('node2_measurements.txt', delimiter='\t')
    node2_PG2 = node2_data[:, 1]
    node2_QG2 = node2_data[:, 2]
    node2_V2 = node2_data[:, 3]
    node2_V6 = node2_data[:, 4]
    node2_V7 = node2_data[:, 5]
    node2_angle2 = node2_data[:, 6]
    node2_angle6 = node2_data[:, 7]
    node2_angle7 = node2_data[:, 8]
    
    # 读取节点3数据：时间戳, PG3, QG3, V3, V8, V9, angle3, angle8, angle9
    node3_data = np.loadtxt('node3_measurements.txt', delimiter='\t')
    node3_PG3 = node3_data[:, 1]
    node3_QG3 = node3_data[:, 2]
    node3_V3 = node3_data[:, 3]
    node3_V8 = node3_data[:, 4]
    node3_V9 = node3_data[:, 5]
    node3_angle3 = node3_data[:, 6]
    node3_angle8 = node3_data[:, 7]
    node3_angle9 = node3_data[:, 8]
    
    print("节点数据读取完成！")
    
    # ==================== 第三步：按时间戳组合测量数据 ====================
    print("正在按时间戳组合数据...")
    
    # 使用节点1的时间戳作为基准
    t_points = node1_time
    num_samples = len(t_points)
    
    # 组合测量数据（格式与MATLAB版本一致）
    # [PG1; PG2; PG3; QG1; QG2; QG3; V1; V2; V3; V4; V5; V6; V7; V8; V9; angle1; angle2; angle3; angle4; angle5; angle6; angle7; angle8; angle9]
    measurements = np.zeros((2 * n + 2 * s, num_samples))
    
    # PG (3个发电机)
    measurements[0, :] = node1_PG1
    measurements[1, :] = node2_PG2
    measurements[2, :] = node3_PG3
    
    # QG (3个发电机)
    measurements[3, :] = node1_QG1
    measurements[4, :] = node2_QG2
    measurements[5, :] = node3_QG3
    
    # Vmag (9个节点电压幅值)
    measurements[6, :] = node1_V1    # V1
    measurements[7, :] = node2_V2    # V2
    measurements[8, :] = node3_V3    # V3
    measurements[9, :] = node1_V4    # V4
    measurements[10, :] = node1_V5   # V5
    measurements[11, :] = node2_V6   # V6
    measurements[12, :] = node2_V7   # V7
    measurements[13, :] = node3_V8   # V8
    measurements[14, :] = node3_V9   # V9
    
    # Vangle (9个节点电压相角)
    measurements[15, :] = node1_angle1  # angle1
    measurements[16, :] = node2_angle2  # angle2
    measurements[17, :] = node3_angle3  # angle3
    measurements[18, :] = node1_angle4  # angle4
    measurements[19, :] = node1_angle5  # angle5
    measurements[20, :] = node2_angle6  # angle6
    measurements[21, :] = node2_angle7  # angle7
    measurements[22, :] = node3_angle8  # angle8
    measurements[23, :] = node3_angle9  # angle9
    
    print(f"数据组合完成！数据点数: {num_samples}\n")
    
    # ==================== 第四步：加载真实状态（可选，用于验证） ====================
    X_true = None
    if os.path.exists('true_states.csv'):
        print("找到真实状态文件，将用于验证...")
        data_true = np.loadtxt('true_states.csv', delimiter=',')
        X_true = data_true[:, 1:].T  # 第一列是时间戳，跳过
    else:
        print("未找到真实状态文件，仅进行估计，不验证...")
    
    # ==================== 第五步：执行UKF状态估计 ====================
    print("正在执行UKF状态估计...")
    
    from ukf_estimation import ukf_estimation
    X_est, RMSE_cov, RMSE_actual = ukf_estimation(
        YBUS, RV, E_abs, PM, M, D, n, s, fs, 
        num_normal, num_fault, t_SW, t_FC, 
        measurements, X_true
    )
    
    # ==================== 第六步：绘制结果 ====================
    print("\n正在绘制估计结果...")
    
    from plot_results import plot_results
    plot_results(X_true, X_est, RMSE_cov, RMSE_actual, t_points, fs, n, num_normal, num_fault)
    
    # ==================== 显示总结 ====================
    print("\n===========================================")
    print("主控端任务完成！")
    print("===========================================")
    print("\n仿真参数：")
    print(f"  采样频率: {fs} Hz")
    print(f"  正常状态点: {num_normal}")
    print(f"  故障状态点: {num_fault}")
    print(f"  故障开始: {t_SW * 1000:.1f} ms")
    print(f"  故障结束: {t_FC * 1000:.1f} ms")
    print("\n结果已显示在绘图窗口中")

if __name__ == '__main__':
    main()
