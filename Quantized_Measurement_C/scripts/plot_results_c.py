"""
plot_results_c.py — Plot UKF results from C output CSVs.
"""
import numpy as np, os
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt

script_dir = os.path.dirname(os.path.abspath(__file__))
data_dir = os.path.join(os.path.dirname(script_dir), 'data')
out_dir = os.path.join(os.path.dirname(script_dir), 'output')
os.makedirs(out_dir, exist_ok=True)

est = np.loadtxt(os.path.join(data_dir, 'ukf_estimation_output.csv'), delimiter=',', skiprows=1)
t = est[:, 0]
X_est_delta = est[:, 1:4]
X_est_omega = est[:, 4:7]
RMSE = est[:, 7]

n, deg, t_SW, t_FC = 3, 180/np.pi, 0.060, 0.080

try:
    comp = np.loadtxt(os.path.join(data_dir, 'ukf_comparison_output.csv'), delimiter=',', skiprows=1)
    X_true = comp[:, 1:7].T
    has_true = True
except:
    has_true = False
    X_true = np.vstack([X_est_delta.T, X_est_omega.T])

for i in range(n):
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    if has_true: ax1.plot(t, X_true[i,:]*deg, 'k-', lw=1.5, label='True')
    ax1.plot(t, X_est_delta[:,i]*deg, 'r--', lw=1.5, label='UKF')
    yl = ax1.get_ylim(); ax1.fill_between([t_SW,t_FC], yl[0], yl[1], color='r', alpha=0.12)
    ax1.grid(True, alpha=0.3); ax1.set_ylabel(r'$\delta_{%d}$ (deg)'%(i+1)); ax1.set_xlabel('Time (s)')
    ax1.set_title(f'Generator {i+1} - Rotor Angle (C UKF)'); ax1.legend(loc='best')

    if has_true: ax2.plot(t, X_true[n+i,:], 'k-', lw=1.5, label='True')
    ax2.plot(t, X_est_omega[:,i], 'r--', lw=1.5, label='UKF')
    yl = ax2.get_ylim(); ax2.fill_between([t_SW,t_FC], yl[0], yl[1], color='r', alpha=0.12)
    ax2.grid(True, alpha=0.3); ax2.set_ylabel(r'$\omega_{%d}$ (rad/s)'%(i+1)); ax2.set_xlabel('Time (s)')
    ax2.set_title(f'Generator {i+1} - Rotor Speed (C UKF)'); ax2.legend(loc='best')

    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f'ukf_results_generator{i+1}_c.png'), dpi=150)
    plt.close(fig)
    print(f'Saved generator {i+1}')

fig2, axes = plt.subplots(2, n, figsize=(5*n, 8))
for i in range(n):
    for row, label, data_true, data_est, ylabel in [
        (0, 'Angle', X_true[i,:]*deg, X_est_delta[:,i]*deg, r'$\delta_{%d}$ (deg)'%(i+1)),
        (1, 'Speed', X_true[n+i,:], X_est_omega[:,i], r'$\omega_{%d}$ (rad/s)'%(i+1))]:
        ax = axes[row, i]
        if has_true: ax.plot(t, data_true, 'k-', lw=1.0, label='True')
        ax.plot(t, data_est, 'r--', lw=1.2, label='UKF')
        yl = ax.get_ylim(); ax.fill_between([t_SW,t_FC], yl[0], yl[1], color='r', alpha=0.08)
        ax.grid(True, alpha=0.3); ax.set_ylabel(ylabel, fontsize=10)
        ax.set_title(f'Gen {i+1} {label}', fontsize=10)
        ax.legend(['True','UKF'] if has_true else ['UKF'], loc='best', fontsize=8)
fig2.tight_layout()
fig2.savefig(os.path.join(out_dir, 'ukf_results_all_generators_c.png'), dpi=150)
plt.close(fig2)
print('Saved all generators')

fig3, ax3 = plt.subplots(figsize=(10, 4))
ax3.plot(t, RMSE, 'g-', lw=1.5)
yl = ax3.get_ylim(); ax3.fill_between([t_SW,t_FC], yl[0], yl[1], color='r', alpha=0.12, label='Fault')
ax3.grid(True, alpha=0.3); ax3.set_ylabel('RMSE'); ax3.set_xlabel('Time (s)')
ax3.set_title('RMSE = sqrt(trace(P)) - C UKF'); ax3.legend()
fig3.tight_layout()
fig3.savefig(os.path.join(out_dir, 'ukf_results_rmse_c.png'), dpi=150)
plt.close(fig3)
print('Saved RMSE')
print('All C-version plots saved.')
