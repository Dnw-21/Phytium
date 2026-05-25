import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

def plot_results(X_true, X_est, RMSE_cov, RMSE_actual, t_points, fs, n, num_normal, num_fault):
    t = t_points / fs

    fig, axes = plt.subplots(nrows=2, ncols=1, figsize=(12, 8))

    axes[0].set_title('Generator Rotor Angles')
    axes[0].set_ylabel('Angle (rad)')
    for i in range(n):
        if X_true is not None:
            axes[0].plot(t, X_true[i, :], label=f'$\\delta_{{{i+1}}}$ True', linestyle='-', linewidth=2)
        axes[0].plot(t, X_est[i, :], label=f'$\\delta_{{{i+1}}}$ Estimated', linestyle='--', linewidth=1.5)

    t_SW = num_normal / fs
    t_FC = (num_normal + num_fault) / fs
    axes[0].axvspan(t_SW, t_FC, color='red', alpha=0.1, label='Fault Period')
    axes[0].legend()
    axes[0].grid(True)

    axes[1].set_title('Generator Speeds')
    axes[1].set_xlabel('Time (s)')
    axes[1].set_ylabel('Speed (rad/s)')
    for i in range(n):
        if X_true is not None:
            axes[1].plot(t, X_true[n+i, :], label=f'$\\omega_{{{i+1}}}$ True', linestyle='-', linewidth=2)
        axes[1].plot(t, X_est[n+i, :], label=f'$\\omega_{{{i+1}}}$ Estimated', linestyle='--', linewidth=1.5)

    axes[1].axvspan(t_SW, t_FC, color='red', alpha=0.1)
    axes[1].legend()
    axes[1].grid(True)

    plt.tight_layout()
    plt.savefig('ukf_rotor_angle_speed.png', dpi=150)
    plt.close()
    print('已保存: ukf_rotor_angle_speed.png')

    if RMSE_actual is not None:
        fig2, ax = plt.subplots(figsize=(12, 4))
        ax.set_title('RMSE Comparison')
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('RMSE')
        ax.plot(t, RMSE_cov, label='Covariance-based RMSE', linestyle='--')
        ax.plot(t, RMSE_actual, label='Actual RMSE', linestyle='-')
        ax.axvspan(t_SW, t_FC, color='red', alpha=0.1)
        ax.legend()
        ax.grid(True)
        plt.tight_layout()
        plt.savefig('ukf_rmse.png', dpi=150)
        plt.close()
        print('已保存: ukf_rmse.png')
