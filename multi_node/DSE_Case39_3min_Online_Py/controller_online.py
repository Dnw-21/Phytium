"""
controller_online.py — Online UKF for 39-Bus 10-Generator
==========================================================
Reads measurements ONE AT A TIME from stdin or file,
runs a single UKF step per measurement, outputs estimate immediately.

Usage:
  python controller_online.py                          (reads from stdin)
  python controller_online.py measurements.txt         (reads from file)
  tail -f measurements.txt | python controller_online.py  (streaming)
  python controller_online.py measurements.txt | python plot_online.py

Requires: numpy, scipy, RK4.py, ukf_core_39.py
==========================================================
"""
import sys
import numpy as np
from scipy.io import loadmat

sys.path.insert(0, '.')
from ukf_core_39 import UKFState, ukf_init, ukf_step


def main():
    # ---- Load system params (once) ----
    data = loadmat('system_params.mat')
    n  = int(data['n'][0,0]);  s  = int(data['s'][0,0])
    fs = int(data['fs'][0,0]); ns = 2*n; nm = 2*n+2*s
    t_SW = float(data['t_SW'][0,0]); t_FC = float(data['t_FC'][0,0])

    sig = float(data['sig'][0,0])
    sp = {
        'YBUS': data['YBUS'], 'RV': data['RV'],
        'E_abs': data['E_abs'].flatten(), 'PM': data['PM'].flatten(),
        'M': data['M'].flatten(), 'D': data['D'].flatten(),
        'n': n, 's': s, 'ns': ns, 'nm': nm, 'fs': fs,
        'deltt': 1.0/fs, 't_SW': t_SW, 't_FC': t_FC,
        'P': sig**2 * np.eye(ns), 'Q_mat': sig**2 * np.eye(ns),
        'R_meas': sig**2 * np.eye(nm), 'W': np.ones((2*ns,1))/(2*ns),
        'X_hat': data['X_0'].flatten(),
    }

    # ---- Open input ----
    if len(sys.argv) > 1:
        inp = open(sys.argv[1])
        sys.stderr.write(f'[Init] Reading from file: {sys.argv[1]}\n')
    else:
        inp = sys.stdin
        sys.stderr.write('[Init] Reading from stdin. Ctrl+D to end.\n')

    sys.stderr.write(f'[Init] 10-gen 39-bus, ns={ns} nm={nm} fs={fs}Hz\n')
    sys.stderr.write(f'[Init] Fault: {t_SW}s ~ {t_FC}s\n')

    # ---- Init UKF state ----
    st = UKFState()
    ukf_init(sp, st)
    sys.stderr.write('[Init] UKF ready. Processing measurements...\n\n')

    # ---- Print header ----
    hdr = 'time' + ''.join(f',delta{i+1}' for i in range(n)) + \
          ''.join(f',omega{i+1}' for i in range(n)) + ',RMSE'
    print(f'# {hdr}  fault_tSW={t_SW:.4f}  fault_tFC={t_FC:.4f}')
    sys.stdout.flush()

    # ---- Process one measurement at a time ----
    count = 0
    for line in inp:
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('time') or line.startswith('step'):
            continue

        parts = line.split(',')
        # Parse: timestamp, PG1..PG10, QG1..QG10, Vreal1..Vreal39, Vimag1..Vimag39
        z = np.zeros(nm)
        k_time = count / fs
        try:
            ts = float(parts[0])
            if ts < 10000: k_time = ts; start = 1
            else: start = 0
        except ValueError: start = 0
        for j in range(nm): z[j] = float(parts[start + j])

        # Single UKF step
        x_out, rmse = ukf_step(st, z, k_time)

        # Output estimate
        vals = ','.join(f'{x_out[i]:.8f}' for i in range(ns))
        print(f'{k_time:.6f},{vals},{rmse:.8f}')
        sys.stdout.flush()

        count += 1
        if count % 50000 == 0:
            sys.stderr.write(f'[Stream] {count} measurements (t={k_time:.1f}s)\n')

    sys.stderr.write(f'\n[Done] {count} measurements processed.\n')
    if len(sys.argv) > 1:
        inp.close()


if __name__ == '__main__':
    main()
