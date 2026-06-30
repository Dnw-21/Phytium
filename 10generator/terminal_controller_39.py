import numpy as np
from scipy.io import loadmat
from ukf_estimation_39 import ukf_estimation_39
from plot_results_39 import plot_results_39


def main():
    print('=' * 60)
    print('Terminal Controller - UKF State Estimation')
    print('IEEE 39-Bus System (10 Generators, 3min)')
    print('Single Fault: Bus 4, 5.0s - 5.3s')
    print('=' * 60)

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
                print(f'  Parsing: {(i+1)/actual_samples*100:.0f}%')

    except FileNotFoundError:
        print('  ERROR: measurements.txt not found!')
        return

    print('  All measurement data loaded.')

    print('\n[Step 3/4] Loading True States (optional)...')
    try:
        X_true = np.loadtxt('true_states.csv', delimiter=',', skiprows=1).T
        print(f'  Loaded true_states.csv ({X_true.shape[1]} points)')
    except FileNotFoundError:
        print('  true_states.csv not found, will use zero comparison.')
        X_true = np.zeros((ns, num_samples))

    print('\n[Step 4/4] Running UKF Estimation...')
    X_est, RMSE = ukf_estimation_39(sp, Z_mes)

    t = np.arange(actual_samples) / fs

    print('\n[Post Processing] Plotting results...')
    plot_results_39(t, X_true[:, :actual_samples], X_est[:, :actual_samples],
                    RMSE[:actual_samples], n, t_SW, t_FC)

    print('\n' + '=' * 60)
    print('Simulation Parameters:')
    print(f'  System: IEEE 39-Bus, 10 Generators')
    print(f'  Sampling: {fs} Hz, Time step: {1.0/fs} s')
    print(f'  Total Time: {total_time} s (3 minutes)')
    print(f'  Total Samples: {num_samples}')
    print(f'  Fault: {t_SW}s - {t_FC}s (Bus 4 three-phase, Line 4-14)')
    print(f'  State dim: {ns}, Measurement dim: {nm}')
    print('=' * 60)


if __name__ == '__main__':
    main()
