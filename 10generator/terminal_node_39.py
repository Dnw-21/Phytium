import numpy as np
from scipy.io import savemat
from initialize_system_39 import initialize_system_39
from dynamic_system import dynamic_system


def get_system_state(k, t_SW, t_FC):
    if k < t_SW:
        return 0
    elif k <= t_FC:
        return 1
    else:
        return 2


def rk4_step(t, x, dt, M, D, Ybusm, E_abs, PM, n):
    k1 = dt * dynamic_system(t, x, M, D, Ybusm, E_abs, PM, n)
    k2 = dt * dynamic_system(t + dt / 2, x + k1 / 2, M, D, Ybusm, E_abs, PM, n)
    k3 = dt * dynamic_system(t + dt / 2, x + k2 / 2, M, D, Ybusm, E_abs, PM, n)
    k4 = dt * dynamic_system(t + dt, x + k3, M, D, Ybusm, E_abs, PM, n)
    return x + (k1 + 2 * k2 + 2 * k3 + k4) / 6


def main():
    print('=' * 60)
    print('Terminal Node - Data Generation')
    print('IEEE 39-Bus System (10 Generators, 3min)')
    print('Single Fault: Bus 4, 5.0s - 5.3s')
    print('=' * 60)

    print('\n[Step 1/3] Initializing System...')
    sp = initialize_system_39()
    print(f'  Buses: {sp["s"]}, Generators: {sp["n"]}')
    print(f'  Generator buses (1-indexed): {sp["gen_bus"] + 1}')
    print(f'  E_abs: {np.array2string(sp["E_abs"], precision=4)}')
    print(f'  delta0 (deg): {np.array2string(sp["X_0"][:sp["n"]] * 180 / np.pi, precision=3)}')

    YBUS = sp['YBUS']
    RV = sp['RV']
    E_abs = sp['E_abs']
    PM = sp['PM']
    M = sp['M']
    D = sp['D']
    n = sp['n']
    s = sp['s']
    fs = sp['fs']
    deltt = sp['deltt']
    total_time = sp['total_time']
    num_samples = sp['num_samples']
    t_SW = sp['t_SW']
    t_FC = sp['t_FC']

    ns = 2 * n
    nm = 2 * n + 2 * s

    print(f'\n[Step 2/3] Generating True Values and Measurements...')
    print(f'  deltt={deltt}s, fs={fs}Hz, total={total_time}s, samples={num_samples}')
    print(f'  Fault: {t_SW}s - {t_FC}s (Bus 4 three-phase)')
    print(f'  Measurement dim Z: {nm} = ({n}PG + {n}QG + {s}Vmag + {s}Vangle)')

    X_true = np.zeros((ns, num_samples))
    Z_mes = np.zeros((nm, num_samples))

    X_sim = sp['X_0'].copy()

    for idx in range(num_samples):
        k = idx / fs
        ps = get_system_state(k, t_SW, t_FC)

        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]

        X_sim = rk4_step(k, X_sim, deltt, M, D, Ybusm, E_abs, PM, n)
        X_true[:, idx] = X_sim

        E = E_abs * np.exp(1j * X_sim[:n])
        I = Ybusm @ E
        V_bus = RVm @ E

        Z_mes[:n, idx] = np.real(E * np.conj(I))
        Z_mes[n:2 * n, idx] = np.imag(E * np.conj(I))
        Z_mes[2 * n:2 * n + s, idx] = np.abs(V_bus)
        Z_mes[2 * n + s:2 * n + 2 * s, idx] = np.angle(V_bus)

        if (idx + 1) % 50000 == 0:
            print(f'  {(idx + 1) / num_samples * 100:.0f}% complete ({(idx + 1) / fs:.1f}s)')

    print('  Data generation complete.')

    print('\n[Step 3/3] Saving Files...')

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
        't_SW': np.array([sp['t_SW']]),
        't_FC': np.array([sp['t_FC']]),
        'total_time': np.array([sp['total_time']]),
        'X_0': sp['X_0'],
        'sig': np.array([sp['sig']]),
        'gen_bus': sp['gen_bus'],
    }
    savemat('system_params.mat', mat_dict)
    print('  Saved system_params.mat')

    header = ','.join([f'delta{i+1}' for i in range(n)] + [f'omega{i+1}' for i in range(n)])
    np.savetxt('true_states.csv', X_true.T, delimiter=',', header=header, comments='', fmt='%.8f')
    print(f'  Saved true_states.csv ({X_true.shape[1]} points)')

    z_cols = (
        [f'PG{i+1}' for i in range(n)]
        + [f'QG{i+1}' for i in range(n)]
        + [f'V{i+1}' for i in range(s)]
        + [f'angle{i+1}' for i in range(s)]
    )
    z_header = 'timestamp,' + ','.join(z_cols)

    with open('measurements.txt', 'w') as f:
        f.write(z_header + '\n')
        for i in range(num_samples):
            t = i / fs
            row = [f'{t:.6f}'] + [f'{Z_mes[j, i]:.10f}' for j in range(nm)]
            f.write(','.join(row) + '\n')
            if (i + 1) % 50000 == 0:
                print(f'  Writing measurements: {(i+1)/num_samples*100:.0f}%')

    print(f'  Saved measurements.txt ({num_samples} rows x {nm + 1} cols)')

    print('\n' + '=' * 60)
    print('All terminal node data generated successfully!')
    print(f'Generated files:')
    print(f'  system_params.mat  - system parameters for controller')
    print(f'  true_states.csv    - true states (20 dim x {num_samples} pts)')
    print(f'  measurements.txt   - full Z vector (98 dim x {num_samples} pts)')
    print('=' * 60)


if __name__ == '__main__':
    main()
