import numpy as np

def RK4(n, deltt, E_abs, ns, X_sigma, PM, M, D, Ybusm):
    """
    四阶Runge-Kutta积分方法（用于UKF中的状态传播）
    
    参数:
        n: 发电机数量
        deltt: 时间步长
        E_abs: 电动势幅值向量
        ns: 状态维度（2*n）
        X_sigma: Sigma点矩阵（每列是一个sigma点）
        PM: 机械功率向量
        M: 惯性常数向量
        D: 阻尼系数向量
        Ybusm: 导纳矩阵
    
    返回:
        xbreve: 传播后的sigma点矩阵
    """
    num_sigma = X_sigma.shape[1]
    xbreve = np.zeros((ns, num_sigma))
    
    for i in range(num_sigma):
        x = X_sigma[:, i]
        
        # RK4积分
        k1 = deltt * dynamic_system(0, x, M, D, Ybusm, E_abs, PM, n)
        k2 = deltt * dynamic_system(0, x + k1/2, M, D, Ybusm, E_abs, PM, n)
        k3 = deltt * dynamic_system(0, x + k2/2, M, D, Ybusm, E_abs, PM, n)
        k4 = deltt * dynamic_system(0, x + k3, M, D, Ybusm, E_abs, PM, n)
        
        xbreve[:, i] = x + (k1 + 2*k2 + 2*k3 + k4) / 6
    
    return xbreve

# 需要导入dynamic_system
from dynamic_system import dynamic_system
