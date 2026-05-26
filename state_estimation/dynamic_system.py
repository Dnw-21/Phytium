import numpy as np

def dynamic_system(t, x, M, D, Ybusm, Vo, Pm, NumG):
    u"""
    电力系统动态方程（转子运动方程）— 支持 n_bus×n_bus 完整导纳矩阵

    当 Ybusm 边长 > NumG 时自动做 Kron 简约提取发电机有功 Pe，
    兼容旧版 3x3 简约阵。

    返回: dx (2*NumG,)  状态导数向量
    """
    Vg = Vo * np.exp(1j * x[:NumG])
    n_bus = Ybusm.shape[0]

    if n_bus > NumG:
        g = slice(0, NumG)
        l = slice(NumG, n_bus)
        Yll = Ybusm[l, l]
        Ylg = Ybusm[l, g]
        Vl = -np.linalg.solve(Yll, Ylg @ Vg)
        I_bus = Ybusm @ np.concatenate([Vg, Vl])
        Pe = np.real(np.conj(I_bus[g]) * Vg)
    else:
        Ibus = Ybusm @ Vg
        Pe = np.real(np.conj(Ibus) * Vg)

    dx = np.zeros(2 * NumG)
    dx[:NumG] = x[NumG:]
    dx[NumG:] = (Pm - Pe) / M - D * x[NumG:] / M
    return dx
