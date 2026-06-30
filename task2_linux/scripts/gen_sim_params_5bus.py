#!/usr/bin/env python3
"""
gen_sim_params_5bus.py — 系统参数导出脚本
===========================================
从 2generators/initialize_system_5.py 获取系统参数,
导出为 FreeRTOS C 头文件 sim_params_5bus.h。

用法: python gen_sim_params_5bus.py [输出路径]
默认输出: /home/alientek/Phytium/freertos/inc/sim_params_5bus.h
"""

import sys, os
import numpy as np

# 添加 2generators 到 Python 路径
sys.path.insert(0, '/home/alientek/Phytium/2generators')
from initialize_system_5 import initialize_system_5


def format_float_array(name, arr, per_line=4):
    """将 numpy 数组格式化为 C 静态数组字符串"""
    arr = np.asarray(arr, dtype=np.float64).flatten()
    lines = []
    lines.append(f'static const float {name}[{len(arr)}] = {{')
    for i in range(0, len(arr), per_line):
        chunk = arr[i:i+per_line]
        line = '    ' + ', '.join(f'{v:.8f}f' for v in chunk)
        if i + per_line < len(arr):
            line += ','
        lines.append(line)
    lines.append('};')
    return '\n'.join(lines)


def format_complex_3d(name, arr):
    """
    将 3D 复数数组 (n_gen × n_gen × 3) 格式化为两个 C 数组 (real/imag).
    arr shape: (n_gen, n_gen, 3) dtype=complex
    """
    real = np.real(arr).astype(np.float64)
    imag = np.imag(arr).astype(np.float64)
    n0, n1, n2 = arr.shape
    total = n0 * n1 * n2

    lines = []
    lines.append(f'// {name} dimensions: [{n0}][{n1}][{n2}]')
    lines.append(f'static const float {name}_REAL[{n0}][{n1}][{n2}] = {{')
    for i in range(n0):
        lines.append(f'    {{  // [{i}]')
        for j in range(n1):
            vals = ', '.join(f'{real[i,j,k]:.12f}f' for k in range(n2))
            lines.append(f'        {{ {vals} }},')
        lines.append('    },')
    lines.append('};')
    lines.append('')
    lines.append(f'static const float {name}_IMAG[{n0}][{n1}][{n2}] = {{')
    for i in range(n0):
        lines.append(f'    {{  // [{i}]')
        for j in range(n1):
            vals = ', '.join(f'{imag[i,j,k]:.12f}f' for k in range(n2))
            lines.append(f'        {{ {vals} }},')
        lines.append('    },')
    lines.append('};')
    return '\n'.join(lines)


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else \
        '/home/alientek/Phytium/freertos/inc/sim_params_5bus.h'

    print(f'gen_sim_params_5bus.py — 系统参数导出')
    print(f'  源: 2generators/initialize_system_5.py')
    print(f'  输出: {output_path}')

    # 获取系统参数
    sp = initialize_system_5()

    YBUS = sp['YBUS']   # (2,2,3) complex
    RV   = sp['RV']     # (5,2,3) complex
    E_abs = sp['E_abs'] # (2,)  float
    PM   = sp['PM']     # (2,)  float
    M    = sp['M']      # (2,)  float
    D    = sp['D']      # (2,)  float
    X_0  = sp['X_0']    # (4,)  float
    n    = sp['n']      # 2
    s    = sp['s']      # 5
    ns   = sp['ns']     # 4
    nm   = sp['nm']     # 14
    fs   = sp['fs']     # 2000
    deltt = sp['deltt'] # 0.0005
    total_time = sp['total_time']  # 180
    num_samples = sp['num_samples'] # 360000
    t_SW = sp['t_SW']   # 5.0
    t_FC = sp['t_FC']   # 5.3
    gen_bus = sp['gen_bus']  # [0, 2]

    # 生成 C 头文件
    lines = []
    lines.append('/*')
    lines.append(' * sim_params_5bus.h — IEEE 5-Bus Overbye 系统参数')
    lines.append(' * 自动生成: gen_sim_params_5bus.py')
    lines.append(' * 请勿手动编辑!')
    lines.append(' */')
    lines.append('')
    lines.append('#ifndef SIM_PARAMS_5BUS_H')
    lines.append('#define SIM_PARAMS_5BUS_H')
    lines.append('')
    lines.append('/* ─── 系统维度 ─── */')
    lines.append(f'#define SIM_5BUS_N_GEN       {n}')
    lines.append(f'#define SIM_5BUS_N_BUS       {s}')
    lines.append(f'#define SIM_5BUS_N_STATE     {ns}')
    lines.append(f'#define SIM_5BUS_N_MEAS      {nm}')
    lines.append('')
    lines.append('/* ─── 仿真参数 ─── */')
    lines.append(f'#define SIM_5BUS_DT          {deltt:.6f}f')
    lines.append(f'#define SIM_5BUS_FS          {fs}')
    lines.append(f'#define SIM_5BUS_TOTAL_S     {total_time:.1f}f')
    lines.append(f'#define SIM_5BUS_NUM_STEPS   {num_samples}')
    lines.append(f'#define SIM_5BUS_FAULT_START {t_SW:.1f}f')
    lines.append(f'#define SIM_5BUS_FAULT_END   {t_FC:.1f}f')
    lines.append('')
    lines.append('/* ─── 发电机母线索引 (0-based) ─── */')
    lines.append(f'#define SIM_5BUS_GEN_BUS_0   {gen_bus[0]}')
    lines.append(f'#define SIM_5BUS_GEN_BUS_1   {gen_bus[1]}')
    lines.append('')
    lines.append('/* ─── 原始潮流解功率 (MW/MVar, baseMVA=100) ─── */')
    lines.append('/* 用于交叉验证, FreeRTOS 侧不使用 */')
    lines.append(f'#define SIM_5BUS_PGEN1_MW     842.9419f')
    lines.append(f'#define SIM_5BUS_QGEN1_MVAR    340.5772f')
    lines.append(f'#define SIM_5BUS_PGEN2_MW     350.0f')
    lines.append(f'#define SIM_5BUS_QGEN2_MVAR    573.2105f')
    lines.append('')

    # 降阶导纳矩阵 YBUS (2×2×3, complex)
    lines.append('/* ─── 降阶导纳矩阵 YBUS[2][2][3] ─── */')
    lines.append('/* 索引: [row][col][fault_state]  fault_state: 0=pre, 1=during, 2=post */')
    lines.append(format_complex_3d('SIM_5BUS_YBUS', YBUS))
    lines.append('')

    # 电压恢复矩阵 RV (5×2×3, complex)
    lines.append('/* ─── 电压恢复矩阵 RV[5][2][3] ─── */')
    lines.append('/* V_bus = RVm @ E_gen, 索引: [bus][gen][fault_state] */')
    lines.append(format_complex_3d('SIM_5BUS_RV', RV))
    lines.append('')

    # 标量参数
    lines.append('/* ─── 发电机参数 ─── */')
    lines.append(format_float_array('SIM_5BUS_E_ABS', E_abs))
    lines.append(format_float_array('SIM_5BUS_PM', PM))
    lines.append(format_float_array('SIM_5BUS_M', M))
    lines.append(format_float_array('SIM_5BUS_D', D))
    lines.append('')

    lines.append('/* ─── 初始状态 X_0 = [δ₁, δ₂, ω₁, ω₂] ─── */')
    lines.append(format_float_array('SIM_5BUS_X0', X_0))
    lines.append('')

    lines.append('#endif /* SIM_PARAMS_5BUS_H */')
    lines.append('')

    content = '\n'.join(lines)

    # 写入文件
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(content)

    print(f'✅ 已生成 {output_path}')
    print(f'   系统: {n} 发电机, {s} 母线')
    print(f'   状态维度: {ns}, 测量维度: {nm}')
    print(f'   仿真: {total_time}s @ {fs}Hz = {num_samples} 步')
    print(f'   故障: t={t_SW}s ~ {t_FC}s')


if __name__ == '__main__':
    main()
