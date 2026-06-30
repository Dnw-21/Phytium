import numpy as np
from case9_sauer import case9_sauer
from ybus_new import ybus_new

def initialize_system():
    baseMVA, bus, gen, branch, Xd, R, H, D, f0 = case9_sauer()

    Y = ybus_new(bus, branch)

    Vmag = np.array([1.040, 1.025, 1.025, 1.026, 0.996, 1.013, 1.026, 1.016, 1.032])
    Vph = np.array([0.000, 9.280, 4.665, -2.217, -3.989, -3.687, 3.720, 0.728, 1.967])
    V = Vmag * np.exp(1j * Vph * np.pi / 180.0)

    P_jQ = np.conj(V) * (Y @ V)
    S = np.conj(P_jQ)
    S = S / baseMVA
    Sg = gen[:, 1] + 1j * gen[:, 2]
    Sg = Sg / baseMVA

    w_syn = 2.0 * np.pi * f0
    M = 2.0 * H / w_syn
    gen_bus = gen[:, 0].astype(int)

    Y22 = np.diag(1.0 / (1j * Xd))

    SL = bus[:, 2] + 1j * bus[:, 3]
    SL = SL / baseMVA
    YL = np.conj(SL) / (np.abs(V)**2)
    Y11 = Y + np.diag(YL)
    for i in range(len(gen_bus)):
        gb = gen_bus[i] - 1
        Y11[gb, gb] = Y11[gb, gb] + Y22[i, i]

    Y12 = np.zeros((bus.shape[0], gen.shape[0]), dtype=np.complex128)
    for i in range(bus.shape[0]):
        for k in range(gen.shape[0]):
            q = gen_bus[k] - 1
            if i == q:
                Y12[q, k] = -1.0 / (R[k] + 1j * Xd[k])
    Y21 = Y12.T

    Ybf = Y22 - Y21 @ np.linalg.inv(Y11) @ Y12
    RV = np.zeros((bus.shape[0], gen.shape[0], 3), dtype=np.complex128)
    RV[:, :, 0] = -np.linalg.inv(Y11) @ Y12

    f11 = 7
    F = np.array([7, 8])
    f1 = F[0]
    f2 = F[1]

    Y11df = Y11.copy()
    Y11df = np.delete(Y11df, f11, axis=0)
    Y11df = np.delete(Y11df, f11, axis=1)
    Y12df = np.delete(Y12, f11, axis=0)
    Y21df = Y12df.T
    Ydf = Y22 - Y21df @ np.linalg.inv(Y11df) @ Y12df
    RV[:, :, 1] = np.zeros_like(RV[:, :, 0])
    RV[:-1, :, 1] = RV[:-1, :, 1] - np.linalg.inv(Y11df) @ Y12df

    Y11after = Y11.copy()
    Y11after[f1, f2] = 0
    Y11after[f2, f1] = 0
    for i in range(branch.shape[0]):
        fb = int(branch[i, 0]) - 1
        tb = int(branch[i, 1]) - 1
        if (f1 == fb and f2 == tb) or (f2 == fb and f1 == tb):
            r, x, b_ch = branch[i, 2], branch[i, 3], branch[i, 4]
            Z_line = r + 1j * x
            Y11after[f1, f1] = Y11after[f1, f1] - 1j * b_ch / 2.0 - 1.0 / Z_line
            Y11after[f2, f2] = Y11after[f2, f2] - 1j * b_ch / 2.0 - 1.0 / Z_line
    Yaf = Y22 - Y21 @ np.linalg.inv(Y11after) @ Y12
    RV[:, :, 2] = -np.linalg.inv(Y11after) @ Y12

    YBUS = np.zeros((3, 3, 3), dtype=np.complex128)
    YBUS[:, :, 0] = Ybf
    YBUS[:, :, 1] = Ydf
    YBUS[:, :, 2] = Yaf

    Ig = np.conj(Sg / V[:len(gen_bus)])
    E0 = V[gen_bus - 1] + Ig * (R + 1j * Xd)
    E_abs = np.abs(E0)

    X_0 = np.concatenate([np.angle(E0), np.zeros(len(Xd))])

    I0 = Ybf @ E0
    PG0 = np.real(E0 * np.conj(I0))
    PM = PG0.copy()

    fs = 1000
    total_time = 180
    num_samples = total_time * fs
    deltt = 1.0 / fs

    fault1_start = 5.0
    fault1_end = 5.3
    fault2_start = 15.0
    fault2_end = 15.3

    n = len(gen_bus)
    s = bus.shape[0]
    ns = 2 * n
    nm = 2 * n + 2 * s

    sig = 1e-2
    P = sig**2 * np.eye(ns)
    Q_mat = sig**2 * np.eye(ns)
    R_meas = sig**2 * np.eye(nm)

    X_hat = X_0.copy()

    W = np.ones((2 * ns, 1)) / (2 * ns)

    t_points = np.arange(num_samples)

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
        'fault1_start': fault1_start,
        'fault1_end': fault1_end,
        'fault2_start': fault2_start,
        'fault2_end': fault2_end,
        'X_0': X_0,
        'X_hat': X_hat,
        'P': P,
        'Q_mat': Q_mat,
        'R_meas': R_meas,
        'sig': sig,
        'W': W,
        'gen_bus': gen_bus,
        't_points': t_points,
    }

    return system_params
