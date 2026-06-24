"""
convert_params_5bus.py
Convert Python system_params.mat to C system_params.bin
"""
import numpy as np
from scipy.io import loadmat
import struct, os

script_dir = os.path.dirname(os.path.abspath(__file__))
py_dir = os.path.join(os.path.dirname(script_dir), 'DSE_Case5_Overbye_3min_Online')

data = loadmat(os.path.join(py_dir, 'system_params.mat'))

n  = int(data['n'])
s  = int(data['s'])
fs = int(data['fs'])
ns = 2 * n
nm = 2 * n + 2 * s
total_time = float(data['total_time'])
num_samples = int(total_time * fs)
deltt = 1.0 / fs
t_SW = float(data['t_SW'])
t_FC = float(data['t_FC'])

YBUS = data['YBUS']
RV   = data['RV']
E_abs = data['E_abs'].flatten()
PM    = data['PM'].flatten()
M     = data['M'].flatten()
D     = data['D'].flatten()
X_0   = data['X_0'].flatten()

print(f'Convert 5bus: n={n} s={s} ns={ns} nm={nm} fs={fs} samples={num_samples}')
print(f'  t_SW={t_SW} t_FC={t_FC} deltt={deltt}')

with open('system_params.bin', 'wb') as f:
    dims = np.array([n, s, ns, nm, fs, num_samples], dtype=np.int32)
    dims.tofile(f)
    np.array([deltt, t_SW, t_FC, 0.0], dtype=np.float64).tofile(f)
    for ps in range(3):
        YBUS[:,:,ps].flatten().astype(np.complex128).tofile(f)
    for ps in range(3):
        RV[:,:,ps].flatten().astype(np.complex128).tofile(f)
    E_abs.astype(np.float64).tofile(f)
    PM.astype(np.float64).tofile(f)
    M.astype(np.float64).tofile(f)
    D.astype(np.float64).tofile(f)
    X_0.astype(np.float64).tofile(f)

print(f'Written system_params.bin ({os.path.getsize("system_params.bin")} bytes)')
