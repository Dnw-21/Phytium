import numpy as np
import time

def generate_true_values(YBUS, RV, E_abs, PM, M, D, n, s, fs, fault_times, total_time, gen_bus):
    """
    生成电力系统仿真的真实状态和测量数据（支持多故障场景）
    
    参数:
        YBUS: 导纳矩阵 (n x n x 3)
        RV: 状态矩阵 (s x n x 3)
        E_abs: 电动势幅值向量
        PM: 机械功率向量
        M: 惯性常数向量
        D: 阻尼系数向量
        n: 发电机数量
        s: 节点数量
        fs: 采样频率
        fault_times: 故障时间配置列表，格式: [(t1_start, t1_end), (t2_start, t2_end), ...]
        total_time: 总仿真时间（秒）
        gen_bus: 发电机母线索引
    
    返回:
        X_true: 真实状态 (2n x num_samples)
        measurements: 测量数据 (2n+2s x num_samples)
        t_points: 时间点数组
    """
    from dynamic_system import dynamic_system
    
    num_samples = int(total_time * fs)
    deltt = 1.0 / fs
    
    # 状态维度和测量维度
    ns = 2 * n
    nm = 2 * n + 2 * s
    
    # 初始化状态向量
    X_true = np.zeros((ns, num_samples))
    measurements = np.zeros((nm, num_samples))
    t_points = np.arange(num_samples)
    
    # 初始状态
    X = np.zeros(ns)
    X[:n] = np.angle(E_abs)  # 初始转子角度
    X[n:] = 0  # 初始转速
    
    # 四阶Runge-Kutta积分
    def rk4_step(X, t, deltt, Ybusm):
        k1 = deltt * dynamic_system(t, X, M, D, Ybusm, E_abs, PM, n)
        k2 = deltt * dynamic_system(t + deltt/2, X + k1/2, M, D, Ybusm, E_abs, PM, n)
        k3 = deltt * dynamic_system(t + deltt/2, X + k2/2, M, D, Ybusm, E_abs, PM, n)
        k4 = deltt * dynamic_system(t + deltt, X + k3, M, D, Ybusm, E_abs, PM, n)
        return X + (k1 + 2*k2 + 2*k3 + k4) / 6
    
    # 判断当前时间是否在故障期间
    def is_in_fault(t):
        for (t_start, t_end) in fault_times:
            if t_start <= t < t_end:
                return True
        return False
    
    # 仿真主循环
    for idx in range(num_samples):
        t = idx * deltt
        
        # 确定当前系统状态
        # ps=0: 正常状态, ps=1: 故障状态, ps=2: 故障后恢复
        if is_in_fault(t):
            ps = 1  # 故障中
        else:
            ps = 0  # 正常状态（包括故障后恢复）
        
        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]
        
        # 更新状态
        X = rk4_step(X, t, deltt, Ybusm)
        X_true[:, idx] = X
        
        # 计算测量值
        # 发电机端电压（复数形式）
        Vg = E_abs * np.exp(1j * X[:n])
        
        # 扩展到所有母线 (RVm: s x n, Vg: n)
        V_bus = RVm @ Vg
        
        # 计算电流和功率
        I_bus = Ybusm @ V_bus
        S = np.conj(I_bus) * V_bus
        
        # 发电机功率（只取发电机母线）
        PG = np.real(S[gen_bus])
        QG = np.imag(S[gen_bus])
        
        # 所有母线电压
        V_mag = np.abs(V_bus)
        V_angle = np.angle(V_bus)
        
        # 组合测量数据
        measurements[0:3, idx] = PG  # PG1, PG2, PG3
        measurements[3:6, idx] = QG  # QG1, QG2, QG3
        measurements[6:15, idx] = V_mag  # V1-V9
        measurements[15:24, idx] = V_angle  # angle1-angle9
    
    print(f"仿真完成！生成 {num_samples} 个采样点")
    print(f"  总仿真时间: {total_time}秒 ({total_time/60:.1f}分钟)")
    print(f"  采样频率: {fs} Hz")
    print(f"  故障配置:")
    for i, (start, end) in enumerate(fault_times):
        print(f"    故障{i+1}: {start:.1f}秒 - {end:.1f}秒 (持续{end-start:.2f}秒)")
    
    return X_true, measurements, t_points