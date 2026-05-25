import numpy as np

def dynamic_system(t, x, M, D, Ybusm, Vo, Pm, NumG):
    """
    电力系统动态方程（转子运动方程）
    
    参数:
        t: 时间（未使用，但保留以兼容ODE求解器接口）
        x: 状态向量 [δ₁, δ₂, δ₃, ω₁, ω₂, ω₃]
        M: 惯性常数向量
        D: 阻尼系数向量
        Ybusm: 导纳矩阵
        Vo: 电动势幅值向量
        Pm: 机械功率向量
        NumG: 发电机数量
    
    返回:
        dx: 状态导数向量
    """
    # 计算发电机端电压（复数形式）
    Vg = Vo * np.exp(1j * x[:NumG])
    
    # 计算电流和功率
    Ibus = Ybusm @ Vg
    S = np.conj(Ibus) * Vg
    Pe = np.real(S)
    
    # 计算状态导数
    dx = np.zeros(2 * NumG)
    dx[:NumG] = x[NumG:]  # dδ/dt = ω
    dx[NumG:] = (Pm - Pe) / M - D * x[NumG:] / M  # dω/dt = (Pm - Pe)/M - Dω/M
    
    return dx
