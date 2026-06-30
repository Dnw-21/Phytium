"""UKF core for 5-Bus 2-Generator (online streaming)."""
import numpy as np
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from RK4 import rk4


class UKFState:
    def __init__(self):
        self.P = None; self.X_hat = None


def get_ps(k, t_SW, t_FC):
    if k < t_SW: return 0
    elif k <= t_FC: return 1
    else: return 2


def ukf_init(sp, st):
    st.P = sp['P'].copy(); st.Q_mat = sp['Q_mat']; st.R_meas = sp['R_meas']
    st.W = sp['W']; st.X_hat = sp['X_hat'].copy()
    st.n = sp['n']; st.s = sp['s']; st.ns = sp['ns']; st.nm = sp['nm']
    st.E_abs = sp['E_abs']; st.PM = sp['PM']; st.M = sp['M']; st.D = sp['D']
    st.YBUS = sp['YBUS']; st.RV = sp['RV']
    st.fs = sp['fs']; st.deltt = sp['deltt']
    st.t_SW = sp['t_SW']; st.t_FC = sp['t_FC']


def ukf_step(st, z_k, k_time):
    n=st.n; s=st.s; ns=st.ns; nm=st.nm
    ps = get_ps(k_time, st.t_SW, st.t_FC)
    Ybusm = st.YBUS[:,:,ps]; RVm = st.RV[:,:,ps]

    try:
        L = np.linalg.cholesky(ns * st.P); root = L
    except np.linalg.LinAlgError:
        st.P = st.P + 1e-8 * np.eye(ns); L = np.linalg.cholesky(ns * st.P); root = L
    X_sigma = np.tile(st.X_hat.reshape(-1,1), (1, 2*ns)) + np.hstack([root, -root])

    xbreve = rk4(n, st.deltt, st.E_abs, ns, X_sigma, st.PM, st.M, st.D, Ybusm)
    X_pred = (xbreve @ st.W).flatten()
    x_hat_rep = np.tile(X_pred.reshape(-1,1), (1, 2*ns))
    st.P = (1.0/(2*ns))*(xbreve-x_hat_rep)@(xbreve-x_hat_rep).T + st.Q_mat
    st.P = (st.P + st.P.T)/2

    try:
        L1 = np.linalg.cholesky(ns * st.P); root1 = L1
    except np.linalg.LinAlgError:
        st.P = st.P + 1e-8 * np.eye(ns); L1 = np.linalg.cholesky(ns * st.P); root1 = L1
    X_s2 = np.tile(X_pred.reshape(-1,1), (1, 2*ns)) + np.hstack([root1, -root1])

    E11 = np.tile(st.E_abs.reshape(-1,1), (1, 2*ns)) * np.exp(1j * X_s2[:n,:])
    I11 = Ybusm @ E11; Vc = RVm @ E11
    zbreve = np.vstack([np.real(E11*np.conj(I11)), np.imag(E11*np.conj(I11)), np.abs(Vc), np.angle(Vc)])
    zhat = (zbreve @ st.W).flatten()
    zhat_rep = np.tile(zhat.reshape(-1,1), (1, 2*ns))

    Pz = (1.0/(2*ns))*(zbreve-zhat_rep)@(zbreve-zhat_rep).T + st.R_meas
    Pxz = (1.0/(2*ns))*(X_s2-x_hat_rep)@(zbreve-zhat_rep).T

    try: K = Pxz @ np.linalg.inv(Pz)
    except np.linalg.LinAlgError: Pz = Pz + 1e-8*np.eye(Pz.shape[0]); K = Pxz @ np.linalg.inv(Pz)

    X_est = X_pred + K @ (z_k - zhat)
    st.P = st.P - K @ Pz @ K.T; st.P = (st.P + st.P.T)/2
    st.X_hat = X_est
    return X_est, np.sqrt(np.trace(st.P))
