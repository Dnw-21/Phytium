import numpy as np
from RK4 import rk4


def get_system_state(k, t_SW, t_FC):
    if k < t_SW:
        return 0
    elif k <= t_FC:
        return 1
    else:
        return 2


def ukf_estimation_39(sp, Z_mes):
    YBUS = sp['YBUS']
    RV = sp['RV']
    E_abs = sp['E_abs']
    PM = sp['PM']
    M = sp['M']
    D = sp['D']
    n = sp['n']
    s = sp['s']
    ns = sp['ns']
    nm = sp['nm']
    fs = sp['fs']
    deltt = sp['deltt']
    num_samples = sp['num_samples']
    t_SW = sp['t_SW']
    t_FC = sp['t_FC']

    P = sp['P'].copy()
    Q_mat = sp['Q_mat']
    R_meas = sp['R_meas']
    W = sp['W']

    X_hat = sp['X_hat'].copy()

    X_est = np.zeros((ns, num_samples))
    RMSE = np.zeros(num_samples)

    print(f'Starting UKF estimation for {num_samples} samples...')
    print(f'  States: {ns}, Measurements: {nm}, Generators: {n}')

    for idx in range(num_samples):
        k = idx / fs

        ps = get_system_state(k, t_SW, t_FC)

        Ybusm = YBUS[:, :, ps]
        RVm = RV[:, :, ps]

        root = np.linalg.cholesky(ns * P).T
        X_tilde = np.hstack([root, -root])
        X_sigma = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns)) + X_tilde

        xbreve = rk4(n, deltt, E_abs, ns, X_sigma, PM, M, D, Ybusm)
        X_hat = (xbreve @ W).flatten()

        x_hat_rep = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns))
        P = (1.0 / (2 * ns)) * (xbreve - x_hat_rep) @ (xbreve - x_hat_rep).T + Q_mat

        root1 = np.linalg.cholesky(ns * P)
        X_tilde1 = np.hstack([root1, -root1])
        X_sigma = np.tile(X_hat.reshape(-1, 1), (1, 2 * ns)) + X_tilde1

        E11 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * X_sigma[:n, :])
        I11 = Ybusm @ E11
        PG11 = np.real(E11 * np.conj(I11))
        QG11 = np.imag(E11 * np.conj(I11))
        Vmag11 = np.abs(RVm @ E11)
        Vangle11 = np.angle(RVm @ E11)
        zbreve = np.vstack([PG11, QG11, Vmag11, Vangle11])

        zhat = (zbreve @ W).flatten()

        zhat_rep = np.tile(zhat.reshape(-1, 1), (1, 2 * ns))
        Pz = (1.0 / (2 * ns)) * (zbreve - zhat_rep) @ (zbreve - zhat_rep).T + R_meas
        Pxz = (1.0 / (2 * ns)) * (X_sigma - x_hat_rep) @ (zbreve - zhat_rep).T

        K = np.linalg.solve(Pz.T, Pxz.T).T

        z = Z_mes[:, idx]
        X_hat = X_hat + K @ (z - zhat)
        P = P - K @ Pz @ K.T

        X_est[:, idx] = X_hat
        RMSE[idx] = np.sqrt(np.trace(P))

        if (idx + 1) % 50000 == 0:
            print(f'  {(idx + 1) / num_samples * 100:.0f}% complete ({(idx + 1) / fs:.1f}s)')

    print('UKF estimation complete.')
    return X_est, RMSE
