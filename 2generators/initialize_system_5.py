"""
System initialization for IEEE 5-Bus Overbye System (2 generators).
Corresponds to the initialization section of DSE_Calculation_UKF_5bus_3min.m.

Key differences from Case39:
  - 5 buses, 2 generators
  - Fault: Bus 2 three-phase fault, Line 2-4 removed after fault
  - Generator parameters: Xd=[0.05, 0.025], H=[5.0, 3.0]
"""

import numpy as np
from case5_system import get_case5_system, build_ybus, run_power_flow


def initialize_system_5():
    """
    Initialize the 5-bus Overbye system for UKF dynamic state estimation.

    Steps (matching MATLAB DSE_Calculation_UKF_5bus_3min.m):
      1. Load system data (case5_Overbye)
      2. Build Ybus
      3. Run/simulate power flow → get V, generator powers
      4. Compute Y11, Y12, Y21, Y22 → reduced YBUS matrices (pre/during/post fault)
      5. Compute RV (bus voltage reconstruction) matrices
      6. Initialize state, UKF parameters

    Returns:
        system_params: dict with all system parameters needed for simulation & UKF
    """
    # ─── Step 1: Load system data ───
    system = get_case5_system()
    bus = system['bus']
    gen = system['gen']
    branch = system['branch']
    n_bus = system['n_bus']
    n_gen = system['n_gen']

    # ─── Step 2: Build Ybus ───
    Y = build_ybus(system)

    # ─── Step 3: Power flow results ───
    pf_result = run_power_flow(system)
    V = pf_result['V']
    gen_bus = pf_result['gen_bus']
    P_gen = pf_result['P_gen']
    Q_gen = pf_result['Q_gen']

    baseMVA = 100.0

    # ─── Generator parameters (Overbye 5-bus) ───
    # These match the MATLAB code:
    #   Xd=[0.05; 0.025]; R=[0; 0]; H=[5.0; 3.0]; D=[0; 0];
    Xd = np.array([0.05, 0.025])
    H = np.array([5.0, 3.0])
    R = np.zeros(len(Xd))
    f0 = 60.0
    w_syn = 2.0 * np.pi * f0
    D = np.zeros(len(Xd))
    M = 2.0 * H / w_syn

    # ─── Step 4: Generator power in pu ───
    Sg = (P_gen + 1j * Q_gen) / baseMVA

    # ─── Step 5: Y22 (generator internal admittance) ───
    Y22 = np.diag(1.0 / (1j * Xd))

    # ─── Step 6: Y11 (network admittance + load admittance + Y22) ───
    # Load admittance: YL = conj(SL) / |V|^2
    SL = bus[:, 2] + 1j * bus[:, 3]        # Pd + jQd from bus data
    SL = SL / baseMVA
    YL = np.conj(SL) / (np.abs(V) ** 2)
    Y11 = Y + np.diag(YL)
    # Add generator internal admittance to diagonal at generator buses
    Y11[np.ix_(gen_bus, gen_bus)] = Y11[np.ix_(gen_bus, gen_bus)] + Y22

    # ─── Step 7: Y12 and Y21 (coupling between network and generators) ───
    Y12 = np.zeros((n_bus, n_gen), dtype=complex)
    for i in range(n_bus):
        for k in range(n_gen):
            q = int(gen_bus[k])
            if i == q:
                Y12[q, k] = -1.0 / (R[k] + 1j * Xd[k])
    Y21 = Y12.T

    # ─── Step 8: Pre-fault reduced YBUS and RV ───
    Ybf = Y22 - Y21 @ np.linalg.inv(Y11) @ Y12
    RV = np.zeros((n_bus, n_gen, 3), dtype=complex)
    RV[:, :, 0] = -np.linalg.inv(Y11) @ Y12

    # ─── Step 9: Fault configuration ───
    # Fault: bus 2 fault, line 2-4 removed after fault
    # MATLAB: f11=2; F=[2 4]; f1=F(1); f2=F(2);
    f11 = 2 - 1      # 0-indexed fault bus (bus 2)
    F = np.array([2, 4])
    f1 = F[0] - 1    # 0-indexed
    f2 = F[1] - 1    # 0-indexed

    # ─── Step 10: During-fault reduced YBUS (bus 2 removed) ───
    Y11df = np.delete(np.delete(Y11, f11, axis=0), f11, axis=1)
    Y12df = np.delete(Y12, f11, axis=0)
    Y21df = Y12df.T
    Ydf = Y22 - Y21df @ np.linalg.inv(Y11df) @ Y12df

    # During-fault RV: row for faulted bus is zero
    # MATLAB equivalent:
    #   RV(:, :, 2)=zeros(size(RV(:, :, 1)));
    #   RV(1:end-1, :, 2)=RV(1:end-1, :, 2)-inv(Y11df)*Y12df;
    RV[:, :, 1] = 0.0
    temp_df = -np.linalg.inv(Y11df) @ Y12df  # (n_bus-1) × n_gen
    RV[:n_bus - 1, :, 1] = temp_df

    # ─── Step 11: Post-fault reduced YBUS (line 2-4 removed) ───
    Y11after = Y11.copy()
    Y11after[f1, f2] = 0
    Y11after[f2, f1] = 0
    # Remove the branch admittance from diagonal
    for i in range(len(branch)):
        fb = int(branch[i, 0]) - 1
        tb = int(branch[i, 1]) - 1
        if (f1 == fb and f2 == tb) or (f2 == fb and f1 == tb):
            r, x, b_ch = branch[i, 2], branch[i, 3], branch[i, 4]
            Y11after[f1, f1] -= 1j * b_ch / 2.0 + 1.0 / (r + 1j * x)
            Y11after[f2, f2] -= 1j * b_ch / 2.0 + 1.0 / (r + 1j * x)

    Yaf = Y22 - Y21 @ np.linalg.inv(Y11after) @ Y12
    RV[:, :, 2] = -np.linalg.inv(Y11after) @ Y12

    # ─── Step 12: Pack YBUS and RV ───
    YBUS = np.zeros((n_gen, n_gen, 3), dtype=complex)
    YBUS[:, :, 0] = Ybf   # pre-fault
    YBUS[:, :, 1] = Ydf   # during-fault
    YBUS[:, :, 2] = Yaf   # post-fault

    # ─── Step 13: Initial conditions ───
    # Generator current injection
    Ig = np.conj(Sg / V[:n_gen])
    # Internal voltage (behind transient reactance)
    E0 = V[gen_bus] + Ig * (R + 1j * Xd)
    E_abs = np.abs(E0)

    # Initial state: rotor angles + zero speed deviation
    X_0 = np.concatenate([np.angle(E0), np.zeros(n_gen)])

    # Initial power flow → mechanical power = electrical power (steady state)
    I0 = Ybf @ E0
    PM = np.real(E0 * np.conj(I0))

    # ─── Step 14: Simulation parameters ───
    fs = 2000              # sampling frequency (Hz)
    total_time = 180.0     # total simulation time (3 minutes)
    num_samples = int(total_time * fs)
    deltt = 1.0 / fs       # time step

    t_SW = 5.0             # fault start time (s)
    t_FC = 5.3             # fault clear time (s)

    n = n_gen
    s = n_bus
    ns = 2 * n             # state dimension (2 angles + 2 speeds)
    nm = 2 * n + 2 * s     # measurement dimension (2PG + 2QG + 5Vmag + 5Vangle)

    # ─── Step 15: UKF parameters ───
    sig = 1e-2
    P_init = sig ** 2 * np.eye(ns)
    Q_mat = sig ** 2 * np.eye(ns)
    R_meas = sig ** 2 * np.eye(nm)

    X_hat = X_0.copy()
    W = np.ones((2 * ns, 1)) / (2 * ns)

    # ─── Step 16: Package system parameters ───
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
        'P': P_init,
        'Q_mat': Q_mat,
        'R_meas': R_meas,
        'sig': sig,
        'W': W,
        'gen_bus': gen_bus,
    }

    return system_params
