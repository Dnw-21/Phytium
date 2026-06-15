"""
plot_online.py
============================================================
Plot UKF estimates from controller_online stdout.
Reads CSV from stdin, generates PNG plots.

Usage:
  ./controller_online < measurements.txt | python plot_online.py
  ./controller_online < measurements.txt | tee estimates.csv | python plot_online.py

Output:
  ukf_online_generator1.png ~ generator3.png
  ukf_online_all_generators.png
  ukf_online_rmse.png
============================================================
"""
import sys, os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ---- Read all lines from stdin ----
all_lines = [l.strip() for l in sys.stdin if l.strip()]
# Parse fault times from header comment, or auto-detect
t_sw, t_fc = 5.0, 5.3  # defaults (will be overridden)
data_lines = []
for l in all_lines:
    if l.startswith('#'):
        # Format: "# time,...  fault_tSW=X.XXXX  fault_tFC=Y.YYYY"
        import re
        m_sw = re.search(r'fault_tSW=([\d.]+)', l)
        m_fc = re.search(r'fault_tFC=([\d.]+)', l)
        if m_sw: t_sw = float(m_sw.group(1))
        if m_fc: t_fc = float(m_fc.group(1))
    else:
        data_lines.append(l)

if not data_lines:
    print("ERROR: no data received from stdin")
    sys.exit(1)

data = np.array([[float(x) for x in l.split(',')] for l in data_lines])
t = data[:, 0]
# Auto-detect: 1(time) + n_gen*delta + n_gen*omega + 1(RMSE) = 2*n_gen+2 columns
n_gen = (data.shape[1] - 2) // 2
delta = data[:, 1:1+n_gen]
omega = data[:, 1+n_gen:1+2*n_gen]
rmse  = data[:, 1+2*n_gen]

deg = 180.0 / np.pi

print(f'Loaded {len(t)} points, {n_gen} gens, t=[{t[0]:.4f}, {t[-1]:.4f}]s, fault=[{t_sw:.4f}, {t_fc:.4f}]s')

# ---- Per-generator ----
for i in range(n_gen):
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 7))

    ax1.plot(t, delta[:, i] * deg, 'b-', lw=1.0, label=f'UKF $\\delta_{i+1}$')
    ax1.fill_between([t_sw, t_fc], ax1.get_ylim()[0], ax1.get_ylim()[1],
                     color='r', alpha=0.12, label='Fault')
    ax1.grid(True, alpha=0.3); ax1.set_ylabel(f'$\\delta_{i+1}$ (deg)')
    ax1.set_title(f'Generator {i+1} — Rotor Angle (Online UKF)')
    ax1.legend(loc='best')

    ax2.plot(t, omega[:, i], 'r-', lw=1.0, label=f'UKF $\\omega_{i+1}$')
    ax2.fill_between([t_sw, t_fc], ax2.get_ylim()[0], ax2.get_ylim()[1],
                     color='r', alpha=0.12, label='Fault')
    ax2.grid(True, alpha=0.3); ax2.set_ylabel(f'$\\omega_{i+1}$ (rad/s)')
    ax2.set_xlabel('Time (s)')
    ax2.set_title(f'Generator {i+1} — Rotor Speed (Online UKF)')
    ax2.legend(loc='best')

    fig.tight_layout()
    fname = f'ukf_online_generator{i+1}.png'
    fig.savefig(fname, dpi=150)
    plt.close(fig)
    print(f'Saved {fname}')

# ---- All generators ----
fig2, axes = plt.subplots(2, n_gen, figsize=(5*n_gen, 8))
for i in range(n_gen):
    axes[0, i].plot(t, delta[:, i]*deg, 'b-', lw=0.8)
    axes[0, i].fill_between([t_sw, t_fc], axes[0, i].get_ylim()[0],
                            axes[0, i].get_ylim()[1], color='r', alpha=0.08)
    axes[0, i].grid(True, alpha=0.3); axes[0, i].set_title(f'Gen {i+1} Angle')
    axes[0, i].set_ylabel(f'$\\delta_{i+1}$ (deg)')

    axes[1, i].plot(t, omega[:, i], 'r-', lw=0.8)
    axes[1, i].fill_between([t_sw, t_fc], axes[1, i].get_ylim()[0],
                            axes[1, i].get_ylim()[1], color='r', alpha=0.08)
    axes[1, i].grid(True, alpha=0.3); axes[1, i].set_title(f'Gen {i+1} Speed')
    axes[1, i].set_ylabel(f'$\\omega_{i+1}$ (rad/s)')
    axes[1, i].set_xlabel('Time (s)')
fig2.tight_layout(); fig2.savefig('ukf_online_all_generators.png', dpi=150)
plt.close(fig2); print('Saved ukf_online_all_generators.png')

# ---- RMSE ----
fig3, ax = plt.subplots(figsize=(12, 4))
ax.plot(t, rmse, 'g-', lw=1.0)
ax.fill_between([t_sw, t_fc], ax.get_ylim()[0], ax.get_ylim()[1],
                color='r', alpha=0.12, label='Fault')
ax.grid(True, alpha=0.3); ax.set_ylabel('RMSE'); ax.set_xlabel('Time (s)')
ax.set_title('RMSE = sqrt(trace(P)) — Online UKF')
ax.legend()
fig3.tight_layout(); fig3.savefig('ukf_online_rmse.png', dpi=150)
plt.close(fig3); print('Saved ukf_online_rmse.png')

print('\nAll plots saved.')
