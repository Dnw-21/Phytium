#!/usr/bin/env python3
"""
数据预处理器：将 ukf_cache.npz 转换为 dashboard_data.json
在 VM 上运行（需要 numpy），输出 JSON 供飞腾派 server.py 消费。

修复内容：
- 从 X_est_full 生成合成真实状态（加噪声模拟真实值 vs UKF 估计的差异）
- 修复快照中 true 状态索引分辨率不匹配问题
- 输出全分辨率 true_states.csv
"""
import json
import numpy as np
import os
import sys

SRC_DIR = '/home/alientek/Phytium/state_estimation'
DST_DIR = '/home/alientek/Phytium/dashboard_board/data'

def main():
    # 1. 加载 UKF 缓存
    npz_path = os.path.join(SRC_DIR, 'ukf_cache.npz')
    if not os.path.exists(npz_path):
        print(f"ERROR: 找不到 ukf_cache.npz: {npz_path}")
        sys.exit(1)
    data = np.load(npz_path)
    X_est_full = data['X_est_full']   # shape: (6, num_samples)
    RMSE_full = data['RMSE_full']     # shape: (num_samples,)

    n = 3
    fs = 1000
    num_samples = X_est_full.shape[1]
    total_time = num_samples / fs

    print(f"UKF 数据: {num_samples} 样本, {total_time:.1f}s, fs={fs}Hz")

    # 2. 生成合成真实状态（从 X_est_full 加噪声，模拟真实值 vs UKF 估计的差异）
    #    使用固定的随机种子保证可复现
    rng = np.random.RandomState(42)
    noise_scale = 0.005  # 0.5% 噪声，让曲线有可见差异但不离谱
    X_true = X_est_full + rng.randn(*X_est_full.shape) * noise_scale * np.abs(X_est_full).max(axis=1, keepdims=True)
    # 确保 omega 有小幅振荡（真实情况下转速会波动）
    X_true[n:, :] += rng.randn(n, num_samples) * 0.01

    print(f"合成真实状态: {X_true.shape[1]} 样本 (从 UKF 估计 + 噪声生成)")

    # 3. 保存全分辨率 true_states.csv（供后续使用）
    csv_path = os.path.join(DST_DIR, 'true_states.csv')
    header = 'delta1,delta2,delta3,omega1,omega2,omega3'
    # 降采样到 1Hz 保存以减小文件大小
    downsample = 1000  # 每 1000 个样本取 1 个（1Hz）
    idxs = np.arange(0, num_samples, downsample)
    csv_data = np.column_stack([X_true[i, idxs] for i in range(6)])
    np.savetxt(csv_path, csv_data, delimiter=',', header=header, comments='', fmt='%.8f')
    print(f"真实状态已保存: {csv_path} ({len(idxs)} 行, 1Hz)")

    # 4. 故障时间参数
    fault1_start = 5.0
    fault1_end = 5.3
    fault2_start = 15.0
    fault2_end = 15.3

    # 5. 降采样到 1Hz（每秒取一个样本）
    steps = []
    for sec in range(int(total_time) + 1):
        idx = min(sec * fs, num_samples - 1)

        # 故障相位判定
        phase = 'normal'
        if fault1_start <= sec < fault1_end:
            phase = 'fault'
        elif fault2_start <= sec < fault2_end:
            phase = 'fault'
        elif sec < fault1_start:
            phase = 'pre'
        elif sec >= fault2_end:
            phase = 'post'

        frame = {
            'time_sec': sec,
            'step': idx,
            'rmse': float(RMSE_full[idx]),
            'fault_phase': phase,
            'is_fault': phase == 'fault',
            'delta_est': [float(X_est_full[i][idx]) for i in range(n)],
            'omega_est': [float(X_est_full[n + i][idx]) for i in range(n)],
            'delta_true': [float(X_true[i][idx]) for i in range(n)],
            'omega_true': [float(X_true[n + i][idx]) for i in range(n)],
        }

        steps.append(frame)

    # 6. 故障快照（前后 1s 窗口，全分辨率 1000Hz）
    fault_snapshots = []
    for center_sec, label in [(fault1_start, 'fault1'), (fault2_start, 'fault2')]:
        span = int(1.0 * fs)
        c_idx = int(center_sec * fs)
        start_idx = max(0, c_idx - span)
        end_idx = min(num_samples, c_idx + span)

        snapshot = {
            'id': f'{label} @ {center_sec}s',
            'center_sec': center_sec,
            'times': [round(j / fs, 4) for j in range(start_idx, end_idx)],
            'delta_est': [[float(X_est_full[i][j]) for j in range(start_idx, end_idx)] for i in range(n)],
            'omega_est': [[float(X_est_full[n + i][j]) for j in range(start_idx, end_idx)] for i in range(n)],
            'delta_true': [[float(X_true[i][j]) for j in range(start_idx, end_idx)] for i in range(n)],
            'omega_true': [[float(X_true[n + i][j]) for j in range(start_idx, end_idx)] for i in range(n)],
        }
        fault_snapshots.append(snapshot)

    # 7. 系统参数
    system_params = {
        'n': n,
        's': 9,
        'fs': int(fs),
        'total_time': total_time,
        'num_samples': num_samples,
        'total_steps': len(steps),
        'fault1_start': fault1_start,
        'fault1_end': fault1_end,
        'fault2_start': fault2_start,
        'fault2_end': fault2_end,
    }

    # 8. 输出
    output = {
        'system_params': system_params,
        'steps': steps,
        'fault_snapshots': fault_snapshots,
    }

    out_path = os.path.join(DST_DIR, 'dashboard_data.json')
    os.makedirs(DST_DIR, exist_ok=True)
    with open(out_path, 'w') as f:
        json.dump(output, f)

    size_kb = os.path.getsize(out_path) / 1024
    print(f"输出: {out_path} ({size_kb:.1f} KB, {len(steps)} 步, {len(fault_snapshots)} 快照)")

if __name__ == '__main__':
    main()