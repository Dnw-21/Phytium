import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def plot_results(t, X_true, X_est, RMSE, n, fault1_start, fault1_end, fault2_start, fault2_end):
    fig1, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

    ax1.plot(t, X_true[0, :], 'b', linewidth=1.5)
    ax1.plot(t, X_est[0, :], 'r--', linewidth=2)
    yl = ax1.get_ylim()
    ax1.fill_between([fault1_start, fault1_end], yl[0], yl[1], color='r', alpha=0.15)
    ax1.fill_between([fault2_start, fault2_end], yl[0], yl[1], color='r', alpha=0.15)
    ax1.grid(True)
    ax1.set_ylabel('Angle_1 (rad)', fontsize=12)
    ax1.set_xlabel('time(s)', fontsize=15)
    ax1.set_title('Actual Vs Estimated $\\delta$ with UKF (3min, Multi-Fault)', fontsize=12)
    ax1.legend(['Actual', 'UKF'], loc='upper left')

    ax2.plot(t, X_true[n, :], 'b', linewidth=1.5)
    ax2.plot(t, X_est[n, :], 'r--', linewidth=2)
    yl = ax2.get_ylim()
    ax2.fill_between([fault1_start, fault1_end], yl[0], yl[1], color='r', alpha=0.15)
    ax2.fill_between([fault2_start, fault2_end], yl[0], yl[1], color='r', alpha=0.15)
    ax2.grid(True)
    ax2.set_ylabel('Speed_1 (rad/s)', fontsize=12)
    ax2.set_xlabel('time(s)', fontsize=15)
    ax2.set_title('Actual Vs Estimated $\\omega$ with UKF (3min, Multi-Fault)', fontsize=12)
    ax2.legend(['Actual', 'UKF'], loc='upper left')

    fig1.tight_layout()
    fig1.savefig('ukf_results_generator1.png', dpi=150)
    print('Saved ukf_results_generator1.png')

    fig2, axes = plt.subplots(2, n, figsize=(14, 9))
    for i in range(n):
        ax_ang = axes[0, i]
        ax_ang.plot(t, X_true[i, :], 'b', linewidth=1.5)
        ax_ang.plot(t, X_est[i, :], 'r--', linewidth=2)
        ax_ang.grid(True)
        ax_ang.set_ylabel(f'Angle_{i+1}', fontsize=12)
        ax_ang.set_title(f'Rotor Angle Generator {i+1}', fontsize=12)
        ax_ang.legend(['Actual', 'UKF'], loc='upper left')

        ax_spd = axes[1, i]
        ax_spd.plot(t, X_true[n+i, :], 'b', linewidth=1.5)
        ax_spd.plot(t, X_est[n+i, :], 'r--', linewidth=2)
        ax_spd.grid(True)
        ax_spd.set_ylabel(f'Speed_{i+1}', fontsize=12)
        ax_spd.set_title(f'Rotor Speed Generator {i+1}', fontsize=12)
        ax_spd.legend(['Actual', 'UKF'], loc='upper left')

    fig2.tight_layout()
    fig2.savefig('ukf_results_all_generators.png', dpi=150)
    print('Saved ukf_results_all_generators.png')

    fig3, ax3 = plt.subplots(figsize=(12, 5))
    ax3.plot(t, RMSE, 'g', linewidth=2)
    yl = ax3.get_ylim()
    ax3.fill_between([fault1_start, fault1_end], yl[0], yl[1], color='r', alpha=0.15)
    ax3.fill_between([fault2_start, fault2_end], yl[0], yl[1], color='r', alpha=0.15)
    ax3.grid(True)
    ax3.set_ylabel('RMSE', fontsize=12)
    ax3.set_xlabel('time(s)', fontsize=15)
    ax3.set_title('Estimation Error Covariance (RMSE)', fontsize=12)

    fig3.tight_layout()
    fig3.savefig('ukf_results_rmse.png', dpi=150)
    print('Saved ukf_results_rmse.png')

    print('All plots saved successfully.')
