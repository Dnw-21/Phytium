"""
Terminal Controller - UKF State Estimation
IEEE 5-Bus Overbye System (2 Generators, 3min)
Single Fault: Bus 2, 5.0s - 5.3s, Line 2-4 removed

Run: python terminal_controller_5.py

Reads:
  - system_params.mat   (system parameters — pre-loaded at controller)
  - measurements.txt    (full measurement data from terminal node)
  - true_states.csv     (optional, for comparison/validation)

Outputs:
  - ukf_results_generator1_5.png ~ ukf_results_generator2_5.png  (per-generator angle/speed)
  - ukf_results_all_generators_5.png  (summary subplot)
  - ukf_results_rmse_5.png            (RMSE curve)
"""

import numpy as np
from scipy.io import loadmat
from ukf_estimation_5 import ukf_estimation_5
from plot_results_5 import plot_results_5


def main():
    print('=' * 60)
    print('Terminal Controller - UKF State Estimation')
    print('IEEE 5-Bus Overbye System (2 Generators, 3min)')
    print('Single Fault: Bus 2, 5.0s - 5.3s, Line 2-4 removed')
    print('=' * 60)

    # ─── Step 1: Load system parameters ───
    print('\n[Step 1/4] Loading System Parameters...')
    data = loadmat('system_params.mat')

    fs = data['fs'][0, 0].astype(float)
    total_time = data['total_time'][0, 0].astype(float)
    num_samples = int(total_time * fs)
    n = int(data['n'][0, 0])
    s = int(data['s'][0, 0])
    ns = 2 * n
    nm = 2 * n + 2 * s

    t_SW = float(data['t_SW'][0, 0])
    t_FC = float(data['t_FC'][0, 0])

    sig = float(data['sig'][0, 0])
    P_init = sig ** 2 * np.eye(ns)
    Q_mat = sig ** 2 * np.eye(ns)
    R_meas = sig ** 2 * np.eye(nm)
    W = np.ones((2 * ns, 1)) / (2 * ns)

    sp = {
        'YBUS': data['YBUS'],
        'RV': data['RV'],
        'E_abs': data['E_abs'].flatten(),
        'PM': data['PM'].flatten(),
        'M': data['M'].flatten(),
        'D': data['D'].flatten(),
        'n': n, 's': s, 'ns': ns, 'nm': nm, 'fs': fs,
        'deltt': 1.0 / fs, 'num_samples': num_samples,
        't_SW': t_SW, 't_FC': t_FC,
        'P': P_init, 'Q_mat': Q_mat, 'R_meas': R_meas, 'W': W,
        'X_hat': data['X_0'].flatten(),
    }

    print(f'  Buses: {s}, Generators: {n}')
    print(f'  Samples: {num_samples}, Sampling: {fs} Hz')
    print(f'  Fault: {t_SW}s - {t_FC}s')

    # ─── Step 2: Load measurements ───
    print('\n[Step 2/4] Loading Measurements File...')

    Z_mes = np.zeros((nm, num_samples))

    try:
        with open('measurements.txt', 'r') as f:
            lines = f.readlines()

        loaded_samples = len(lines) - 1
        actual_samples = min(num_samples, loaded_samples)
        print(f'  Loaded measurements.txt ({actual_samples} samples)')

        for i in range(actual_samples):
            parts = lines[i + 1].strip().split(',')
            for j in range(nm):
                Z_mes[j, i] = float(parts[j + 1])

            if (i + 1) % 50000 == 0:
                print(f'  Parsing: {(i + 1) / actual_samples * 100:.0f}%')

    except FileNotFoundError:
        print('  ERROR: measurements.txt not found!')
        print('  Run terminal_node_5.py first to generate measurement data.')
        return

    print('  All measurement data loaded.')

    # ─── Step 3: Load true states (optional) ───
    print('\n[Step 3/4] Loading True States (optional)...')
    try:
        X_true = np.loadtxt('true_states.csv', delimiter=',', skiprows=1).T
        print(f'  Loaded true_states.csv ({X_true.shape[1]} points)')
    except FileNotFoundError:
        print('  true_states.csv not found, will use zero comparison.')
        X_true = np.zeros((ns, num_samples))

    # ─── Step 4: Run UKF estimation ───
    print('\n[Step 4/4] Running UKF Estimation...')
    X_est, RMSE = ukf_estimation_5(sp, Z_mes)

    t = np.arange(actual_samples) / fs

    # ─── Plot results ───
    print('\n[Post Processing] Plotting results...')
    plot_results_5(t, X_true[:, :actual_samples], X_est[:, :actual_samples],
                   RMSE[:actual_samples], n, t_SW, t_FC)

    print('\n' + '=' * 60)
    print('Simulation Parameters:')
    print(f'  System: IEEE 5-Bus Overbye, {n} Generators')
    print(f'  Sampling: {fs} Hz, Time step: {1.0/fs} s')
    print(f'  Total Time: {total_time} s (3 minutes)')
    print(f'  Total Samples: {num_samples}')
    print(f'  Fault: {t_SW}s - {t_FC}s (Bus 2 three-phase, Line 2-4)')
    print(f'  State dim: {ns}, Measurement dim: {nm}')
    print('=' * 60)


if __name__ == '__main__':
    main()
