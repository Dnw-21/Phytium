#!/usr/bin/env python3
"""
mock_ukf_data.py — 生成 Mock UKF 数据用于离线测试 Dashboard
============================================================
不依赖开发板/FreeRTOS, 直接在本地生成 3 节点的 npz + metrics,
让 Dashboard 可以在浏览器中正常显示曲线和资源监控。

用法:
    # 1. 生成 mock npz (覆盖现有 /tmp/ukf_results_*.npz)
    python3 mock_ukf_data.py gen

    # 2. 启动 dashboard 测试 (假设 dashboard_server_v2.py 已在 5001 端口)
    python3 mock_ukf_data.py gen && python3 dashboard_server_v2.py --port 5001

    # 3. 验证 API 输出
    python3 mock_ukf_data.py verify
"""

import os
import sys
import json
import time
import math
import struct
import random
from datetime import datetime

import numpy as np

# 与 dashboard_server_v2.py / launch_ukf_multi.py / ukf_pipeline.c 保持一致
NODE_META = {
    '5bus':  {'ns': 4,  'n_gen': 2,  'nm': 14, 'n_bus': 5,  'fault': (5.0, 5.3)},
    '39bus': {'ns': 20, 'n_gen': 10, 'nm': 98, 'n_bus': 39, 'fault': (5.0, 5.3)},
    '9bus':  {'ns': 6,  'n_gen': 3,  'nm': 24, 'n_bus': 9,  'fault': (5.0, 5.3)},
}
NPZ_TPL = '/tmp/ukf_results_{}.npz'
METRICS_FILE = '/tmp/ukf_metrics.json'


def gen_node(node_name, n_frames=300, t_end=10.0, with_z=True):
    """生成单个节点的 mock UKF 数据"""
    meta = NODE_META[node_name]
    ns = meta['ns']
    n_gen = meta['n_gen']
    nm = meta['nm']
    f_start, f_end = meta['fault']

    dt = t_end / n_frames
    times = [i * dt for i in range(n_frames)]

    # X_est: ns 个状态维度
    # - 前 n_gen 维: δ (角度), 围绕平衡点小波动
    # - 后 n_gen 维: ω (转速), 围绕 0
    X_est = [[] for _ in range(ns)]
    for i, t in enumerate(times):
        # 故障注入: t ∈ [5, 5.3] 时 δ 大幅震荡
        in_fault = f_start <= t <= f_end
        post_fault = t > f_end and t < f_end + 1.0
        for k in range(n_gen):
            # δ 维度 (前 n_gen)
            base_delta = 0.3 + 0.1 * k  # 各发电机初值
            if in_fault:
                osc = 0.5 * math.sin(2 * math.pi * 8 * (t - f_start))
            elif post_fault:
                osc = 0.3 * math.exp(-3 * (t - f_end)) * math.cos(2 * math.pi * 6 * (t - f_end))
            else:
                osc = 0.05 * math.sin(2 * math.pi * 0.3 * t) + 0.02 * random.gauss(0, 1)
            X_est[k].append(base_delta + osc)
        for k in range(n_gen):
            # ω 维度 (后 n_gen)
            if in_fault:
                osc = 0.3 * math.sin(2 * math.pi * 8 * (t - f_start))
            elif post_fault:
                osc = 0.2 * math.exp(-3 * (t - f_end)) * math.sin(2 * math.pi * 6 * (t - f_end))
            else:
                osc = 0.01 * math.sin(2 * math.pi * 0.3 * t) + 0.005 * random.gauss(0, 1)
            X_est[n_gen + k].append(osc)

    # RMSE: 故障时显著上升
    rmse = []
    for t in times:
        if f_start <= t <= f_end:
            r = 0.05 + 0.2 * (t - f_start) / 0.3
        elif t > f_end:
            r = 0.05 + 0.15 * math.exp(-2 * (t - f_end))
        else:
            r = 0.02 + 0.01 * random.gauss(0, 1)
        rmse.append(max(0.001, r))

    # Z (测量值): nm 维
    # 物理含义: [P_inj(n_gen), Q_inj(n_gen), V_mag(n_bus), V_ang(n_bus)]
    # 这里只 mock 形状
    Z = [[] for _ in range(nm)]
    for i, t in enumerate(times):
        in_fault_t = f_start <= t <= f_end
        for k in range(nm):
            base = 0.0
            if k < n_gen:
                # P_inj, 在 δ 附近
                base = 0.8 + 0.3 * X_est[k][i] + 0.05 * random.gauss(0, 1)
            elif k < 2 * n_gen:
                # Q_inj
                base = 0.4 + 0.1 * X_est[k - n_gen][i] + 0.05 * random.gauss(0, 1)
            elif k < 2 * n_gen + meta['n_bus']:
                # V_mag (母线电压幅值, 0.9~1.1)
                base = 1.0 + 0.05 * random.gauss(0, 1)
                if in_fault_t:
                    base *= 0.85
            else:
                # V_ang (母线电压相角)
                ang_idx = k - 2 * n_gen - meta['n_bus']
                base = 0.1 * X_est[ang_idx][i] if ang_idx < n_gen else 0.0
            Z[k].append(base)

    return {
        'time': times,
        'X_est': X_est,
        'rmse': rmse,
        'Z': Z if with_z else [],
    }


def in_fault(t, f_start, f_end):
    return f_start <= t <= f_end


def save_npz(node_name, data):
    """保存为 npz (与 launch_ukf_multi.py 格式一致)"""
    path = NPZ_TPL.format(node_name)
    save_dict = dict(
        time=np.array(data['time'], dtype=np.float64),
        X_est=np.array(data['X_est'], dtype=np.float64),
        rmse=np.array(data['rmse'], dtype=np.float64),
    )
    if data.get('Z'):
        save_dict['Z'] = np.array(data['Z'], dtype=np.float64)
    np.savez(path, **save_dict)
    print(f'[mock] saved {path} '
          f'({len(data["time"])} frames, ns={len(data["X_est"])}, '
          f'Z_dim={len(data.get("Z", []))})')
    return path


def save_metrics():
    """生成 metrics.json 模拟 launch_ukf_multi.py 的输出"""
    metrics = {}
    for node, meta in NODE_META.items():
        # 模拟进程: random 一点的 fps/cpu_pct
        metrics[node] = {
            'label': f'IEEE {meta["n_bus"]}-Bus',
            'ns': meta['ns'],
            'nm': meta['nm'],
            'cpu_core': {'5bus': 0, '39bus': 2, '9bus': 1}[node],
            'cpu_pct': round(random.uniform(15, 35), 1),
            'ts': 10.0,
            'frames': 300,
            'fps': round(40 + random.uniform(-5, 5), 1),
            'rmse': round(random.uniform(0.02, 0.05), 5),
            'latency_us': round(150 + random.uniform(0, 80), 1),
            'status': 'running',
            'updated_at': datetime.now().isoformat(),
        }
    with open(METRICS_FILE, 'w') as f:
        json.dump(metrics, f, indent=2)
    print(f'[mock] saved {METRICS_FILE}')


def cmd_gen(args):
    """生成 mock npz 和 metrics"""
    n_frames = int(args[0]) if args else 300
    print(f'[mock] generating {n_frames} frames per node...')
    random.seed(42)
    np.random.seed(42)
    for node in ['5bus', '39bus', '9bus']:
        data = gen_node(node, n_frames=n_frames)
        save_npz(node, data)
    save_metrics()
    print('[mock] done. 启动 dashboard: python3 dashboard_server_v2.py --port 5001')


def cmd_verify(args):
    """验证 dashboard API (需要先启动 server)"""
    import urllib.request
    base = args[0] if args else 'http://127.0.0.1:5001'

    tests = [
        ('/api/status',     'status'),
        ('/api/compare',    'compare'),
        ('/api/history?node=5bus',  'history.5bus'),
        ('/api/history?node=39bus', 'history.39bus'),
        ('/api/history?node=9bus',  'history.9bus'),
        ('/api/resources',  'resources'),
    ]
    print(f'[verify] base={base}')
    ok = 0
    for path, name in tests:
        url = base + path
        try:
            with urllib.request.urlopen(url, timeout=3) as r:
                data = json.loads(r.read().decode())
            # 简单校验
            if name.startswith('history'):
                node = name.split('.')[1]
                t = data.get('time', [])
                z = data.get('Z_dim', 0)
                states = sum(1 for k in data if k.startswith('state_'))
                zkeys = sum(1 for k in data if k.startswith('Z_'))
                print(f'  [OK] {path:35s}  t={len(t)}  states={states}  Z_dim={z}  Z_keys={zkeys}')
            elif name == 'status':
                nodes = data.get('nodes', [])
                print(f'  [OK] {path:35s}  status={data.get("status")}  nodes={len(nodes)}')
            elif name == 'compare':
                print(f'  [OK] {path:35s}  nodes={list(data.keys())}')
            elif name == 'resources':
                cores = data.get('cores', {})
                shm = data.get('shm', {})
                procs = data.get('processes', {})
                print(f'  [OK] {path:35s}  cores={len(cores)}  shm={len(shm)}  procs={len(procs)}')
            ok += 1
        except Exception as e:
            print(f'  [FAIL] {path:35s}  {e}')
    print(f'[verify] {ok}/{len(tests)} OK')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    args = sys.argv[2:]
    if cmd == 'gen':
        return cmd_gen(args) or 0
    elif cmd == 'verify':
        return cmd_verify(args) or 0
    else:
        print(f'unknown command: {cmd}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
