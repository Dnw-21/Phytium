"""
Vectorized RK4 integrator for UKF sigma point propagation.
Standalone module (identical to the RK4 in dynamic_system.py but used by the controller).
"""

import numpy as np


def rk4(n, deltt, E_abs, ns, X_sigma, PM, M, D, Ybusm):
    """
    Vectorized 4th-order Runge-Kutta integration for UKF sigma points.

    Parameters:
        n:        number of generators
        deltt:    time step
        E_abs:    internal voltage magnitudes [n]
        ns:       state dimension (2*n)
        X_sigma:  sigma points [ns x (2*ns)]
        PM:       mechanical power [n]
        M:        inertia constants [n]
        D:        damping coefficients [n]
        Ybusm:    current reduced admittance matrix [n x n]

    Returns:
        xbreve:   propagated sigma points [ns x (2*ns)]
    """
    E1 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * X_sigma[:n, :])
    I1 = Ybusm @ E1
    PG1 = np.real(E1 * np.conj(I1))
    PM_rep = np.tile(PM.reshape(-1, 1), (1, 2 * ns))
    M_rep = np.tile(M.reshape(-1, 1), (1, 2 * ns))
    D_rep = np.tile(D.reshape(-1, 1), (1, 2 * ns))

    k1_w = deltt * (M_rep**(-1) * (PM_rep - PG1 - (D_rep * X_sigma[n:ns, :])))
    k1_delta = deltt * X_sigma[n:ns, :]

    E2 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * (X_sigma[:n, :] + k1_delta / 2.0))
    I2 = Ybusm @ E2
    PG2 = np.real(E2 * np.conj(I2))
    k2_w = deltt * (M_rep**(-1) * (PM_rep - PG2 - (D_rep * (X_sigma[n:ns, :] + k1_w / 2.0))))
    k2_delta = deltt * (X_sigma[n:ns, :] + k1_w / 2.0)

    E3 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * (X_sigma[:n, :] + k2_delta / 2.0))
    I3 = Ybusm @ E3
    PG3 = np.real(E3 * np.conj(I3))
    k3_w = deltt * (M_rep**(-1) * (PM_rep - PG3 - (D_rep * (X_sigma[n:ns, :] + k2_w / 2.0))))
    k3_delta = deltt * (X_sigma[n:ns, :] + k2_w / 2.0)

    E4 = np.tile(E_abs.reshape(-1, 1), (1, 2 * ns)) * np.exp(1j * (X_sigma[:n, :] + k3_delta))
    I4 = Ybusm @ E4
    PG4 = np.real(E4 * np.conj(I4))
    k4_w = deltt * (M_rep**(-1) * (PM_rep - PG4 - (D_rep * (X_sigma[n:ns, :] + k3_w))))
    k4_delta = deltt * (X_sigma[n:ns, :] + k3_w)

    xbreve = np.zeros((ns, 2 * ns))
    xbreve[:n, :] = X_sigma[:n, :] + (k1_delta + 2.0 * k2_delta + 2.0 * k3_delta + k4_delta) / 6.0
    xbreve[n:ns, :] = X_sigma[n:ns, :] + (k1_w + 2.0 * k2_w + 2.0 * k3_w + k4_w) / 6.0

    return xbreve
