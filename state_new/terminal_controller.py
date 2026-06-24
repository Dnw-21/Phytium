import numpy as np
from scipy.io import loadmat
from ukf_estimation import ukf_estimation
from plot_results import plot_results

def main():
    print('=' * 50)
    print('Terminal Controller - UKF State Estimation')
    print('3min, Multi-Fault (5.0-5.3s, 15.0-15.3s)')
    print('=' * 50)

    data = loadmat('system_params.mat')

    fs = data['fs'][0, 0].astype(float)
    total_time = data['total_time'][0, 0].astype(float)
    num_samples = int(total_time * fs)
    n = int(data['n'][0, 0])
    s = int(data['s'][0, 0])
    ns = 2 * n
    nm = 2 * n + 2 * s

    if 'fault_times' in data:
        ft = data['fault_times']
        fault1_start = float(ft[0, 0])
        fault1_end = float(ft[0, 1])
        fault2_start = float(ft[1, 0])
        fault2_end = float(ft[1, 1])
    else:
        fault1_start = 5.0
        fault1_end = 5.3
        fault2_start = 15.0
        fault2_end = 15.3

    sig = float(data['sig'][0, 0])
    P_init = sig**2 * np.eye(ns)
    Q_mat = sig**2 * np.eye(ns)
    R_meas = sig**2 * np.eye(nm)
    W = np.ones((2 * ns, 1)) / (2 * ns)

    sp = {
        'YBUS': data['YBUS'],
        'RV': data['RV'],
        'E_abs': data['E_abs'].flatten(),
        'PM': data['PM'].flatten(),
        'M': data['M'].flatten(),
        'D': data['D'].flatten(),
        'n': n,
        's': s,
        'ns': ns,
        'nm': nm,
        'fs': fs,
        'deltt': 1.0 / fs,
        'num_samples': num_samples,
        'fault1_start': fault1_start,
        'fault1_end': fault1_end,
        'fault2_start': fault2_start,
        'fault2_end': fault2_end,
        'P': P_init,
        'Q_mat': Q_mat,
        'R_meas': R_meas,
        'W': W,
        'X_hat': data['X_0'].flatten(),
    }

    Z_mes = np.zeros((nm, num_samples))

    node_assignments = [[0, 3, 4], [1, 5, 6], [2, 7, 8]]

    for node_idx, buses in enumerate(node_assignments):
        filename = f'node{node_idx+1}_measurements.txt'
        gen_i = node_idx

        print(f'Loading {filename}...')
        with open(filename, 'r') as f:
            lines = f.readlines()

        for line_idx, line in enumerate(lines[1:]):
            parts = line.strip().split('\t')
            i = line_idx

            Z_mes[gen_i, i] = float(parts[1])
            Z_mes[n + gen_i, i] = float(parts[2])

            for j, b in enumerate(buses):
                Z_mes[2*n + b, i] = float(parts[3 + j])
                Z_mes[2*n + s + b, i] = float(parts[3 + len(buses) + j])

    print('All node data loaded.')

    try:
        X_true = np.loadtxt('true_states.csv', delimiter=',', skiprows=1).T
        print(f'Loaded true_states.csv ({X_true.shape[1]} points)')
    except FileNotFoundError:
        print('true_states.csv not found, will run UKF without true comparison.')
        X_true = None

    X_est, RMSE = ukf_estimation(sp, Z_mes)

    t = np.arange(num_samples) / fs

    if X_true is not None:
        X_plot = X_true
    else:
        X_plot = np.zeros((ns, num_samples))

    plot_results(t, X_plot, X_est, RMSE, n, fault1_start, fault1_end, fault2_start, fault2_end)

    print('=' * 50)
    print('Simulation Parameters (3min, Multi-Fault):')
    print(f'Sampling Frequency: {fs} Hz')
    print(f'Total Time: {total_time} s (3 minutes)')
    print(f'Total Samples: {num_samples}')
    print(f'Fault 1: {fault1_start}s - {fault1_end}s (0.3s)')
    print(f'Fault 2: {fault2_start}s - {fault2_end}s (0.3s)')
    print('=' * 50)

if __name__ == '__main__':
    main()
