"""
terminal_node.py — Measurement Data Generation
================================================
Exact Python port of Generate_ZData_Adaptive.m
IEEE 9-Bus, 3-Generator (case9_new_Sauer)
1000Hz, 80 samples (60 normal + 20 fault)
Fault: Bus 8, Line 8-9 (t=0.060s ~ 0.080s)
Measurements rounded to 4 decimal places.
sigma = 1e-2 for all states (matching MATLAB).
================================================
"""
import numpy as np
from scipy.io import savemat

# ============================================================
# 9-Bus System Data (pre-solved power flow, matches runpf(case9_new_Sauer))
# ============================================================
baseMVA = 100.0
f0 = 60.0
w_syn = 2.0 * np.pi * f0

bus = np.array([
    [1,  3,   0,   0, 0, 0, 1, 1.040,  0.000, 345, 1, 1.1, 0.9],
    [2,  2,   0,   0, 0, 0, 1, 1.025,  9.280, 345, 1, 1.1, 0.9],
    [3,  2,   0,   0, 0, 0, 1, 1.025,  4.665, 345, 1, 1.1, 0.9],
    [4,  1,   0,   0, 0, 0, 1, 1.026, -2.217, 345, 1, 1.1, 0.9],
    [5,  1, 125,  50, 0, 0, 1, 0.996, -3.989, 345, 1, 1.1, 0.9],
    [6,  1,  90,  30, 0, 0, 1, 1.013, -3.687, 345, 1, 1.1, 0.9],
    [7,  1,   0,   0, 0, 0, 1, 1.026,  3.720, 345, 1, 1.1, 0.9],
    [8,  1, 100,  35, 0, 0, 1, 1.016,  0.728, 345, 1, 1.1, 0.9],
    [9,  1,   0,   0, 0, 0, 1, 1.032,  1.967, 345, 1, 1.1, 0.9],
])

gen = np.array([
    [1,  71.64,  27.05, 300, -300, 1.040, 100, 1, 250, 10, 0,0,0,0,0,0,0,0,0,0,0],
    [2, 163.00,   6.65, 300, -300, 1.025, 100, 1, 300, 10, 0,0,0,0,0,0,0,0,0,0,0],
    [3,  85.00, -10.86, 300, -300, 1.025, 100, 1, 270, 10, 0,0,0,0,0,0,0,0,0,0,0],
])

branch = np.array([
    [1, 4, 0.0000, 0.0576, 0.000, 250, 250, 250, 0, 0, 1, -360, 360],
    [4, 6, 0.0170, 0.0920, 0.158, 250, 250, 250, 0, 0, 1, -360, 360],
    [6, 9, 0.0390, 0.1700, 0.358, 150, 150, 150, 0, 0, 1, -360, 360],
    [3, 9, 0.0000, 0.0586, 0.000, 300, 300, 300, 0, 0, 1, -360, 360],
    [9, 8, 0.0119, 0.1008, 0.209, 150, 150, 150, 0, 0, 1, -360, 360],
    [8, 7, 0.0085, 0.0720, 0.149, 250, 250, 250, 0, 0, 1, -360, 360],
    [7, 2, 0.0000, 0.0625, 0.000, 250, 250, 250, 0, 0, 1, -360, 360],
    [7, 5, 0.0320, 0.1610, 0.306, 250, 250, 250, 0, 0, 1, -360, 360],
    [5, 4, 0.0100, 0.0850, 0.176, 250, 250, 250, 0, 0, 1, -360, 360],
])

n_bus = bus.shape[0]
n_gen = gen.shape[0]
gen_bus = gen[:, 0].astype(int) - 1

# ---- Machine parameters ----
Xd = np.array([0.06080, 0.11980, 0.18130])
R_gen = np.array([0.0, 0.0, 0.0])
H = np.array([23.64, 6.40, 3.01])
D = np.array([0.0255, 0.00663, 0.00265])
M = 2.0 * H / w_syn

# ---- Pre-solved power flow voltages ----
V = bus[:, 7] * np.exp(1j * bus[:, 8] * np.pi / 180)
P_gen = gen[:, 1]
Q_gen = gen[:, 2]

# ---- Build Ybus ----
Y = np.zeros((n_bus, n_bus), dtype=complex)
for br in range(branch.shape[0]):
    f = int(branch[br, 0]) - 1; t = int(branch[br, 1]) - 1
    r = branch[br, 2]; x = branch[br, 3]; b = branch[br, 4]
    y = 1.0 / (r + 1j * x); y_shunt = 1j * b / 2.0
    Y[f, f] += y + y_shunt; Y[t, t] += y + y_shunt
    Y[f, t] -= y; Y[t, f] -= y

# ---- Network reduction ----
Sg = (P_gen + 1j * Q_gen) / baseMVA
Y22 = np.diag(1.0 / (1j * Xd))
SL = (bus[:, 2] + 1j * bus[:, 3]) / baseMVA
YL = np.conj(SL) / (np.abs(V) ** 2)
Y11 = Y + np.diag(YL)
Y11[np.ix_(gen_bus, gen_bus)] += Y22

Y12 = np.zeros((n_bus, n_gen), dtype=complex)
for i in range(n_bus):
    for k in range(n_gen):
        q = int(gen_bus[k])
        if i == q: Y12[q, k] = -1.0 / (R_gen[k] + 1j * Xd[k])
Y21 = Y12.T

# Pre-fault
Ybf = Y22 - Y21 @ np.linalg.inv(Y11) @ Y12
RV = np.zeros((n_bus, n_gen, 3), dtype=complex)
RV[:, :, 0] = -np.linalg.inv(Y11) @ Y12

# Fault: Bus 8, Line [8, 9] (matching Generate_ZData_Adaptive.m)
f11 = 8 - 1; F = np.array([8, 9]); f1 = F[0] - 1; f2 = F[1] - 1

Y11df = np.delete(np.delete(Y11, f11, axis=0), f11, axis=1)
Y12df = np.delete(Y12, f11, axis=0); Y21df = Y12df.T
Ydf = Y22 - Y21df @ np.linalg.inv(Y11df) @ Y12df
RV[:, :, 1] = np.zeros_like(RV[:, :, 0])
idx_set = np.setdiff1d(np.arange(n_bus), f11)
Rtmp = -np.linalg.inv(Y11df) @ Y12df
for ri, bus_i in enumerate(idx_set): RV[bus_i, :, 1] = Rtmp[ri, :]

Y11after = Y11.copy(); Y11after[f1, f2] = 0; Y11after[f2, f1] = 0
for i in range(branch.shape[0]):
    fb = int(branch[i, 0]) - 1; tb = int(branch[i, 1]) - 1
    if (f1 == fb and f2 == tb) or (f2 == fb and f1 == tb):
        r, x, b_ch = branch[i, 2], branch[i, 3], branch[i, 4]
        branch_admit = 1.0 / (r + 1j * x)
        Y11after[f1, f1] -= 1j * b_ch / 2.0 + branch_admit
        Y11after[f2, f2] -= 1j * b_ch / 2.0 + branch_admit
Yaf = Y22 - Y21 @ np.linalg.inv(Y11after) @ Y12
RV[:, :, 2] = -np.linalg.inv(Y11after) @ Y12

YBUS = np.zeros((n_gen, n_gen, 3), dtype=complex)
YBUS[:, :, 0] = Ybf; YBUS[:, :, 1] = Ydf; YBUS[:, :, 2] = Yaf

# Initial state
Ig = np.conj(Sg / V[:n_gen])
E0 = V[gen_bus] + Ig * (R_gen + 1j * Xd)
E_abs = np.abs(E0)
X_0 = np.concatenate([np.angle(E0), np.zeros(n_gen)])
I0 = Ybf @ E0; PM = np.real(E0 * np.conj(I0))

# ---- Sampling (matching Generate_ZData_Adaptive.m) ----
fs = 1000
num_normal = 60; num_fault = 20
deltt = 1.0 / fs
t_SW = num_normal / fs       # 0.060s
t_FC = (num_normal + num_fault) / fs  # 0.080s
num_samples = num_normal + num_fault

ns = 2 * n_gen; nm = 2 * n_gen + 2 * n_bus

print(f'System: {n_gen} gen x {n_bus} bus, {ns} states, {nm} meas')
print(f'Sampling: {fs}Hz, {num_normal} normal + {num_fault} fault = {num_samples} steps')
print(f'Fault: Bus 8, Line 8-9, t={t_SW:.3f}s ~ {t_FC:.3f}s')
print(f'E_abs: {np.array2string(E_abs, precision=6)}')
print(f'delta0(deg): {np.array2string(np.angle(E0)*180/np.pi, precision=4)}')
print(f'D: {D}')
print(f'sigma = 1e-2 (all states)')

# ---- Dynamics ----
def dynamic_system(t, x, M, D, Ybusm, E_abs, PM, n):
    Vg = E_abs * np.exp(1j * x[:n]); Ibus = Ybusm @ Vg
    Pe = np.real(np.conj(Ibus) * Vg)
    dx = np.zeros(2*n); dx[:n] = x[n:2*n]
    dx[n:2*n] = (PM - Pe) / M - D * x[n:2*n] / M
    return dx

def rk4_step(t, x, dt, M, D, Ybusm, E_abs, PM, n):
    k1 = dt * dynamic_system(t, x, M, D, Ybusm, E_abs, PM, n)
    k2 = dt * dynamic_system(t+dt/2, x+k1/2, M, D, Ybusm, E_abs, PM, n)
    k3 = dt * dynamic_system(t+dt/2, x+k2/2, M, D, Ybusm, E_abs, PM, n)
    k4 = dt * dynamic_system(t+dt, x+k3, M, D, Ybusm, E_abs, PM, n)
    return x + (k1 + 2*k2 + 2*k3 + k4) / 6

def get_ps(k):
    if k < t_SW: return 0
    elif k <= t_FC: return 1
    else: return 2

# ---- Simulate ----
print('\nSimulating...')
X_true = np.zeros((ns, num_samples)); Z_mes = np.zeros((nm, num_samples))
X_sim = X_0.copy()
for idx in range(num_samples):
    k = idx / fs; ps = get_ps(k)
    Ybusm = YBUS[:, :, ps]; RVm = RV[:, :, ps]
    X_sim = rk4_step(k, X_sim, deltt, M, D, Ybusm, E_abs, PM, n_gen)
    X_true[:, idx] = X_sim
    E = E_abs * np.exp(1j * X_sim[:n_gen]); I = Ybusm @ E; V_bus = RVm @ E
    Z_mes[:n_gen, idx] = np.real(E * np.conj(I))
    Z_mes[n_gen:2*n_gen, idx] = np.imag(E * np.conj(I))
    Z_mes[2*n_gen:2*n_gen+n_bus, idx] = np.real(V_bus)
    Z_mes[2*n_gen+n_bus:2*n_gen+2*n_bus, idx] = np.imag(V_bus)
Z_rounded = np.round(Z_mes, 4)
print(f'Done. Z range: [{Z_rounded.min():.4f}, {Z_rounded.max():.4f}]')

# ---- Save system_params.mat ----
sig = 1e-2  # matching MATLAB Generate_ZData_Adaptive.m
P = sig**2 * np.eye(ns)
Q_mat = sig**2 * np.eye(ns)
R_meas = sig**2 * np.eye(nm)

mat_dict = {
    'YBUS': YBUS, 'RV': RV, 'E_abs': E_abs, 'PM': PM, 'M': M, 'D': D,
    'n': np.array([n_gen]), 's': np.array([n_bus]),
    'fs': np.array([fs]), 't_SW': np.array([t_SW]), 't_FC': np.array([t_FC]),
    'total_time': np.array([num_samples/fs]),
    'X_0': X_0, 'sig': np.array([sig]),
    'sig_angle': np.array([sig]), 'sig_speed': np.array([sig]), 'sig_meas': np.array([sig]),
    'P': P, 'Q_mat': Q_mat, 'R_meas': R_meas,
    'gen_bus': gen_bus,
}
savemat('system_params.mat', mat_dict)
print('Saved system_params.mat')

# ---- Save true_states.csv ----
hdr = ','.join([f'delta{i+1}' for i in range(n_gen)] + [f'omega{i+1}' for i in range(n_gen)])
np.savetxt('true_states.csv', X_true.T, delimiter=',', header=hdr, comments='', fmt='%.8f')
print(f'Saved true_states.csv ({num_samples} pts)')

# ---- Save measurements.txt (4dp) ----
cols = ([f'PG{i+1}' for i in range(n_gen)] + [f'QG{i+1}' for i in range(n_gen)]
        + [f'Vreal{i+1}' for i in range(n_bus)] + [f'Vimag{i+1}' for i in range(n_bus)])
with open('measurements.txt', 'w') as f:
    f.write('timestamp,' + ','.join(cols) + '\n')
    for i in range(num_samples):
        row = [f'{i/fs:.6f}'] + [f'{Z_rounded[j,i]:.4f}' for j in range(nm)]
        f.write(','.join(row) + '\n')
print(f'Saved measurements.txt ({num_samples} rows x {nm+1} cols, 4dp)')

# ---- Save system_params.bin (for C controller) ----
import struct
dim_header = np.array([n_gen, n_bus, ns, nm, fs, num_samples], dtype=np.int32)
scalars = np.array([deltt, t_SW, t_FC, 0.0], dtype=np.float64)
with open('system_params.bin', 'wb') as f:
    dim_header.tofile(f); scalars.tofile(f)
    for ps in range(3): YBUS[:,:,ps].flatten().astype(np.complex128).tofile(f)
    for ps in range(3): RV[:,:,ps].flatten().astype(np.complex128).tofile(f)
    E_abs.astype(np.float64).tofile(f); PM.astype(np.float64).tofile(f)
    M.astype(np.float64).tofile(f); D.astype(np.float64).tofile(f)
    X_0.astype(np.float64).tofile(f)
print('Saved system_params.bin (for C controller)')

print('\n=== All files generated ===')
print('  system_params.mat   - Python controller input')
print('  system_params.bin   - C controller input')
print('  measurements.txt    - 80 rows, 4dp (LoRa transmission)')
print('  true_states.csv     - true states for verification')
