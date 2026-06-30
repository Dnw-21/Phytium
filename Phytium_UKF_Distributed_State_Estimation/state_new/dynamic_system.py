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
