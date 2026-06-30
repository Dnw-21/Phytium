"""
ukf_core_39.py — Online UKF step, exact copy of batch ukf_estimation_39 loop body.
"""
import numpy as np
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'DSE_Case39_3min', '10generator', '10generator'))
from RK4 import rk4


class UKFState:
    def __init__(self):
        self.P = None
        self.X_hat = None


def get_ps(k, t_SW, t_FC):
    if k < t_SW: return 0
    elif k <= t_FC: return 1
    else: return 2


def ukf_init(sp, st):
    """Initialize UKF state — exact copy of terminal_controller_39.py init."""
    ns = sp['ns']; nm = sp['nm']
    st.P = sp['P'].copy()
    st.Q_mat = sp['Q_mat']
    st.R_meas = sp['R_meas']
    st.W = sp['W']
    st.X_hat = sp['X_hat'].copy()
    st.n = sp['n']; st.s = sp['s']; st.ns = ns; st.nm = nm
    st.E_abs = sp['E_abs']; st.PM = sp['PM']; st.M = sp['M']; st.D = sp['D']
    st.YBUS = sp['YBUS']; st.RV = sp['RV']
    st.fs = sp['fs']; st.deltt = sp['deltt']
    st.t_SW = sp['t_SW']; st.t_FC = sp['t_FC']


def ukf_step(st, z_k, k_time):
    """Process ONE measurement — exact copy of ukf_estimation_39 loop body."""
    n=st.n; s=st.s; ns=st.ns; nm=st.nm
    ps = get_ps(k_time, st.t_SW, st.t_FC)
    Ybusm = st.YBUS[:,:,ps]; RVm = st.RV[:,:,ps]

    # ---- Sigma points ----
    try:
        L = np.linalg.cholesky(ns * st.P); root = L
    except np.linalg.LinAlgError:
        st.P = st.P + 1e-8 * np.eye(ns)
        L = np.linalg.cholesky(ns * st.P); root = L
    X_tilde = np.hstack([root, -root])
    X_sigma = np.tile(st.X_hat.reshape(-1,1), (1, 2*ns)) + X_tilde

    # ---- Prediction ----
    xbreve = rk4(n, st.deltt, st.E_abs, ns, X_sigma, st.PM, st.M, st.D, Ybusm)
    X_pred = (xbreve @ st.W).flatten()
    x_hat_rep = np.tile(X_pred.reshape(-1,1), (1, 2*ns))
    st.P = (1.0/(2*ns)) * (xbreve - x_hat_rep) @ (xbreve - x_hat_rep).T + st.Q_mat
    st.P = (st.P + st.P.T) / 2

    # ---- New sigma points ----
    try:
        L1 = np.linalg.cholesky(ns * st.P); root1 = L1
    except np.linalg.LinAlgError:
        st.P = st.P + 1e-8 * np.eye(ns)
        L1 = np.linalg.cholesky(ns * st.P); root1 = L1
    X_tilde1 = np.hstack([root1, -root1])
    X_sigma2 = np.tile(X_pred.reshape(-1,1), (1, 2*ns)) + X_tilde1

    # ---- Measurement prediction (Vmag/Vangle, matching MATLAB) ----
    E11 = np.tile(st.E_abs.reshape(-1,1), (1, 2*ns)) * np.exp(1j * X_sigma2[:n,:])
    I11 = Ybusm @ E11
    Vc11 = RVm @ E11
    zbreve = np.vstack([np.real(E11 * np.conj(I11)),
                         np.imag(E11 * np.conj(I11)),
                         np.abs(Vc11), np.angle(Vc11)])
    zhat = (zbreve @ st.W).flatten()
    zhat_rep = np.tile(zhat.reshape(-1,1), (1, 2*ns))

    Pz = (1.0/(2*ns)) * (zbreve - zhat_rep) @ (zbreve - zhat_rep).T + st.R_meas
    Pxz = (1.0/(2*ns)) * (X_sigma2 - x_hat_rep) @ (zbreve - zhat_rep).T

    # ---- Kalman gain ----
    try:
        K = Pxz @ np.linalg.inv(Pz)
    except np.linalg.LinAlgError:
        Pz = Pz + 1e-8 * np.eye(Pz.shape[0])
        K = Pxz @ np.linalg.inv(Pz)

    # ---- Measurement update ----
    X_est = X_pred + K @ (z_k - zhat)
    st.P = st.P - K @ Pz @ K.T
    st.P = (st.P + st.P.T) / 2
    st.X_hat = X_est

    rmse = np.sqrt(np.trace(st.P))
    return X_est, rmse
