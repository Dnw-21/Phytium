#!/usr/bin/env python3
"""
从 trace_reader 输出中提取 FLASH_WAVE (type=0x05) 波形数据并绘图。

FreeRTOS v12+ 输出格式:
  [FW_BEG] wave#N                          ← 波形会话开始
  [FW_DAT p=N ts=T len=L]                  ← 每帧标记行
  HEXHEXHEX...                              ← 完整 hex (无空格, 不截断)
  [FW_END] wave#N pkts=N bytes=N ...       ← 波形会话结束

用法:
  1. 捕获数据: ssh user@192.168.88.11 "sudo timeout 120 /home/user/trace_reader" 2>/dev/null > trace_wave.txt
  2. 绘图:     python3 plot_wave.py trace_wave.txt
  图片保存在 txt 文件同目录下。
"""
import sys, os, struct, re
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def parse_fw_dat(text):
    """从文本中提取所有 [FW_DAT] 行的 hex 数据, 返回 (bytes, list_of_pkt_info)"""
    # 匹配: [FW_DAT p=N ts=T len=L]\nHEXHEX...\n
    # 注意: hex 行是紧随 [FW_DAT] 行之后的整行紧凑 hex, 无空格
    pattern = r'\[FW_DAT p=(\d+) ts=(\d+) len=(\d+)\]\r?\n([0-9A-Fa-f]+)'
    matches = re.findall(pattern, text)
    if not matches:
        return None, []

    all_bytes = bytearray()
    pkt_info = []
    for m in matches:
        pkt_idx = int(m[0])
        ts = int(m[1])
        pkt_len = int(m[2])
        hex_str = m[3].strip()
        try:
            data = bytes.fromhex(hex_str)
        except ValueError:
            print(f'  WARN: invalid hex in pkt {pkt_idx}')
            continue
        if len(data) != pkt_len:
            print(f'  WARN: pkt {pkt_idx} len mismatch: expected {pkt_len}, got {len(data)}')
        all_bytes.extend(data)
        pkt_info.append((pkt_idx, ts, len(data)))

    return bytes(all_bytes), pkt_info


def parse_fw_end(text):
    """从文本中提取 [FW_END] 汇总信息"""
    m = re.search(
        r'\[FW_END\] wave#(\d+) pkts=(\d+) bytes=(\d+) pts=(\d+) lost=(\d+) '
        r'ts=\[(\d+)\.\.(\d+)\] val=\[(\d+)\.\.(\d+)\]',
        text
    )
    if m:
        return {
            'wave_id': int(m.group(1)),
            'pkts': int(m.group(2)),
            'bytes': int(m.group(3)),
            'pts': int(m.group(4)),
            'lost': int(m.group(5)),
            'ts_first': int(m.group(6)),
            'ts_last': int(m.group(7)),
            'val_min': int(m.group(8)),
            'val_max': int(m.group(9)),
        }
    return None


def extract_all_waves(text):
    """提取文本中所有波形会话, 返回 [(wave_id, raw_bytes, pkt_info), ...]"""
    waves = []
    # 按 [FW_BEG] 分段
    parts = re.split(r'\[FW_BEG\]', text)
    for part in parts[1:]:  # 跳过第一个空段
        raw, pkts = parse_fw_dat(part)
        if raw:
            # 获取 wave_id
            m = re.match(r'\s*wave#(\d+)', part)
            wid = int(m.group(1)) if m else 0
            waves.append((wid, raw, pkts))
    return waves


def plot_wave(raw_bytes, pkts_info, out_path, sample_rate=None):
    """绘制 FLASH_WAVE int16 波形 (大端序)"""
    n = len(raw_bytes) // 2
    if n == 0:
        print('No int16 samples to plot')
        return
    samples = np.array(struct.unpack('>%dh' % n, raw_bytes), dtype=np.float64)

    fig, ax = plt.subplots(figsize=(16, 6))

    if sample_rate and sample_rate > 0:
        t = np.arange(n) / sample_rate * 1000  # ms
        ax.plot(t, samples, linewidth=0.5, color='blue')
        ax.set_xlabel('Time (ms)')
        title_rate = f', rate={sample_rate}Hz'
    else:
        ax.plot(samples, linewidth=0.5, color='blue')
        ax.set_xlabel('Sample index')
        title_rate = ''

    total_pkts = len(pkts_info)
    ax.set_title(f'FLASH_WAVE Waveform — {total_pkts} pkts, {n} samples{title_rate}')
    ax.set_ylabel('int16 value')
    ax.grid(True, alpha=0.3)

    # 标注每帧边界
    sample_idx = 0
    for pkt_idx, ts, plen in pkts_info:
        pts_in_pkt = plen // 2
        sample_idx += pts_in_pkt
        x = sample_idx
        if sample_rate and sample_rate > 0:
            x = sample_idx / sample_rate * 1000
        ax.axvline(x=x, color='red', alpha=0.15, linewidth=0.5)

    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    print(f'Waveform saved to {out_path}')
    plt.close()

    # 每帧统计
    print(f'\n  Per-frame stats ({total_pkts} frames, Big-Endian int16):')
    sample_start = 0
    for pkt_idx, ts, plen in pkts_info:
        pts = plen // 2
        sample_end = sample_start + pts
        if sample_start >= n:
            break
        chunk = samples[sample_start:sample_end]
        print(f'  Pkt {pkt_idx:3d}: ts={ts:6d} [{sample_start:5d}..{sample_end-1:5d}] '
              f'min={chunk.min():7.0f} max={chunk.max():7.0f} mean={chunk.mean():9.1f}')
        sample_start = sample_end


def plot_node_raw(samples, out_path):
    """绘制 NODE_RAW 转换后的物理值"""
    if not samples:
        print('No NODE_RAW samples found')
        return
    arr = np.array(samples)
    fig, axes = plt.subplots(4, 1, figsize=(16, 10), sharex=True)
    labels = ['Active Power (W)', 'Reactive Power (var)', 'Voltage Angle (rad)', 'Voltage Magnitude (V)']
    colors = ['blue', 'red', 'green', 'orange']
    for i in range(4):
        axes[i].plot(arr[:, i], linewidth=0.8, color=colors[i])
        axes[i].set_ylabel(labels[i])
        axes[i].grid(True, alpha=0.3)
    axes[0].set_title(f'NODE_RAW Converted Values — {len(samples)} samples')
    axes[-1].set_xlabel('Sample index')
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    print(f'NODE_RAW plot saved to {out_path}')
    plt.close()


def main():
    if len(sys.argv) < 2:
        print(f'Usage: {sys.argv[0]} <trace_output.txt>')
        sys.exit(1)

    txt_path = os.path.abspath(sys.argv[1])
    out_dir = os.path.dirname(txt_path)

    with open(txt_path, 'r', errors='replace') as f:
        text = f.read()

    # 解析 [FW_END] 汇总
    end_info = parse_fw_end(text)
    if end_info:
        print(f'FW_END summary: wave#{end_info["wave_id"]} '
              f'{end_info["pkts"]} pkts, {end_info["bytes"]} bytes, '
              f'{end_info["pts"]} pts, {end_info["lost"]} lost, '
              f'ts=[{end_info["ts_first"]}..{end_info["ts_last"]}], '
              f'val=[{end_info["val_min"]}..{end_info["val_max"]}]')

    # 解析所有波形会话
    waves = extract_all_waves(text)

    if not waves:
        # 兼容旧格式: 尝试解析 [WCAP]
        print('No [FW_DAT] data found, trying legacy [WCAP] format...')
        raw, pkts = None, set()
        m = re.search(r'\[WCAP\] pkts=(\d+) bytes=(\d+)\r?\n(.*?)\[WCAP_END\]', text, re.DOTALL)
        if m:
            hex_str = re.sub(r'[\s\r\n]+', '', m.group(3))
            raw = bytes.fromhex(hex_str)
            pkts = [(i, 0, 128) for i in range(int(m.group(1)))]
        if raw:
            plot_wave(raw, pkts, os.path.join(out_dir, 'waveform.png'))
        else:
            print('No waveform data found in trace output')
    else:
        for wave_id, raw_bytes, pkt_info in waves:
            n_samples = len(raw_bytes) // 2
            print(f'\nWave #{wave_id}: {len(pkt_info)} pkts, {len(raw_bytes)} bytes, {n_samples} samples')
            out_name = f'waveform_w{wave_id}.png' if len(waves) > 1 else 'waveform.png'
            plot_wave(raw_bytes, pkt_info, os.path.join(out_dir, out_name))

    # 解析 NODE_RAW 数据
    samples = []
    for m in re.finditer(r'\[RAW\] s\d+: p=(-?\d+) q=(-?\d+) ang=(-?\d+) vmag=(-?\d+)', text):
        p = int(m.group(1)) / 10000.0
        q = int(m.group(2)) / 10000.0
        ang = int(m.group(3)) / 10000.0
        vmag = int(m.group(4)) / 10000.0
        samples.append((p, q, ang, vmag))
    if samples:
        print(f'\nFound {len(samples)} NODE_RAW samples')
        plot_node_raw(samples, os.path.join(out_dir, 'node_raw.png'))

if __name__ == '__main__':
    main()
