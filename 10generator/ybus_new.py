import numpy as np

def ybus_new(bus, branch):
    nb = bus.shape[0]
    Y = np.zeros((nb, nb), dtype=np.complex128)

    for i in range(nb):
        for j in range(nb):
            if i == j:
                a = np.where(branch[:, 0] == i + 1)[0]
                b_idx = np.where(branch[:, 1] == i + 1)[0]
                for k in a:
                    r, x, b_ch = branch[k, 2], branch[k, 3], branch[k, 4]
                    Y[i, j] += 1.0 / (r + 1j * x) + 1j * b_ch / 2.0
                for k in b_idx:
                    r, x, b_ch = branch[k, 2], branch[k, 3], branch[k, 4]
                    Y[i, j] += 1.0 / (r + 1j * x) + 1j * b_ch / 2.0
            else:
                c = np.where(branch[:, 0] == i + 1)[0]
                d = np.where(branch[:, 1] == j + 1)[0]
                for m in c:
                    for n_idx in d:
                        if m == n_idx:
                            r, x = branch[m, 2], branch[m, 3]
                            Y[i, j] -= 1.0 / (r + 1j * x)
                Y[j, i] = Y[i, j]

    return Y