import numpy as np

def dynamic_system(t, x, M, D, Ybusm, Vo, Pm, NumG):
    Vg = Vo * np.exp(1j * x[:NumG])
    Ibus = Ybusm @ Vg
    S = np.conj(Ibus) * Vg
    Pe = np.real(S)
    dx = np.zeros(2 * NumG)
    dx[:NumG] = x[NumG:2*NumG]
    dx[NumG:2*NumG] = (Pm - Pe) / M - D * x[NumG:2*NumG] / M
    return dx

def RK4(n, deltt, E_abs, ns, X_sigma, PM, M, D, Ybusm):
    num_sigma = X_sigma.shape[1]
    
    E_abs = np.atleast_1d(E_abs)
    E_abs_mat = np.tile(E_abs.reshape(-1, 1), (1, num_sigma))
    
    E1 = E_abs_mat * np.exp(1j * X_sigma[:n, :])
    I1 = Ybusm @ E1
    PG1 = np.real(E1 * np.conj(I1))
    
    M = np.atleast_1d(M).reshape(-1, 1)
    D = np.atleast_1d(D).reshape(-1, 1)
    PM = np.atleast_2d(PM)
    
    k1_w = deltt * ((PM - PG1 - (D * X_sigma[n:ns, :])) / M)
    k1_delta = deltt * X_sigma[n:ns, :]
    
    E2 = E_abs_mat * np.exp(1j * (X_sigma[:n, :] + k1_delta / 2))
    I2 = Ybusm @ E2
    PG2 = np.real(E2 * np.conj(I2))
    k2_w = deltt * ((PM - PG2 - (D * (X_sigma[n:ns, :] + k1_w / 2))) / M)
    k2_delta = deltt * (X_sigma[n:ns, :] + k1_w / 2)
    
    E3 = E_abs_mat * np.exp(1j * (X_sigma[:n, :] + k2_delta / 2))
    I3 = Ybusm @ E3
    PG3 = np.real(E3 * np.conj(I3))
    k3_w = deltt * ((PM - PG3 - (D * (X_sigma[n:ns, :] + k2_w / 2))) / M)
    k3_delta = deltt * (X_sigma[n:ns, :] + k2_w / 2)
    
    E4 = E_abs_mat * np.exp(1j * (X_sigma[:n, :] + k3_delta))
    I4 = Ybusm @ E4
    PG4 = np.real(E4 * np.conj(I4))
    k4_w = deltt * ((PM - PG4 - (D * (X_sigma[n:ns, :] + k3_w))) / M)
    k4_delta = deltt * (X_sigma[n:ns, :] + k3_w)
    
    xbreve = np.zeros((ns, num_sigma))
    xbreve[:n, :] = X_sigma[:n, :] + (k1_delta + 2 * k2_delta + 2 * k3_delta + k4_delta) / 6
    xbreve[n:ns, :] = X_sigma[n:ns, :] + (k1_w + 2 * k2_w + 2 * k3_w + k4_w) / 6
    
    return xbreve