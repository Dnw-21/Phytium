#!/usr/bin/env python3
"""
三节点数据生成器 — 基于 zdata 和 controller_online (C UKF) 的实时估计管道

流程：
  1. 从 dy_3gen/z*data_adaptive.c 解析 zdata 数组（每节点 60 normal + 20 fault = 80 点）
  2. 将 Vmag/Vangle 转换为 controller_online 期望的 Vreal/Vimag 格式
  3. 生成每个节点的 measurements.txt
  4. 通过 controller_online（C 二进制）逐条运行 UKF
  5. 收集估计结果 → 输出 dashboard_data.json（3 节点独立数据）

运行方式：
  cd /home/alientek/Phytium/dashboard_board
  python3 prep/generate_3node_data.py
"""

import os
import re
import sys
import json
import math
import subprocess
import numpy as np

# ── 路径配置 ──────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
Z_SRC = {
    'node_1': {
        'c_file': '/home/alientek/Phytium/dy_3gen/zdata_adaptive.c',
        'normal_array': 'g_zdata_normal',
        'fault_array': 'g_zdata_fault',
    },
    'node_2': {
        'c_file': '/home/alientek/Phytium/dy_3gen/z2data_adaptive.c',
        'normal_array': 'g_z2data_normal',
        'fault_array': 'g_z2data_fault',
    },
    'node_3': {
        'c_file': '/home/alientek/Phytium/dy_3gen/z3data_adaptive.c',
        'normal_array': 'g_z3data_normal',
        'fault_array': 'g_z3data_fault',
    },
}
UKF_DIR = '/home/alientek/Phytium/dy_3gen/DSE_Calculation_UKF_9case_3minc_implementation'
UKF_BIN = os.path.join(UKF_DIR, 'controller_online')
PARAMS_BIN = os.path.join(UKF_DIR, 'system_params.bin')
DATA_DIR = os.path.join(BASE_DIR, 'data')
OUTPUT_PATH = os.path.join(DATA_DIR, 'dashboard_data.json')

FS = 2000  # UKF 内部采样率 (Hz)
DELTT = 0.0005  # UKF 时间步长


def parse_zdata_c_array(c_file_path, array_name):
    """从 C 源文件中解析 const 数组数据。

    Returns:
        list[list[float]]: 每行是一个数据点的 24 个 float 值
    """
    with open(c_file_path, 'r') as f:
        content = f.read()

    # 定位数组定义
    pattern = rf'const\s+\w+\s+{re.escape(array_name)}\[\w+\]\s*=\s*\{{'
    match = re.search(pattern, content)
    if not match:
        print(f"  ERROR: 找不到数组 {array_name} 在 {c_file_path}")
        return []

    start = match.end()
    # 找到匹配的闭合括号
    brace_count = 1
    i = start
    while i < len(content) and brace_count > 0:
        if content[i] == '{':
            brace_count += 1
        elif content[i] == '}':
            brace_count -= 1
        i += 1
    array_text = content[start:i-1]

    # 提取所有 {...} 块
    points = []
    for m in re.finditer(r'\{([^}]+)\}', array_text):
        vals_str = m.group(1)
        vals = [float(x.strip().rstrip('f')) for x in vals_str.split(',')]
        assert len(vals) == 24, f"期望 24 个值，实际 {len(vals)}"
        points.append(vals)

    return points


def vmag_vangle_to_vreal_vimag(data_points):
    """将测量数据从 [PG1-3, QG1-3, Vmag1-9, Vangle1-9] 转换为
    [PG1-3, QG1-3, Vreal1-9, Vimag1-9]（controller_online 期望的格式）。

    每个 data_point: [pg1,pg2,pg3, qg1,qg2,qg3, vmag1..vmag9, vangle1..vangle9]
    """
    converted = []
    for pt in data_points:
        pg = pt[0:3]
        qg = pt[3:6]
        vmag = pt[6:15]
        vangle = pt[15:24]
        vreal = [vm * math.cos(va) for vm, va in zip(vmag, vangle)]
        vimag = [vm * math.sin(va) for vm, va in zip(vmag, vangle)]
        converted.append(pg + qg + vreal + vimag)
    return converted


def write_measurements(data_points, output_path):
    """将测量数据写入 controller_online 可读取的 CSV 格式。

    格式: time,PG1,PG2,PG3,QG1,QG2,QG3,Vreal1..Vreal9,Vimag1..Vimag9
    时间从 0 开始，步长为 1/FS。
    """
    with open(output_path, 'w') as f:
        f.write('# time,PG1,PG2,PG3,QG1,QG2,QG3,Vreal1..Vreal9,Vimag1..Vimag9\n')
        for i, pt in enumerate(data_points):
            t = i / FS
            line = ','.join([f'{t:.6f}'] + [f'{v:.6f}' for v in pt])
            f.write(line + '\n')
    print(f"  Wrote {len(data_points)} measurements → {output_path}")


def run_ukf(measurements_path, output_csv_path):
    """运行 controller_online，将测量数据管道输入，捕获估计输出。

    Returns:
        list[dict]: 每步的估计结果 {time, delta1-3, omega1-3, rmse}
    """
    cwd = UKF_DIR
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = ''

    with open(measurements_path, 'r') as fin:
        proc = subprocess.run(
            [UKF_BIN],
            stdin=fin,
            capture_output=True,
            text=True,
            cwd=cwd,
            env=env,
        )

    if proc.returncode != 0:
        print(f"  ERROR: UKF 进程返回 {proc.returncode}")
        print(f"  stderr: {proc.stderr[:500]}")
        return []

    # 解析 CSV 输出
    results = []
    for line in proc.stdout.strip().split('\n'):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(',')
        if len(parts) != 8:
            continue
        results.append({
            'time': float(parts[0]),
            'delta1': float(parts[1]),
            'delta2': float(parts[2]),
            'delta3': float(parts[3]),
            'omega1': float(parts[4]),
            'omega2': float(parts[5]),
            'omega3': float(parts[6]),
            'rmse': float(parts[7]),
        })

    # 保存到文件
    with open(output_csv_path, 'w') as f:
        f.write('# time,delta1,delta2,delta3,omega1,omega2,omega3,RMSE\n')
        for r in results:
            f.write(f"{r['time']:.6f},{r['delta1']:.8f},{r['delta2']:.8f},{r['delta3']:.8f},"
                    f"{r['omega1']:.8f},{r['omega2']:.8f},{r['omega3']:.8f},{r['rmse']:.8f}\n")

    print(f"  UKF produced {len(results)} estimates → {output_csv_path}")
    return results


def generate_node_data(node_id, z_info):
    """为单个节点生成完整估计数据。"""
    print(f"\n{'='*60}")
    print(f"Processing {node_id}...")
    print(f"{'='*60}")

    # 1. 解析 zdata
    normal = parse_zdata_c_array(z_info['c_file'], z_info['normal_array'])
    fault = parse_zdata_c_array(z_info['c_file'], z_info['fault_array'])
    all_data = normal + fault
    print(f"  Parsed {len(normal)} normal + {len(fault)} fault = {len(all_data)} points")

    # 2. 转换格式：Vmag/Vangle → Vreal/Vimag
    measurements = vmag_vangle_to_vreal_vimag(all_data)

    # 3. 写入 measurements.txt
    meas_path = os.path.join(DATA_DIR, f'measurements_{node_id}.txt')
    write_measurements(measurements, meas_path)

    # 4. 运行 UKF
    csv_path = os.path.join(DATA_DIR, f'estimates_{node_id}.csv')
    results = run_ukf(meas_path, csv_path)

    if not results:
        print(f"  ERROR: {node_id} UKF 无输出")
        return None

    # 5. 构造 steps（与 dashboard_server 兼容的格式）
    steps = []
    for i, r in enumerate(results):
        # 简易故障判定：后 20 个点为故障阶段（对应 zdata fault 段）
        is_fault = (i >= len(normal))
        steps.append({
            'time_sec': r['time'],
            'step': i,
            'rmse': r['rmse'],
            'fault_phase': 'fault' if is_fault else 'normal',
            'is_fault': is_fault,
            'delta_est': [r['delta1'], r['delta2'], r['delta3']],
            'omega_est': [r['omega1'], r['omega2'], r['omega3']],
        })

    return {
        'node_id': node_id,
        'steps': steps,
        'total': len(steps),
        'normal_count': len(normal),
        'fault_count': len(fault),
    }


def main():
    print("=" * 60)
    print("三节点 UKF 数据生成器")
    print("=" * 60)

    # 检查 UKF 二进制
    if not os.path.exists(UKF_BIN):
        print(f"ERROR: UKF 二进制不存在: {UKF_BIN}")
        print("请先编译: cd {UKF_DIR} && make build")
        sys.exit(1)

    if not os.path.exists(PARAMS_BIN):
        print(f"ERROR: system_params.bin 不存在: {PARAMS_BIN}")
        sys.exit(1)

    os.makedirs(DATA_DIR, exist_ok=True)

    # 处理三个节点
    node_data = {}
    for node_id in ['node_1', 'node_2', 'node_3']:
        result = generate_node_data(node_id, Z_SRC[node_id])
        if result is None:
            print(f"ERROR: {node_id} 生成失败")
            sys.exit(1)
        node_data[node_id] = result

    # 构建 dashboard_data.json
    # 使用 node_1 的 steps 作为主时间线（所有节点同步）
    steps_node1 = node_data['node_1']['steps']

    system_params = {
        'n': 3,
        's': 9,
        'fs': FS,
        'total_time': steps_node1[-1]['time_sec'] if steps_node1 else 0,
        'num_samples': len(steps_node1),
        'total_steps': len(steps_node1),
        'fault_start': node_data['node_1']['normal_count'] / FS,
        'fault_end': len(steps_node1) / FS,
    }

    # 每个 step 合并三个节点的数据
    merged_steps = []
    for i in range(len(steps_node1)):
        frame = {
            'time_sec': steps_node1[i]['time_sec'],
            'step': i,
            'rmse': steps_node1[i]['rmse'],
            'fault_phase': steps_node1[i]['fault_phase'],
            'is_fault': steps_node1[i]['is_fault'],
            # 三个节点的独立估计
            'nodes': {}
        }
        for nid in ['node_1', 'node_2', 'node_3']:
            nd = node_data[nid]
            if i < len(nd['steps']):
                s = nd['steps'][i]
                frame['nodes'][nid] = {
                    'delta_est': s['delta_est'],
                    'omega_est': s['omega_est'],
                    'rmse': s['rmse'],
                    'is_fault': s['is_fault'],
                    'fault_phase': s['fault_phase'],
                }
        merged_steps.append(frame)

    output = {
        'system_params': system_params,
        'steps': merged_steps,
        'fault_snapshots': [],  # 不再需要离线快照
        'node_ids': ['node_1', 'node_2', 'node_3'],
    }

    with open(OUTPUT_PATH, 'w') as f:
        json.dump(output, f)

    size_kb = os.path.getsize(OUTPUT_PATH) / 1024
    print(f"\n{'='*60}")
    print(f"输出: {OUTPUT_PATH} ({size_kb:.1f} KB)")
    print(f"节点: {len(output['node_ids'])} 个, 每节点 {len(steps_node1)} 步")
    print(f"时间范围: 0 ~ {system_params['total_time']:.4f}s")
    print(f"{'='*60}")


if __name__ == '__main__':
    main()
