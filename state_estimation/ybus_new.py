import numpy as np

def ybus_new(net):
    """
    计算导纳矩阵（考虑故障状态）
    
    参数:
        net: pandapower网络对象
    
    返回:
        YBUS: 导纳矩阵 (n x n x 3)
              YBUS[:,:,0] - 故障前
              YBUS[:,:,1] - 故障中（母线8短路）
              YBUS[:,:,2] - 故障后（线路断开）
        RV: 状态矩阵 (n_bus x n_gen x 3) — Kron投影 V_bus = RV @ Vg
    """
    import pandapower as pp
    
    n = len(net.bus)  # 母线数
    s = n  # 状态数
    
    # 计算正常状态的导纳矩阵
    pp.runpp(net)
    Ybus_normal = net._ppc['internal']['Ybus'].toarray()
    
    # 创建故障状态的导纳矩阵
    Ybus_fault = Ybus_normal.copy()
    Ybus_post_fault = Ybus_normal.copy()
    
    # 故障设置：母线8短路（索引7，因为Python从0开始）
    fault_bus = 7  # Bus 8
    
    # 故障中：母线8短路（导纳无穷大）
    Ybus_fault[fault_bus, :] = 0
    Ybus_fault[:, fault_bus] = 0
    Ybus_fault[fault_bus, fault_bus] = 1e10
    
    # 故障后：断开母线8-9之间的线路
    # 需要找到连接母线8和母线9的线路并移除
    for i, line in net.line.iterrows():
        if (line['from_bus'] == fault_bus and line['to_bus'] == 8) or \
           (line['from_bus'] == 8 and line['to_bus'] == fault_bus):
            # 计算该线路的导纳
            r = line['r_ohm_per_km'] * line['length_km']
            x = line['x_ohm_per_km'] * line['length_km']
            y_line = 1 / (r + 1j * x)
            
            # 从导纳矩阵中移除
            Ybus_post_fault[int(line['from_bus']), int(line['to_bus'])] += y_line
            Ybus_post_fault[int(line['to_bus']), int(line['from_bus'])] += y_line
            Ybus_post_fault[int(line['from_bus']), int(line['from_bus'])] -= y_line
            Ybus_post_fault[int(line['to_bus']), int(line['to_bus'])] -= y_line
    
    # 组合导纳矩阵
    YBUS = np.zeros((n, n, 3), dtype=np.complex128)
    YBUS[:, :, 0] = Ybus_normal
    YBUS[:, :, 1] = Ybus_fault
    YBUS[:, :, 2] = Ybus_post_fault
    
    # 状态矩阵RV — Kron 投影: V_bus = RV[:,:,ps] @ Vg
    # RV 形状: (n_bus, n_gen, 3) = (9, 3, 3)
    n_gen = 3
    RV = np.zeros((s, n_gen, 3), dtype=np.complex128)
    for i in range(3):
        g = slice(0, n_gen)
        l = slice(n_gen, n)
        Ylg_mat = YBUS[l, g, i]
        Yll_mat = YBUS[l, l, i]
        M_kron = -np.linalg.solve(Yll_mat, Ylg_mat)
        RV[:, :, i] = np.vstack([np.eye(n_gen, dtype=np.complex128), M_kron])
    
    return YBUS, RV
