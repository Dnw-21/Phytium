#!/usr/bin/env python3
"""
终端端程序：电力系统仿真数据生成（Python版本）

功能：
1. 初始化系统参数（潮流计算）
2. 生成真实状态和测量数据（支持多故障场景）
3. 按节点分配数据
4. 导出TXT文件（用于LoRa传输）
5. 保存系统参数（用于主控端）

运行方式：
    python terminal_node.py

依赖安装：
    pip install numpy scipy matplotlib pandas pandapower
"""

import numpy as np
import scipy.io as sio
import os

def main():
    print("===========================================")
    print("终端端：电力系统仿真数据生成（Python版本）")
    print("===========================================\n")
    
    # ==================== 第一步：初始化系统 ====================
    print("正在初始化系统...")
    from initialize_system import initialize_system
    YBUS, RV, E_abs, PM, M, D, n, s, gen_bus, fs, fault_times, total_time = initialize_system()
    
    # ==================== 第二步：生成仿真数据 ====================
    print("\n正在生成仿真数据...")
    from generate_true_values import generate_true_values
    X_true, measurements, t_points = generate_true_values(
        YBUS, RV, E_abs, PM, M, D, n, s, fs, 
        fault_times, total_time, gen_bus
    )
    
    # ==================== 第三步：保存系统参数 ====================
    print("\n正在保存系统参数...")
    sio.savemat('system_params.mat', {
        'YBUS': YBUS,
        'RV': RV,
        'E_abs': E_abs,
        'PM': PM,
        'M': M,
        'D': D,
        'n': n,
        's': s,
        'fs': fs,
        'fault_times': np.array(fault_times),
        'total_time': total_time
    })
    print("系统参数已保存到 system_params.mat")
    
    # ==================== 第四步：保存真实状态 ====================
    print("正在保存真实状态...")
    # 添加时间戳列
    time_col = t_points / fs  # 转换为时间（秒）
    X_true_with_time = np.column_stack([time_col, X_true.T])
    np.savetxt('true_states.csv', X_true_with_time, delimiter=',')
    print("真实状态已保存到 true_states.csv")
    
    # ==================== 第五步：按节点分配数据 ====================
    print("\n正在按节点分配数据...")
    
    # 节点1数据：PG1, QG1, V1, V4, V5, angle1, angle4, angle5
    node1_data = np.zeros((len(t_points), 10))
    node1_data[:, 0] = t_points / fs  # 时间戳
    node1_data[:, 1] = measurements[0, :]  # PG1
    node1_data[:, 2] = measurements[3, :]  # QG1
    node1_data[:, 3] = measurements[6, :]  # V1
    node1_data[:, 4] = measurements[9, :]  # V4
    node1_data[:, 5] = measurements[10, :]  # V5
    node1_data[:, 6] = measurements[15, :]  # angle1
    node1_data[:, 7] = measurements[18, :]  # angle4
    node1_data[:, 8] = measurements[19, :]  # angle5
    
    # 节点2数据：PG2, QG2, V2, V6, V7, angle2, angle6, angle7
    node2_data = np.zeros((len(t_points), 10))
    node2_data[:, 0] = t_points / fs  # 时间戳
    node2_data[:, 1] = measurements[1, :]  # PG2
    node2_data[:, 2] = measurements[4, :]  # QG2
    node2_data[:, 3] = measurements[7, :]  # V2
    node2_data[:, 4] = measurements[11, :]  # V6
    node2_data[:, 5] = measurements[12, :]  # V7
    node2_data[:, 6] = measurements[16, :]  # angle2
    node2_data[:, 7] = measurements[20, :]  # angle6
    node2_data[:, 8] = measurements[21, :]  # angle7
    
    # 节点3数据：PG3, QG3, V3, V8, V9, angle3, angle8, angle9
    node3_data = np.zeros((len(t_points), 10))
    node3_data[:, 0] = t_points / fs  # 时间戳
    node3_data[:, 1] = measurements[2, :]  # PG3
    node3_data[:, 2] = measurements[5, :]  # QG3
    node3_data[:, 3] = measurements[8, :]  # V3
    node3_data[:, 4] = measurements[13, :]  # V8
    node3_data[:, 5] = measurements[14, :]  # V9
    node3_data[:, 6] = measurements[17, :]  # angle3
    node3_data[:, 7] = measurements[22, :]  # angle8
    node3_data[:, 8] = measurements[23, :]  # angle9
    
    # ==================== 第六步：保存节点数据文件 ====================
    print("正在保存节点数据文件...")
    
    # 保存为Tab分隔的TXT文件
    np.savetxt('node1_measurements.txt', node1_data, delimiter='\t', fmt='%.6f')
    np.savetxt('node2_measurements.txt', node2_data, delimiter='\t', fmt='%.6f')
    np.savetxt('node3_measurements.txt', node3_data, delimiter='\t', fmt='%.6f')
    
    print("节点1数据已保存到 node1_measurements.txt")
    print("节点2数据已保存到 node2_measurements.txt")
    print("节点3数据已保存到 node3_measurements.txt")
    
    # ==================== 总结 ====================
    print("\n===========================================")
    print("终端端任务完成！")
    print("===========================================")
    print("\n生成的文件：")
    print("  ├── system_params.mat    (系统参数)")
    print("  ├── true_states.csv      (真实状态)")
    print("  ├── node1_measurements.txt")
    print("  ├── node2_measurements.txt")
    print("  └── node3_measurements.txt")
    print("\n仿真参数：")
    print(f"  采样频率: {fs} Hz")
    print(f"  总仿真时间: {total_time}秒 ({total_time/60:.1f}分钟)")
    print(f"  总数据点数: {len(t_points)}")
    print("  故障配置:")
    for i, (start, end) in enumerate(fault_times):
        print(f"    故障{i+1}: {start:.1f}秒 - {end:.1f}秒 (持续{end-start:.2f}秒)")

if __name__ == '__main__':
    main()