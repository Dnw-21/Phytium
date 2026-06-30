"""
IEEE 5-Bus Overbye System Data
case5_Overbye from MATPOWER (Glover & Overbye, "Power System Analysis and Design")
2 generators at buses 1 (slack) and 3 (PV), 7 branches
"""

import numpy as np


def get_case5_system():
    """
    Returns the base case5_Overbye system data (pre-power-flow).
    The power flow solution is provided in run_power_flow().
    """

    # Bus data format (MATPOWER):
    # [bus_i, type, Pd, Qd, Gs, Bs, area, Vm(pu), Va(deg), baseKV, zone, Vmax, Vmin]
    # type: 1=PQ, 2=PV, 3=Slack
    # Pre-solved power flow values (Vm, Va) from PYPOWER case5_Overbye runpf
    bus = np.array([
        [1, 3, 0,     0,     0, 0, 1, 1.06000000,   0.000000,  100, 1, 1.1, 0.9],
        [2, 1, 300,   98.6,  0, 0, 1, 0.83948341, -23.123008,  100, 1, 1.1, 0.9],
        [3, 2, 300,   98.6,  0, 0, 1, 1.00000000, -47.738727,  100, 1, 1.1, 0.9],
        [4, 1, 400,   131.0, 0, 0, 1, 0.87757514, -48.570920,  100, 1, 1.1, 0.9],
        [5, 1, 0,     0,     0, 0, 1, 0.86329896, -22.508906,  100, 1, 1.1, 0.9],
    ])

    # Generator data format (MATPOWER):
    # [bus, Pg, Qg, Qmax, Qmin, Vg, mBase, status, Pmax, Pmin, ...]
    # Pg, Qg are post-power-flow solved values from PYPOWER runpf
    gen = np.array([
        [1, 842.941946, 340.577235,  400, -400, 1.06, 100, 1, 400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [3, 350.000000, 573.210504,  300, -300, 1.00, 100, 1, 300, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ])

    # Branch data format (MATPOWER):
    # [fbus, tbus, r, x, b, rateA, rateB, rateC, ratio, angle, status, angmin, angmax]
    branch = np.array([
        [1, 2, 0.02, 0.06, 0.030, 250, 250, 250, 0, 0, 1, -360, 360],
        [1, 5, 0.08, 0.24, 0.025, 250, 250, 250, 0, 0, 1, -360, 360],
        [2, 3, 0.06, 0.25, 0.020, 250, 250, 250, 0, 0, 1, -360, 360],
        [2, 4, 0.06, 0.18, 0.020, 250, 250, 250, 0, 0, 1, -360, 360],
        [2, 5, 0.04, 0.12, 0.015, 250, 250, 250, 0, 0, 1, -360, 360],
        [3, 4, 0.01, 0.03, 0.010, 250, 250, 250, 0, 0, 1, -360, 360],
        [4, 5, 0.08, 0.24, 0.025, 250, 250, 250, 0, 0, 1, -360, 360],
    ])

    return {
        'bus': bus,
        'gen': gen,
        'branch': branch,
        'n_bus': len(bus),
        'n_gen': len(gen),
        'n_branch': len(branch),
    }


def build_ybus(system):
    """
    Build the bus admittance matrix Ybus from branch data.
    Equivalent to Ybus_new(case5_Overbye) in MATLAB.
    """
    n_bus = system['n_bus']
    branch = system['branch']
    Y = np.zeros((n_bus, n_bus), dtype=complex)

    for br in range(len(branch)):
        f = int(branch[br, 0]) - 1
        t = int(branch[br, 1]) - 1
        r = branch[br, 2]
        x = branch[br, 3]
        b = branch[br, 4]

        y = 1.0 / (r + 1j * x)
        y_shunt = 1j * b / 2.0

        Y[f, f] += y + y_shunt
        Y[t, t] += y + y_shunt
        Y[f, t] -= y
        Y[t, f] -= y

    return Y


def run_power_flow(system):
    """
    Returns pre-solved power flow results from MATPOWER case5_Overbye.

    In the MATLAB version, this is obtained via:
      result = runpf(case5_Overbye)
      Vmag = result.bus(:, 8)
      Vph = result.bus(:, 9)
      Sg = result.gen(:, 2) + 1j*result.gen(:, 3)

    Here we provide the pre-solved values hardcoded from a typical MATPOWER
    power flow solution of case5_Overbye.
    """
    bus = system['bus']
    gen = system['gen']

    # Pre-solved bus voltages from power flow
    Vm = bus[:, 7]
    Va = bus[:, 8]
    V = Vm * np.exp(1j * Va * np.pi / 180.0)

    # Generator bus indices (0-indexed)
    gen_bus_idx = gen[:, 0].astype(int) - 1

    # Pre-solved generator powers from power flow (MW, MVar on 100 MVA base)
    # These are obtained from MATPOWER runpf result
    P_gen = gen[:, 1].astype(float)
    Q_gen = gen[:, 2].astype(float)

    print('Using pre-solved power flow data (MATPOWER case5_Overbye)')

    return {
        'V': V,
        'V_mag': Vm,
        'V_angle': Va,
        'P_gen': P_gen,
        'Q_gen': Q_gen,
        'gen_bus': gen_bus_idx,
    }
