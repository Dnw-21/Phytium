import numpy as np
import os
from initialize_system import initialize_system
from generate_true_values import generate_true_values
from scipy.io import savemat

def main():
    print('=' * 50)
    print('Terminal Node - Data Generation')
    print('3min, Multi-Fault (5.0-5.3s, 15.0-15.3s)')
    print('=' * 50)

    sp = initialize_system()

    X_true, Z_mes = generate_true_values(sp)

    mat_dict = {
        'YBUS': sp['YBUS'],
        'RV': sp['RV'],
        'E_abs': sp['E_abs'],
        'PM': sp['PM'],
        'M': sp['M'],
        'D': sp['D'],
        'n': np.array([sp['n']]),
        's': np.array([sp['s']]),
        'fs': np.array([sp['fs']]),
        'fault_times': np.array([
            [sp['fault1_start'], sp['fault1_end']],
            [sp['fault2_start'], sp['fault2_end']]
        ]),
        'total_time': np.array([sp['total_time']]),
        'X_0': sp['X_0'],
        'sig': np.array([sp['sig']]),
        'gen_bus': sp['gen_bus'],
    }
    savemat('system_params.mat', mat_dict)
    print('Saved system_params.mat')

    header = 'delta1,delta2,delta3,omega1,omega2,omega3'
    np.savetxt('true_states.csv', X_true.T, delimiter=',', header=header, comments='', fmt='%.8f')
    print(f'Saved true_states.csv ({X_true.shape[1]} points)')

    n = sp['n']
    s = sp['s']
    fs = sp['fs']
    gen_bus = sp['gen_bus']

    node_assignments = [[0, 3, 4], [1, 5, 6], [2, 7, 8]]

    for node_idx, buses in enumerate(node_assignments):
        filename = f'node{node_idx+1}_measurements.txt'
        gen_i = node_idx

        with open(filename, 'w') as f:
            header_cols = ['timestamp', f'PG{gen_i+1}', f'QG{gen_i+1}']
            for b in buses:
                header_cols.append(f'V{b+1}')
            for b in buses:
                header_cols.append(f'angle{b+1}')
            f.write('\t'.join(header_cols) + '\n')

            for i in range(Z_mes.shape[1]):
                t = i / fs
                row = [f'{t:.6f}']
                row.append(f'{Z_mes[gen_i, i]:.8f}')
                row.append(f'{Z_mes[n+gen_i, i]:.8f}')
                for b in buses:
                    row.append(f'{Z_mes[2*n+b, i]:.8f}')
                for b in buses:
                    row.append(f'{Z_mes[2*n+s+b, i]:.8f}')
                f.write('\t'.join(row) + '\n')

        print(f'Saved {filename}')

    print('\nAll terminal node data generated successfully!')

if __name__ == '__main__':
    main()
