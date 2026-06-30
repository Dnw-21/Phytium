"""
convert_params.py
Convert Python system_params.mat to C system_params.bin
Uses Python's measurements.txt directly.
"""
import numpy as np
from scipy.io import loadmat
import struct, os

script_dir = os.path.dirname(os.path.abspath(__file__))
py_dir = os.path.join(os.path.dirname(script_dir), 'python')

data = loadmat(os.path.join(py_dir, 'system_params.mat'))

n  = int(data['n'])
s  = int(data['s'])
fs = int(data['fs'])
ns = 2 * n
nm = 2 * n + 2 * s
total_time = int(data['total_time'])
num_samples = int(total_time * fs)
deltt = 1.0 / fs
t_SW = float(data['t_SW'])
t_FC = float(data['t_FC'])

YBUS = data['YBUS']  # (3,3,3)
RV   = data['RV']    # (9,3,3)
E_abs = data['E_abs'].flatten()
PM    = data['PM'].flatten()
M     = data['M'].flatten()
D     = data['D'].flatten()
X_0   = data['X_0'].flatten()

print(f'Convert: n={n} s={s} ns={ns} nm={nm} fs={fs} samples={num_samples}')
print(f'  t_SW={t_SW} t_FC={t_FC} deltt={deltt}')
print(f'  E_abs={E_abs}')
print(f'  PM={PM}')
print(f'  M={M}')
print(f'  D={D}')
print(f'  X_0={X_0}')

with open('system_params.bin', 'wb') as f:
    # Header: 6 ints
    dims = np.array([n, s, ns, nm, fs, num_samples], dtype=np.int32)
    dims.tofile(f)
    # Scalars: 4 doubles (deltt, t_SW, t_FC, unused)
    np.array([deltt, t_SW, t_FC, 0.0], dtype=np.float64).tofile(f)
    # YBUS: 3 stages of n*n complex
    for ps in range(3):
        YBUS[:,:,ps].flatten().astype(np.complex128).tofile(f)
    # RV: 3 stages of s*n complex
    for ps in range(3):
        RV[:,:,ps].flatten().astype(np.complex128).tofile(f)
    # Vectors
    E_abs.astype(np.float64).tofile(f)
    PM.astype(np.float64).tofile(f)
    M.astype(np.float64).tofile(f)
    D.astype(np.float64).tofile(f)
    X_0.astype(np.float64).tofile(f)

print(f'Written system_params.bin ({os.path.getsize("system_params.bin")} bytes)')

# Also copy Python's measurements.txt
import shutil
src = os.path.join(py_dir, 'measurements.txt')
dst = 'measurements.txt'
shutil.copy(src, dst)
print(f'Copied {src} -> {dst}')

# Also copy true_states.csv
src2 = os.path.join(py_dir, 'true_states.csv')
dst2 = 'true_states.csv'
if os.path.exists(src2):
    shutil.copy(src2, dst2)
    print(f'Copied {src2} -> {dst2}')
