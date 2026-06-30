"""
Plotting functions for UKF results.
IEEE 5-Bus Overbye System (2 Generators).

Generates:
  - Per-generator plots (rotor angle + speed, actual vs. estimated)
  - All-generators summary subplot
  - RMSE curve
"""

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def plot_results_5(t, X_true, X_est, RMSE, n, t_SW, t_FC):
    """
    Generate and save all UKF result plots.

    Parameters:
        t:      time vector [num_samples]
        X_true: true states [ns x num_samples]
        X_est:  estimated states [ns x num_samples]
        RMSE:   error trace [num_samples]
        n:      number of generators
        t_SW:   fault start time
        t_FC:   fault clear time
    """
    plt.rcParams['font.size'] = 12

    # ─── Per-generator plots ───
    for i in range(n):
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

        # Rotor angle
        ax1.plot(t, X_true[i, :], 'b', linewidth=1.5, label=f'Angle_{i + 1} Actual')
        ax1.plot(t, X_est[i, :], 'r--', linewidth=2, label=f'Angle_{i + 1} UKF')
        yl = ax1.get_ylim()
        ax1.fill_between([t_SW, t_FC], yl[0], yl[1], color='r', alpha=0.15)
        ax1.grid(True)
        ax1.set_ylabel(f'Angle_{i + 1} (rad)', fontsize=12)
        ax1.set_xlabel('Time (s)', fontsize=15)
        ax1.set_title(f'Actual Vs Estimated $\\delta_{{{i + 1}}}$ with UKF (5-bus Overbye 3min)',
                      fontsize=12)
        ax1.legend(loc='upper right')

        # Rotor speed
        ax2.plot(t, X_true[n + i, :], 'b', linewidth=1.5, label=f'Speed_{i + 1} Actual')
        ax2.plot(t, X_est[n + i, :], 'r--', linewidth=2, label=f'Speed_{i + 1} UKF')
        yl = ax2.get_ylim()
        ax2.fill_between([t_SW, t_FC], yl[0], yl[1], color='r', alpha=0.15)
        ax2.grid(True)
        ax2.set_ylabel(f'Speed_{i + 1} (rad/s)', fontsize=12)
        ax2.set_xlabel('Time (s)', fontsize=15)
        ax2.set_title(f'Actual Vs Estimated $\\omega_{{{i + 1}}}$ with UKF (5-bus Overbye 3min)',
                      fontsize=12)
        ax2.legend(loc='upper right')

        fig.tight_layout()
        fig.savefig(f'ukf_results_generator{i + 1}_5.png', dpi=150)
        plt.close(fig)
        print(f'  Saved ukf_results_generator{i + 1}_5.png')

    # ─── All generators summary plot ───
    cols = min(2, n)
    rows = n  # 2 generators × (angle + speed) = 4 rows, but we do 2 cols
    fig2, axes = plt.subplots(n * 2, cols, figsize=(cols * 5, n * 6))
    axes = np.atleast_2d(axes)

    for i in range(n):
        c = 0  # actual column
        r_ang = i * 2      # angle row
        r_spd = i * 2 + 1  # speed row

        # Rotor angle
        ax_ang = axes[r_ang, c]
        ax_ang.plot(t, X_true[i, :], 'b', linewidth=1.0)
        ax_ang.plot(t, X_est[i, :], 'r--', linewidth=1.5)
        yl = ax_ang.get_ylim()
        ax_ang.fill_between([t_SW, t_FC], yl[0], yl[1], color='r', alpha=0.1)
        ax_ang.grid(True, alpha=0.3)
        ax_ang.set_ylabel(f'Angle_{i + 1} (rad)', fontsize=10)
        ax_ang.set_title(f'Rotor Angle Gen {i + 1}', fontsize=10)
        ax_ang.legend(['Actual', 'UKF'], loc='upper right', fontsize=8)

        # Rotor speed
        ax_spd = axes[r_spd, c]
        ax_spd.plot(t, X_true[n + i, :], 'b', linewidth=1.0)
        ax_spd.plot(t, X_est[n + i, :], 'r--', linewidth=1.5)
        yl = ax_spd.get_ylim()
        ax_spd.fill_between([t_SW, t_FC], yl[0], yl[1], color='r', alpha=0.1)
        ax_spd.grid(True, alpha=0.3)
        ax_spd.set_ylabel(f'Speed_{i + 1} (rad/s)', fontsize=10)
        ax_spd.set_title(f'Rotor Speed Gen {i + 1}', fontsize=10)
        ax_spd.legend(['Actual', 'UKF'], loc='upper right', fontsize=8)

    # Hide unused subplots (only for 2nd column if present)
    if cols > 1:
        for r in range(n * 2):
            axes[r, 1].set_visible(False)

    fig2.tight_layout()
    fig2.savefig('ukf_results_all_generators_5.png', dpi=150)
    plt.close(fig2)
    print('  Saved ukf_results_all_generators_5.png')

    # ─── RMSE plot ───
    fig3, ax3 = plt.subplots(figsize=(12, 5))
    ax3.plot(t, RMSE, 'g', linewidth=2)
    yl = ax3.get_ylim()
    ax3.fill_between([t_SW, t_FC], yl[0], yl[1], color='r', alpha=0.15, label='Fault')
    ax3.grid(True)
    ax3.set_ylabel('RMSE', fontsize=12)
    ax3.set_xlabel('Time (s)', fontsize=15)
    ax3.set_title('Estimation Error Covariance (RMSE) - 5-bus Overbye 3min', fontsize=12)
    ax3.legend()

    fig3.tight_layout()
    fig3.savefig('ukf_results_rmse_5.png', dpi=150)
    plt.close(fig3)
    print('  Saved ukf_results_rmse_5.png')

    print('\nAll plots saved successfully.')
