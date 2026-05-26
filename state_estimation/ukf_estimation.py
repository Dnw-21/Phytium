import numpy as np
from scipy.linalg import cholesky

def ukf_estimation(YBUS, RV, E_abs, PM, M, D, n, s, fs, fault_times, total_time, measurements, X_true=None):
    """
    Unscented Kalman Filter 状态估计（支持多故障场景）
    
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
        measurements: 测量数据 (2n+2s x num_samples)
        X_true: 真实状态（可选，用于计算RMSE）
    
    返回:
        X_est: 估计状态
        RMSE_cov: 基于协方差的RMSE
        RMSE_actual: 基于真实值的RMSE（如果提供了X_true）
    """
    from RK4 import RK4
    
    num_samples = measurements.shape[1]
    deltt = 1.0 / fs
    
    # 状态维度和测量维度
    ns = 2 * n
    nm = 2 * n + 2 * s
    
    # 初始化协方差矩阵
    sig = 1e-2
    P = (sig ** 2) * np.eye(ns)
    Q = (sig ** 2) * np.eye(ns)
    
    # 初始状态估计
    X_hat = np.zeros(ns)
    X_hat[:n] = np.angle(E_abs)  # 初始角度
    X_hat[n:] = 0  # 初始转速
    
    # 存储结果
    X_est = []
    RMSE_cov = []
    RMSE_actual = []
    
    # UKF权重
    W = np.ones(2 * ns) / (2 * ns)
    
    # 测量噪声协方差
    R_meas = (sig ** 2) * np.eye(nm)
    
    # 判断当前时间是否在故障期间
    def is_in_fault(t):
        for (t_start, t_end) in fault_times:
            if t_start <= t < t_end:
                return True
        return False
    
    for idx in range(num_samples):
        k = idx / fs
        
        # 确定当前系统状态（故障前/故障中/故障后）
        if is_in_fault(k):
            ps = 1  # 故障中
        else:
            ps = 0  # 正常状态（包括故障后恢复）
        
        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]
        
        # === UKF预测步骤 ===
        # 生成sigma点
        try:
            root = cholesky(n * P)
        except np.linalg.LinAlgError:
            root = cholesky(n * P + 1e-6 * np.eye(ns))
        
        X_tilde = np.column_stack([root, -root])
        X_sigma = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns)) + X_tilde
        
        # 通过动态方程传播sigma点
        xbreve = RK4(n, deltt, E_abs, ns, X_sigma, PM, M, D, Ybusm)
        
        # 计算预测状态
        X_hat = xbreve @ W
        
        # 计算预测协方差
        x_hat_rep = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns))
        P = (1 / (2 * ns)) * (xbreve - x_hat_rep) @ (xbreve - x_hat_rep).T + Q
        
        # === UKF更新步骤 ===
        # 再次生成sigma点（使用更新后的协方差）
        try:
            root1 = cholesky(n * P)
        except np.linalg.LinAlgError:
            root1 = cholesky(n * P + 1e-6 * np.eye(ns))
        
        X_tilde1 = np.column_stack([root1, -root1])
        X_sigma = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns)) + X_tilde1
        
        # 计算预测测量
        E11 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * X_sigma[:n, :])
        I11 = Ybusm @ E11
        PG11 = np.real(E11 * np.conj(I11))
        QG11 = np.imag(E11 * np.conj(I11))
        Vmag11 = np.abs(RVm @ E11)
        Vangle11 = np.angle(RVm @ E11)
        zbreve = np.vstack([PG11, QG11, Vmag11, Vangle11])
        
        # 计算测量均值和协方差
        z_hat = zbreve @ W
        z_hat_rep = np.tile(z_hat.reshape(-1, 1), (1, 2 * ns))
        P_zz = (1 / (2 * ns)) * (zbreve - z_hat_rep) @ (zbreve - z_hat_rep).T + R_meas
        P_xz = (1 / (2 * ns)) * (xbreve - x_hat_rep) @ (zbreve - z_hat_rep).T
        
        # 计算卡尔曼增益
        try:
            K = P_xz @ np.linalg.inv(P_zz)
        except np.linalg.LinAlgError:
            K = P_xz @ np.linalg.pinv(P_zz)
        
        # 更新状态估计
        z = measurements[:, idx]
        X_hat = X_hat + K @ (z - z_hat)
        
        # 更新协方差
        P = P - K @ P_zz @ K.T
        
        # 保存结果
        X_est.append(X_hat)
        RMSE_cov.append(np.sqrt(np.trace(P)))
        
        # 计算实际RMSE（如果提供了真实值）
        if X_true is not None:
            X_true_col = X_true[:, idx]
            rmse_actual = np.sqrt(np.mean((X_hat - X_true_col) ** 2))
            RMSE_actual.append(rmse_actual)
    
    X_est = np.array(X_est).T
    RMSE_cov = np.array(RMSE_cov)
    RMSE_actual = np.array(RMSE_actual) if RMSE_actual else None
    
    return X_est, RMSE_cov, RMSE_actual