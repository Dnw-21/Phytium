"""Online UKF for 5-Bus 2-Generator — reads measurements one at a time."""
import sys, numpy as np
from scipy.io import loadmat
from ukf_core_5 import UKFState, ukf_init, ukf_step

def main():
    data = loadmat('system_params.mat')
    n=int(data['n'][0,0]); s=int(data['s'][0,0]); fs=int(data['fs'][0,0]); ns=2*n; nm=2*n+2*s
    t_SW=float(data['t_SW'][0,0]); t_FC=float(data['t_FC'][0,0])
    sig=float(data['sig'][0,0])
    sp = {'YBUS':data['YBUS'],'RV':data['RV'],'E_abs':data['E_abs'].flatten(),
          'PM':data['PM'].flatten(),'M':data['M'].flatten(),'D':data['D'].flatten(),
          'n':n,'s':s,'ns':ns,'nm':nm,'fs':fs,'deltt':1.0/fs,'t_SW':t_SW,'t_FC':t_FC,
          'P':sig**2*np.eye(ns),'Q_mat':sig**2*np.eye(ns),'R_meas':sig**2*np.eye(nm),
          'W':np.ones((2*ns,1))/(2*ns),'X_hat':data['X_0'].flatten()}

    inp = open(sys.argv[1]) if len(sys.argv)>1 else sys.stdin
    sys.stderr.write(f'[Init] 5-Bus 2-Gen, ns={ns} nm={nm} fs={fs}Hz\n')
    sys.stderr.write(f'[Init] Fault: Bus 2, {t_SW}s~{t_FC}s\n')

    st = UKFState(); ukf_init(sp, st)
    sys.stderr.write('[Init] UKF ready.\n\n')

    hdr = 'time'+''.join(f',delta{i+1}' for i in range(n))+''.join(f',omega{i+1}' for i in range(n))+',RMSE'
    print(f'# {hdr}  fault_tSW={t_SW:.4f}  fault_tFC={t_FC:.4f}')
    sys.stdout.flush()

    cnt = 0
    for line in inp:
        line=line.strip()
        if not line or line.startswith('#') or line.startswith('time'): continue
        parts=line.split(',')
        z=np.zeros(nm); kt=cnt/fs
        try:
            ts=float(parts[0])
            if ts<10000: kt=ts; stt=1
            else: stt=0
        except ValueError: stt=0
        for j in range(nm): z[j]=float(parts[stt+j])
        xo,ro=ukf_step(st,z,kt)
        vals=','.join(f'{xo[i]:.8f}' for i in range(ns))
        print(f'{kt:.6f},{vals},{ro:.8f}'); sys.stdout.flush()
        cnt+=1
        if cnt%50000==0: sys.stderr.write(f'[Stream] {cnt} (t={kt:.1f}s)\n')

    sys.stderr.write(f'\n[Done] {cnt} measurements.\n')
    if len(sys.argv)>1: inp.close()

if __name__=='__main__': main()
