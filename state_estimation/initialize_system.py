import numpy as np
from case9_sauer import case9_sauer
from ybus_new import ybus_new

def initialize_system():
    """
    初始化电力系统参数
    
    返回:
        YBUS: 导纳矩阵 (n x n x 3)
        RV: 状态矩阵 (s x n x 3)
        E_abs: 电动势幅值向量
        PM: 机械功率向量
        M: 惯性常数向量
        D: 阻尼系数向量
        n: 发电机数量
        s: 节点数量
        gen_bus: 发电机母线索引
        fs: 采样频率
        fault_times: 故障时间配置列表
        total_time: 总仿真时间（秒）
    """
    # 系统参数
    fs = 1000  # 采样频率 (Hz)
    
    # 多故障配置
    # 格式: [(故障开始时间, 故障结束时间), ...]
    fault_times = [
        (5.0, 5.3),   # 第一次故障：5秒发生，持续0.3秒
        (15.0, 15.3)  # 第二次故障：15秒发生，持续0.3秒
    ]
    
    # 总仿真时间：3分钟 = 180秒
    total_time = 180.0
    
    # 获取系统数据
    net, gen_bus, PM, M, D = case9_sauer()
    
    n = len(gen_bus)  # 发电机数量
    s = len(net.bus)  # 节点数量
    
    # 计算导纳矩阵
    YBUS, RV = ybus_new(net)
    
    # 获取电动势幅值（发电机端电压幅值）
    E_abs = np.array([1.0487, 1.0316, 1.0278])  # 标幺值
    
    # 转换单位（p.u.）
    base_power = 100  # 基准功率 (MVA)
    PM = PM / base_power  # 转换为标幺值
    M = M * base_power / 314.16  # 转换为标幺值
    D = D * base_power / 314.16  # 转换为标幺值
    
    print("系统初始化完成！")
    print(f"  发电机数: {n}, 节点数: {s}")
    print(f"  采样频率: {fs} Hz")
    print(f"  总仿真时间: {total_time}秒 ({total_time/60:.1f}分钟)")
    print(f"  故障配置:")
    for i, (start, end) in enumerate(fault_times):
        print(f"    故障{i+1}: {start:.1f}秒 - {end:.1f}秒 (持续{end-start:.2f}秒)")
    
    return YBUS, RV, E_abs, PM, M, D, n, s, gen_bus, fs, fault_times, total_time