import matplotlib.pyplot as plt
import numpy as np

def plot_results(X_true, X_est, RMSE_cov, RMSE_actual, t_points, fs, n, fault_times=[(5.0, 5.3), (15.0, 15.3)]):
    """
    绘制UKF估计结果（支持多故障场景）
    
    参数:
        X_true: 真实状态
        X_est: 估计状态
        RMSE_cov: 基于协方差的RMSE
        RMSE_actual: 基于真实值的RMSE
        t_points: 时间点数组
        fs: 采样频率
        n: 发电机数量
        fault_times: 故障时间配置列表，格式: [(t1_start, t1_end), (t2_start, t2_end), ...]
    """
    t = t_points / fs
    
    # 创建图形
    fig, axes = plt.subplots(nrows=2, ncols=1, figsize=(16, 10))
    
    # 绘制转子角度
    axes[0].set_title('Generator Rotor Angles', fontsize=14)
    axes[0].set_ylabel('Angle (rad)', fontsize=12)
    for i in range(n):
        if X_true is not None:
            axes[0].plot(t, X_true[i, :], label=f'$\\delta_{i+1}$ True', linestyle='-', linewidth=2)
        axes[0].plot(t, X_est[i, :], label=f'$\\delta_{i+1}$ Estimated', linestyle='--', linewidth=1.5)
    
    # 标记所有故障区域
    colors = ['red', 'orange', 'yellow', 'purple', 'cyan']
    for i, (t_start, t_end) in enumerate(fault_times):
        color = colors[i % len(colors)]
        axes[0].axvspan(t_start, t_end, color=color, alpha=0.15, label=f'Fault {i+1}')
        axes[0].axvline(x=t_start, color=color, linestyle=':', linewidth=2)
        axes[0].axvline(x=t_end, color=color, linestyle=':', linewidth=2)
    
    axes[0].legend(fontsize=10)
    axes[0].grid(True, alpha=0.3)
    
    # 绘制转速
    axes[1].set_title('Generator Speeds', fontsize=14)
    axes[1].set_xlabel('Time (s)', fontsize=12)
    axes[1].set_ylabel('Speed (rad/s)', fontsize=12)
    for i in range(n):
        if X_true is not None:
            axes[1].plot(t, X_true[n+i, :], label=f'$\\omega_{i+1}$ True', linestyle='-', linewidth=2)
        axes[1].plot(t, X_est[n+i, :], label=f'$\\omega_{i+1}$ Estimated', linestyle='--', linewidth=1.5)
    
    # 标记所有故障区域
    for i, (t_start, t_end) in enumerate(fault_times):
        color = colors[i % len(colors)]
        axes[1].axvspan(t_start, t_end, color=color, alpha=0.15)
        axes[1].axvline(x=t_start, color=color, linestyle=':', linewidth=2)
        axes[1].axvline(x=t_end, color=color, linestyle=':', linewidth=2)
    
    axes[1].legend(fontsize=10)
    axes[1].grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # 如果有实际RMSE，绘制RMSE曲线
    if RMSE_actual is not None:
        fig2, ax = plt.subplots(figsize=(16, 5))
        ax.set_title('RMSE Comparison', fontsize=14)
        ax.set_xlabel('Time (s)', fontsize=12)
        ax.set_ylabel('RMSE', fontsize=12)
        ax.plot(t, RMSE_cov, label='Covariance-based RMSE', linestyle='--', linewidth=1.5)
        ax.plot(t, RMSE_actual, label='Actual RMSE', linestyle='-', linewidth=1.5)
        
        # 标记所有故障区域
        for i, (t_start, t_end) in enumerate(fault_times):
            color = colors[i % len(colors)]
            ax.axvspan(t_start, t_end, color=color, alpha=0.15)
            ax.axvline(x=t_start, color=color, linestyle=':', linewidth=2)
            ax.axvline(x=t_end, color=color, linestyle=':', linewidth=2)
        
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        plt.tight_layout()
    
    plt.show()