#!/usr/bin/env python3
"""
三节点 UKF 数据构建器 — 使用真实 DSE 目录数据
==============================================
直接读取三套真实的 system_params.bin + measurements.txt:
  Z1: DSE_Adaptive_1000Hz_80ms/          → node_1 (Bus 8, Line 8-9)
  Z2: DSE_Z2_Adaptive_1000Hz_80ms/...    → node_2 (Bus 4, Line 4-5)
  Z3: Generate_Z3Data_Adaptive.m/        → node_3 (Bus 3, Line 3-9)

只修改 t_SW/t_FC 实现不同的故障窗口，其余数据不动。
"""

import os, sys, subprocess, struct, csv, shutil

# ── 路径配置 ──────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DSE_BASE = '/home/alientek/Phytium/dy_3gen'
TOOL_DIR = os.path.join(BASE_DIR, 'tools')
DATA_DIR = os.path.join(BASE_DIR, 'data')

# 三个节点的源目录
NODE_SRC = {
    'node_1': os.path.join(DSE_BASE, 'DSE_Adaptive_1000Hz_80ms'),
    'node_2': os.path.join(DSE_BASE, 'DSE_Z2_Adaptive_1000Hz_80ms',
                           'DSE_Z2_Adaptive_1000Hz_80ms'),
    'node_3': os.path.join(DSE_BASE, 'Generate_Z3Data_Adaptive.m'),
}

# UKF 源码（只在 Z1 目录下有）
UKF_SRC_DIR = NODE_SRC['node_1']

# ── 故障窗口：每个节点不同触发时刻 ──────────────────────────
# ── 故障时间（两种尺度） ──────────────────────────────────
# FAULT_TUNING: kHz 时间用于 system_params.bin + measurement 扰动
# FAULT_STEPS:  step 序号用于 dashboard is_fault 标志
FS = 1000
FAULT_TUNING = {
    'node_1': {'t_SW': 0.010, 't_FC': 0.013},   # UKF 看到故障 = 0.010-0.013s
    'node_2': {'t_SW': 0.015, 't_FC': 0.018},
    'node_3': {'t_SW': 0.020, 't_FC': 0.023},
}
FAULT_STEPS = {
    'node_1': {'start': 10, 'end': 13},          # dashboard step 10-12
    'node_2': {'start': 15, 'end': 18},
    'node_3': {'start': 20, 'end': 23},
}

# 每个节点故障时注入扰动的母线电压 (Vreal 列索引 6-14, Vimag 列索引 15-23)
# 不同节点扰动不同母线，保证 UKF 估计分化
FAULT_INJECT = {
    'node_1': {'vreal_idx': 7,  'vimag_idx': 16, 'scale': 0.75},   # Bus 2 电压骤降 25%
    'node_2': {'vreal_idx': 9,  'vimag_idx': 18, 'scale': 0.70},   # Bus 4 电压骤降 30%  
    'node_3': {'vreal_idx': 13, 'vimag_idx': 22, 'scale': 0.65},   # Bus 8 电压骤降 35%
}

# ═══════════════════════════════════════════════════════════
# 编译
# ═══════════════════════════════════════════════════════════

def compile_controller_online():
    src = os.path.join(UKF_SRC_DIR, 'controller_online.c')
    bin_x86 = os.path.join(TOOL_DIR, 'controller_online')
    bin_arm = os.path.join(TOOL_DIR, 'controller_online.arm')

    os.makedirs(TOOL_DIR, exist_ok=True)

    ret = os.system(f'gcc -O2 -std=c99 -o {bin_x86} {src} -lm')
    if ret != 0:
        print('ERROR: x86_64 编译失败')
        return None
    print(f'[OK] x86_64: {bin_x86}')

    ret = os.system(f'aarch64-linux-gnu-gcc -O2 -std=c99 -o {bin_arm} {src} -lm')
    if ret != 0:
        print('[WARN] ARM64 交叉编译失败（开发板需单独编译）')
    else:
        print(f'[OK] aarch64: {bin_arm}')

    return bin_x86


# ═══════════════════════════════════════════════════════════
# system_params.bin 补丁
# ═══════════════════════════════════════════════════════════

def patch_sw_fc(bin_path, t_SW, t_FC, output_path):
    """修改 system_params.bin 中的 t_SW 和 t_FC 值

    system_params.bin 格式 (terminal_node.py):
      dim_header (int32×6): [n_gen, n_bus, ns, nm, fs, num_samples]
      scalars (float64×4): [deltt, t_SW, t_FC, 0.0]
      YBUS[0..2] (complex128 × n_gen² each)
      RV[0..2] (complex128 × n_bus×n_gen each)
      E_abs, PM, M, D (float64 × n_gen each)
      X_0 (float64 × ns)

    t_SW 在字节偏移 32 (= 6×4 + 1×8)
    t_FC 在字节偏移 40 (= 6×4 + 2×8)
    """
    with open(bin_path, 'rb') as f:
        data = bytearray(f.read())

    # 读头部验证
    n_gen = struct.unpack_from('i', data, 0)[0]
    n_bus = struct.unpack_from('i', data, 4)[0]
    ns = struct.unpack_from('i', data, 8)[0]
    nm = struct.unpack_from('i', data, 12)[0]
    fs_val = struct.unpack_from('i', data, 16)[0]
    num_samp = struct.unpack_from('i', data, 20)[0]
    old_sw = struct.unpack_from('d', data, 32)[0]
    old_fc = struct.unpack_from('d', data, 40)[0]

    print(f'    系统: {n_gen}gen×{n_bus}bus, ns={ns}, nm={nm}, '
          f'fs={fs_val}, N={num_samp}')
    print(f'    原始: t_SW={old_sw:.4f}, t_FC={old_fc:.4f}')
    print(f'    修改: t_SW={t_SW:.4f}, t_FC={t_FC:.4f}')

    # 打补丁
    struct.pack_into('d', data, 32, t_SW)
    struct.pack_into('d', data, 40, t_FC)

    with open(output_path, 'wb') as f:
        f.write(data)


# ═══════════════════════════════════════════════════════════
# UKF 运行
# ═══════════════════════════════════════════════════════════

def run_ukf(ukf_bin, params_path, meas_path, output_csv):
    """运行 controller_online，测量数据从 stdin 管道输入"""
    work_dir = os.path.dirname(params_path)
    target_params = os.path.join(work_dir, 'system_params.bin')

    # 复制 params 到工作目录（如果不在同一位置）
    if os.path.abspath(params_path) != os.path.abspath(target_params):
        shutil.copy2(params_path, target_params)

    with open(meas_path, 'r') as fin:
        proc = subprocess.run(
            [ukf_bin],
            stdin=fin, capture_output=True, text=True,
            cwd=work_dir,
            env={**dict(os.environ), 'LD_LIBRARY_PATH': ''},
        )

    if proc.returncode != 0:
        print(f'  ERROR: UKF 返回 {proc.returncode}')
        print(f'  stderr: {proc.stderr[:200]}')
        return []

    lines = proc.stdout.strip().split('\n')
    if len(lines) < 2:
        print(f'  ERROR: UKF 输出为空')
        return []

    results = []
    reader = csv.DictReader(lines)
    for row in reader:
        results.append({
            'time_sec': float(row.get('time', 0)),
            'delta1': float(row.get('delta1', 0)),
            'delta2': float(row.get('delta2', 0)),
            'delta3': float(row.get('delta3', 0)),
            'omega1': float(row.get('omega1', 0)),
            'omega2': float(row.get('omega2', 0)),
            'omega3': float(row.get('omega3', 0)),
            'RMSE': float(row.get('RMSE', 0)),
        })

    with open(output_csv, 'w') as fout:
        fout.write(proc.stdout)

    print(f'  UKF 估计 → {output_csv} ({len(results)} 行)')
    return results


# ═══════════════════════════════════════════════════════════
# 故障快照构建
# ═══════════════════════════════════════════════════════════

def _build_fault_snapshots(steps):
    """为每个节点的故障窗口生成回放快照 (±5 steps 窗口)"""
    node_labels = {'node_1': '节点1 (G1)', 'node_2': '节点2 (G2)', 'node_3': '节点3 (G3)'}
    snapshots = []
    for nid in ['node_1', 'node_2', 'node_3']:
        # 找到该节点的故障窗口
        fault_indices = [i for i, s in enumerate(steps)
                        if s['nodes'][nid].get('is_fault')]
        if not fault_indices:
            continue
        f_start, f_end = fault_indices[0], fault_indices[-1]
        # 前后各扩展 5 步
        win_start = max(0, f_start - 5)
        win_end = min(len(steps) - 1, f_end + 5)
        cen = (f_start + f_end) / 2  # 步数，1 step = 1 秒

        de = [[], [], []]
        oe = [[], [], []]
        times = []
        for i in range(win_start, win_end + 1):
            nd = steps[i]['nodes'][nid]
            # 1 step = 1 仿真秒
            times.append(float(i))
            for g in range(3):
                de[g].append(float(nd['delta_est'][g]))
                oe[g].append(float(nd['omega_est'][g]))

        snapshots.append({
            'id': f'{node_labels[nid]} · step {f_start}-{f_end}',
            'node_id': nid,
            'center_sec': cen,  # 仿真秒数
            'delta_est': de,
            'omega_est': oe,
            'times': times,
        })

    return snapshots


# ═══════════════════════════════════════════════════════════
# 主流程
# ═══════════════════════════════════════════════════════════

def main():
    print("=" * 60)
    print("三节点 UKF 数据构建器 - 真实 DSE 数据版")
    print("=" * 60)

    os.makedirs(DATA_DIR, exist_ok=True)

    # 1. 编译
    ukf_bin = compile_controller_online()
    if not ukf_bin:
        sys.exit(1)

    # 2. 为每个节点准备数据 + 运行 UKF
    node_estimates = {}
    for node_id in ['node_1', 'node_2', 'node_3']:
        print(f"\n{'─'*50}")
        print(f"  {node_id}")
        fc = FAULT_TUNING[node_id]
        src_dir = NODE_SRC[node_id]
        print(f"    源目录: {src_dir}")

        # 工作目录（隔离）
        work_dir = os.path.join(DATA_DIR, node_id)
        os.makedirs(work_dir, exist_ok=True)

        # 2a. system_params.bin（打补丁设置故障窗口）
        src_params = os.path.join(src_dir, 'system_params.bin')
        use_params = os.path.join(work_dir, 'system_params.bin')

        patch_sw_fc(src_params, fc['t_SW'], fc['t_FC'], use_params)
        fstep = FAULT_STEPS[node_id]
        print(f'    故障窗口: t_SW={fc["t_SW"]:.3f}s, t_FC={fc["t_FC"]:.3f}s '
              f'(step {fstep["start"]}-{fstep["end"]-1})')

        # 2b. 复制 measurements.txt 并注入故障扰动
        src_meas = os.path.join(src_dir, 'measurements.txt')
        dst_meas = os.path.join(work_dir, 'measurements.txt')

        # 注入故障：在故障窗口内扰动特定母线电压
        inject = FAULT_INJECT.get(node_id, None)
        with open(src_meas, 'r') as fin:
            lines = fin.readlines()
        header = lines[0]
        data_lines = lines[1:]
        num_lines = len(data_lines)
        modified = 0
        with open(dst_meas, 'w') as fout:
            fout.write(header)
            for i, line in enumerate(data_lines):
                t = i / FS  # time in seconds
                if inject and fc['t_SW'] <= t < fc['t_FC']:
                    parts = line.strip().split(',')
                    # 扰动 Vreal 和 Vimag 的指定列（模拟故障电压跌落）
                    parts[inject['vreal_idx']] = str(
                        float(parts[inject['vreal_idx']]) * inject['scale'])
                    parts[inject['vimag_idx']] = str(
                        float(parts[inject['vimag_idx']]) * inject['scale'])
                    line = ','.join(parts) + '\n'
                    modified += 1
                fout.write(line)
        print(f'    measurements.txt: {num_lines} 行, 故障扰动 {modified} 行')

        # 2c. 运行 UKF
        output_csv = os.path.join(DATA_DIR, f'estimates_{node_id}.csv')
        results = run_ukf(ukf_bin, use_params, dst_meas, output_csv)

        if not results:
            print(f'  ERROR: {node_id} UKF 无输出！')
            sys.exit(1)

        node_estimates[node_id] = results

    # 3. 构建 dashboard_data.json
    print(f"\n{'='*60}")
    print("构建 dashboard_data.json...")
    num_samples = max(len(v) for v in node_estimates.values())

    steps = []
    for i in range(num_samples):
        frame = {
            'time_sec': float(i),  # 1 step = 1 仿真秒
            'step': i,
            'fault_phase': 'normal',
            'is_fault': False,
            'nodes': {}
        }
        for nid in ['node_1', 'node_2', 'node_3']:
            est = node_estimates[nid]
            if i < len(est):
                e = est[i]
                de = [e['delta1'], e['delta2'], e['delta3']]
                oe = [e['omega1'], e['omega2'], e['omega3']]
                rmse = e['RMSE']
            else:
                de = [0, 0, 0]
                oe = [0, 0, 0]
                rmse = 0

            # dashboard 故障标志 (按 step 序号)
            fs = FAULT_STEPS.get(nid, {'start': -1, 'end': -1})
            node_fault = fs['start'] <= i < fs['end']

            frame['nodes'][nid] = {
                'delta_est': de,
                'omega_est': oe,
                'rmse': float(rmse),
                'is_fault': node_fault,
                'fault_phase': 'fault' if node_fault else 'normal',
            }

        steps.append(frame)

    output = {
        'system_params': {
            'fs': FS,
            'num_samples': num_samples,
            'total_time': num_samples / FS,
        },
        'node_ids': ['node_1', 'node_2', 'node_3'],
        'node_labels': {
            'node_1': '终端节点 1',
            'node_2': '终端节点 2',
            'node_3': '终端节点 3',
        },
        'steps': steps,
        'fault_snapshots': _build_fault_snapshots(steps),
    }

    out_path = os.path.join(DATA_DIR, 'dashboard_data.json')
    with open(out_path, 'w') as f:
        json.dump(output, f)
    print(f'  → {out_path} ({num_samples} 步)')

    # 4. 摘要
    print(f"\n{'='*60}")
    print(f"故障阶段估值对比 (Step {num_samples-1})")
    print(f"{'='*60}")
    last = steps[-1]
    for nid in ['node_1', 'node_2', 'node_3']:
        nd = last['nodes'][nid]
        de = [round(x, 4) for x in nd['delta_est']]
        oe = [round(x, 4) for x in nd['omega_est']]
        print(f"  {nid}: δ={de}, ω={oe}")

    # 故障窗口验证
    for nid in ['node_1', 'node_2', 'node_3']:
        fault_steps = []
        for i, s in enumerate(steps):
            if s['nodes'][nid].get('is_fault'):
                fault_steps.append(i)
        if fault_steps:
            print(f"  {nid}: 故障窗口 step {fault_steps[0]}-{fault_steps[-1]} "
                  f"({len(fault_steps)} 步)")

    print(f"\n{'='*60}")
    print("部署文件:")
    print(f"  {TOOL_DIR}/controller_online")
    print(f"  {TOOL_DIR}/controller_online.arm")
    for nid in ['node_1', 'node_2', 'node_3']:
        print(f"  {DATA_DIR}/estimates_{nid}.csv")
    print(f"  {DATA_DIR}/dashboard_data.json")
    print("=" * 60)


if __name__ == '__main__':
    import json
    main()
