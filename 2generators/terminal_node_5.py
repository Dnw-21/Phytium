"""
Terminal Node - Data Generation
IEEE 5-Bus Overbye System (2 Generators, 3min)
Single Fault: Bus 2, 5.0s - 5.3s, Line 2-4 removed

Run: python terminal_node_5.py

Generates:
  - system_params.mat   (system parameters for controller)
  - true_states.csv     (true states: 2δ + 2ω = 4 dim × 360,000 pts)
  - measurements.txt    (full measurement vector: timestamp + 14 dim Z × 360,000 pts)
"""

import numpy as np
from scipy.io import savemat
from initialize_system_5 import initialize_system_5
from dynamic_system import dynamic_system


def get_system_state(k, t_SW, t_FC):
    """
    Determine the system state index based on current time.
    0 = pre-fault, 1 = during-fault, 2 = post-fault.
    """
    if k < t_SW:
        return 0
    elif k <= t_FC:
        return 1
    else:
        return 2


def rk4_step(t, x, dt, M, D, Ybusm, E_abs, PM, n):
    """
    Single RK4 step for true state propagation.
    Corresponds to the ode45 call in MATLAB.
    """
    k1 = dt * dynamic_system(t, x, M, D, Ybusm, E_abs, PM, n)
    k2 = dt * dynamic_system(t + dt / 2.0, x + k1 / 2.0, M, D, Ybusm, E_abs, PM, n)
    k3 = dt * dynamic_system(t + dt / 2.0, x + k2 / 2.0, M, D, Ybusm, E_abs, PM, n)
    k4 = dt * dynamic_system(t + dt, x + k3, M, D, Ybusm, E_abs, PM, n)
    return x + (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0


def main():
    print('=' * 60)
    print('Terminal Node - Data Generation')
    print('IEEE 5-Bus Overbye System (2 Generators, 3min)')
    print('Single Fault: Bus 2, 5.0s - 5.3s, Line 2-4 removed')
    print('=' * 60)

    # ─── Step 1: Initialize system ───
    print('\n[Step 1/3] Initializing System...')
    sp = initialize_system_5()
    print(f'  Buses: {sp["s"]}, Generators: {sp["n"]}')
    print(f'  Generator buses (1-indexed): {sp["gen_bus"] + 1}')
    print(f'  E_abs: {np.array2string(sp["E_abs"], precision=4)}')
    print(f'  delta0 (deg): {np.array2string(sp["X_0"][:sp["n"]] * 180.0 / np.pi, precision=3)}')

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

    # ─── Step 2: Generate true states and measurements ───
    print(f'\n[Step 2/3] Generating True Values and Measurements...')
    print(f'  deltt={deltt}s, fs={fs}Hz, total={total_time}s, samples={num_samples}')
    print(f'  Fault: {t_SW}s - {t_FC}s (Bus 2 three-phase)')
    print(f'  Measurement dim Z: {nm} = ({n}PG + {n}QG + {s}Vmag + {s}Vangle)')

    X_true = np.zeros((ns, num_samples))
    Z_mes = np.zeros((nm, num_samples))

    X_sim = sp['X_0'].copy()

    for idx in range(num_samples):
        k = idx / fs
        ps = get_system_state(k, t_SW, t_FC)

        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]

        # Propagate true state using RK4
        X_sim = rk4_step(k, X_sim, deltt, M, D, Ybusm, E_abs, PM, n)
        X_true[:, idx] = X_sim

        # Compute measurements from true state
        E = E_abs * np.exp(1j * X_sim[:n])
        I = Ybusm @ E
        V_bus = RVm @ E

        Z_mes[:n, idx] = np.real(E * np.conj(I))               # PG
        Z_mes[n:2 * n, idx] = np.imag(E * np.conj(I))           # QG
        Z_mes[2 * n:2 * n + s, idx] = np.abs(V_bus)             # Vmag
        Z_mes[2 * n + s:2 * n + 2 * s, idx] = np.angle(V_bus)   # Vangle

        if (idx + 1) % 50000 == 0:
            print(f'  {(idx + 1) / num_samples * 100:.0f}% complete ({(idx + 1) / fs:.1f}s)')

    print('  Data generation complete.')

    # ─── Step 3: Save files ───
    print('\n[Step 3/3] Saving Files...')

    # system_params.mat
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

    # true_states.csv
    header = ','.join([f'delta{i+1}' for i in range(n)] + [f'omega{i+1}' for i in range(n)])
    np.savetxt('true_states.csv', X_true.T, delimiter=',', header=header, comments='', fmt='%.8f')
    print(f'  Saved true_states.csv ({X_true.shape[1]} points)')

    # measurements.txt
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
    print(f'  true_states.csv    - true states ({ns} dim x {num_samples} pts)')
    print(f'  measurements.txt   - full Z vector ({nm} dim x {num_samples} pts)')
    print('=' * 60)


if __name__ == '__main__':
    main()
