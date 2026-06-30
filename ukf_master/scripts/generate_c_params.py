"""
generate_c_params.py
Convert system_params.mat to C-readable system_params.txt
"""
import numpy as np
from scipy.io import loadmat

import os
script_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(os.path.dirname(script_dir))  # DSE_Quantized_Measurement.m
mat_path = os.path.join(parent_dir, 'system_params.mat')
data = loadmat(mat_path)

n  = int(data['n'][0,0])
s  = int(data['s'][0,0])
fs = int(data['fs'][0,0])
ns = 2 * n
nm = 2 * n + 2 * s
num_samples = 80  # or from total_time * fs
deltt = 1.0 / fs
t_SW = float(data['t_SW'][0,0])
t_FC = float(data['t_FC'][0,0])

YBUS = data['YBUS']   # (n, n, 3)
RV   = data['RV']     # (s, n, 3)
E_abs = data['E_abs'].flatten()
PM    = data['PM'].flatten()
M     = data['M'].flatten()
D     = data['D'].flatten()
X_0   = data['X_0'].flatten()

out_path = os.path.join(os.path.dirname(script_dir), 'data', 'system_params.txt')
with open(out_path, 'w') as f:
    f.write(f'# n s fs ns nm num_samples deltt t_SW t_FC\n')
    f.write(f'{n} {s} {fs} {ns} {nm} {num_samples} {deltt} {t_SW} {t_FC}\n')

    for stage in range(3):
        f.write(f'# YBUS stage {stage} ({n}x{n} complex, real imag pairs)\n')
        for i in range(n):
            for j in range(n):
                v = YBUS[i, j, stage]
                f.write(f'{v.real:.16f} {v.imag:.16f}\n')

    for stage in range(3):
        f.write(f'# RV stage {stage} ({s}x{n} complex)\n')
        for i in range(s):
            for j in range(n):
                v = RV[i, j, stage]
                f.write(f'{v.real:.16f} {v.imag:.16f}\n')

    f.write(f'# E_abs ({n} values)\n')
    for v in E_abs: f.write(f'{v:.16f}\n')

    f.write(f'# PM ({n} values)\n')
    for v in PM: f.write(f'{v:.16f}\n')

    f.write(f'# M ({n} values)\n')
    for v in M: f.write(f'{v:.16f}\n')

    f.write(f'# D ({n} values)\n')
    for v in D: f.write(f'{v:.16f}\n')

    f.write(f'# X_hat_init ({ns} values)\n')
    for v in X_0: f.write(f'{v:.16f}\n')

print('Generated system_params.txt')
