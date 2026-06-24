import numpy as np
from dynamic_system import dynamic_system

def get_system_state(k, fault1_start, fault1_end, fault2_start, fault2_end):
    if k < fault1_start:
        return 0
    elif fault1_start <= k < fault1_end:
        return 1
    elif fault1_end <= k < fault2_start:
        return 2
    elif fault2_start <= k < fault2_end:
        return 1
    else:
        return 2

def rk4_step(t, x, dt, M, D, Ybusm, E_abs, PM, n):
    k1 = dt * dynamic_system(t, x, M, D, Ybusm, E_abs, PM, n)
    k2 = dt * dynamic_system(t + dt/2, x + k1/2, M, D, Ybusm, E_abs, PM, n)
    k3 = dt * dynamic_system(t + dt/2, x + k2/2, M, D, Ybusm, E_abs, PM, n)
    k4 = dt * dynamic_system(t + dt, x + k3, M, D, Ybusm, E_abs, PM, n)
    return x + (k1 + 2*k2 + 2*k3 + k4) / 6

def generate_true_values(sp):
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
    fault1_start = sp['fault1_start']
    fault1_end = sp['fault1_end']
    fault2_start = sp['fault2_start']
    fault2_end = sp['fault2_end']

    ns = 2 * n
    nm = 2 * n + 2 * s

    X_true = np.zeros((ns, num_samples))
    Z_mes = np.zeros((nm, num_samples))

    X_sim = sp['X_0'].copy()

    print(f'Generating {num_samples} samples ({total_time}s at {fs}Hz)...')

    for idx in range(num_samples):
        k = idx / fs
        ps = get_system_state(k, fault1_start, fault1_end, fault2_start, fault2_end)

        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]

        X_sim = rk4_step(k, X_sim, deltt, M, D, Ybusm, E_abs, PM, n)
        X_true[:, idx] = X_sim

        E = E_abs * np.exp(1j * X_sim[:n])
        I = Ybusm @ E
        V_bus = RVm @ E

        PG = np.real(E * np.conj(I))
        QG = np.imag(E * np.conj(I))
        V_mag = np.abs(V_bus)
        V_angle = np.angle(V_bus)

        Z_mes[:n, idx] = PG
        Z_mes[n:2*n, idx] = QG
        Z_mes[2*n:2*n+s, idx] = V_mag
        Z_mes[2*n+s:2*n+2*s, idx] = V_angle

        if (idx + 1) % 30000 == 0:
            print(f'  {(idx+1)/num_samples*100:.0f}% complete ({(idx+1)/fs:.1f}s)')

    print('Data generation complete.')
    return X_true, Z_mes
