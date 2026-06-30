import numpy as np
from case39_system import get_case39_system, build_ybus, run_power_flow


def initialize_system_39():
    system = get_case39_system()
    bus = system['bus']
    gen = system['gen']
    branch = system['branch']
    n_bus = system['n_bus']
    n_gen = system['n_gen']

    Y = build_ybus(system)

    pf_result = run_power_flow(system)
    V = pf_result['V']
    gen_bus = pf_result['gen_bus']
    P_gen = pf_result['P_gen']
    Q_gen = pf_result['Q_gen']

    baseMVA = 100.0

    Xd = np.array([0.006, 0.0697, 0.0531, 0.0436, 0.132, 0.05, 0.049, 0.057, 0.057, 0.031])
    H = np.array([500, 30.3, 35.8, 28.6, 26, 34.8, 26.4, 24.3, 34.5, 42])
    R = np.zeros(len(Xd))
    f0 = 60.0
    w_syn = 2.0 * np.pi * f0
    D = np.zeros(len(Xd)) / w_syn
    M = 2.0 * H / w_syn

    Sg = (P_gen + 1j * Q_gen) / baseMVA

    Y22 = np.diag(1.0 / (1j * Xd))

    SL = bus[:, 2] + 1j * bus[:, 3]
    SL = SL / baseMVA
    YL = np.conj(SL) / (np.abs(V) ** 2)
    Y11 = Y + np.diag(YL)
    Y11[np.ix_(gen_bus, gen_bus)] = Y11[np.ix_(gen_bus, gen_bus)] + Y22

    Y12 = np.zeros((n_bus, n_gen), dtype=complex)
    for i in range(n_bus):
        for k in range(n_gen):
            q = int(gen_bus[k])
            if i == q:
                Y12[q, k] = -1.0 / (R[k] + 1j * Xd[k])
    Y21 = Y12.T

    Ybf = Y22 - Y21 @ np.linalg.inv(Y11) @ Y12
    RV = np.zeros((n_bus, n_gen, 3), dtype=complex)
    RV[:, :, 0] = -np.linalg.inv(Y11) @ Y12

    f11 = 4 - 1
    F = np.array([4, 14])
    f1 = F[0] - 1
    f2 = F[1] - 1

    Y11df = np.delete(np.delete(Y11, f11, axis=0), f11, axis=1)
    Y12df = np.delete(Y12, f11, axis=0)
    Y21df = Y12df.T
    Ydf = Y22 - Y21df @ np.linalg.inv(Y11df) @ Y12df
    RV[:, :, 1] = np.zeros_like(RV[:, :, 0])
    RV[:-1, :, 1] = RV[:-1, :, 1] - np.linalg.inv(Y11df) @ Y12df

    Y11after = Y11.copy()
    Y11after[f1, f2] = 0
    Y11after[f2, f1] = 0
    for i in range(len(branch)):
        fb = int(branch[i, 0]) - 1
        tb = int(branch[i, 1]) - 1
        if (f1 == fb and f2 == tb) or (f2 == fb and f1 == tb):
            r, x, b_ch = branch[i, 2], branch[i, 3], branch[i, 4]
            Y11after[f1, f1] -= 1j * b_ch / 2.0 + 1.0 / (r + 1j * x)
            Y11after[f2, f2] -= 1j * b_ch / 2.0 + 1.0 / (r + 1j * x)
    Yaf = Y22 - Y21 @ np.linalg.inv(Y11after) @ Y12
    RV[:, :, 2] = -np.linalg.inv(Y11after) @ Y12

    YBUS = np.zeros((n_gen, n_gen, 3), dtype=complex)
    YBUS[:, :, 0] = Ybf
    YBUS[:, :, 1] = Ydf
    YBUS[:, :, 2] = Yaf

    Ig = np.conj(Sg / V[:n_gen])
    E0 = V[gen_bus] + Ig * (R + 1j * Xd)
    E_abs = np.abs(E0)

    X_0 = np.concatenate([np.angle(E0), np.zeros(n_gen)])

    I0 = Ybf @ E0
    PM = np.real(E0 * np.conj(I0))

    fs = 2000
    total_time = 180
    num_samples = int(total_time * fs)
    deltt = 1.0 / fs

    t_SW = 5.0
    t_FC = 5.3

    n = n_gen
    s = n_bus
    ns = 2 * n
    nm = 2 * n + 2 * s

    sig = 1e-2
    P = sig ** 2 * np.eye(ns)
    Q_mat = sig ** 2 * np.eye(ns)
    R_meas = sig ** 2 * np.eye(nm)

    X_hat = X_0.copy()
    W = np.ones((2 * ns, 1)) / (2 * ns)

    system_params = {
        'YBUS': YBUS,
        'RV': RV,
        'E_abs': E_abs,
        'PM': PM,
        'M': M,
        'D': D,
        'n': n,
        's': s,
        'ns': ns,
        'nm': nm,
        'fs': fs,
        'deltt': deltt,
        'total_time': total_time,
        'num_samples': num_samples,
        't_SW': t_SW,
        't_FC': t_FC,
        'X_0': X_0,
        'X_hat': X_hat,
        'P': P,
        'Q_mat': Q_mat,
        'R_meas': R_meas,
        'sig': sig,
        'W': W,
        'gen_bus': gen_bus,
    }

    return system_params
