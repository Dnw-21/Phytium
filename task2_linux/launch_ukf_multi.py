#!/usr/bin/env python3
"""
launch_ukf_multi.py — 多节点在线 UKF 并行启动器 (v4)
=====================================================
基于 multi_node 目录下的实时 UKF 实现, 每节点独立二进制。

数据流:
  FreeRTOS → SHM ring buffer → ukf_pipeline_{node} (mmap /dev/mem)
                                    ↓
                              [ukf_init] (一次)
                              [ukf_step] (每帧, 在线实时)
                                    ↓
                              stdout CSV → 保存到 /tmp/ukf_out_{node}.csv
                              stderr heartbeat → 解析指标

CPU 分配方案:
  Core 0 (A55): 5bus + 9bus UKF + Python 主脚本 (轻量混合区)
  Core 1 (A76): FreeRTOS 独占 (三节点 RK4 仿真引擎)
  Core 2 (A76): 39bus UKF 独占 (VIP 计算特区)

与 v3 的关键区别:
  1. 使用 ukf_pipeline_{node} 专用二进制 (而非 ukf_pipeline --node)
  2. CSV 文本输出 (而非二进制帧)
  3. 在线实时 UKF 算法 (分离 Q/R, 矩阵求逆)
  4. 编译时专用化 (更小内存, 更高 cache 命中率)
  5. 零 numpy 依赖

用法:
  sudo python3 launch_ukf_multi.py [--nodes 5bus,39bus,9bus] [--speed 10]

输出:
  /tmp/ukf_out_5bus.csv /tmp/ukf_out_39bus.csv /tmp/ukf_out_9bus.csv
  /tmp/ukf_metrics.json     (实时指标)
  /tmp/ukf_log_5bus.log     (节点日志)
"""

import os, sys, re, time, signal, subprocess, threading, json
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ── 节点配置 ──
NODES = {
    '5bus':  {'bin': 'ukf_pipeline_5bus',  'ns': 4,  'nm': 14,
              'label': 'IEEE 5-Bus (2-gen)',  'cpu': 0},
    '39bus': {'bin': 'ukf_pipeline_39bus', 'ns': 20, 'nm': 98,
              'label': 'IEEE 39-Bus (10-gen)', 'cpu': 2},
    '9bus':  {'bin': 'ukf_pipeline_9bus',  'ns': 6,  'nm': 24,
              'label': 'IEEE 9-Bus (3-gen)',   'cpu': 0},
}

METRICS_FILE = '/tmp/ukf_metrics.json'
METRICS_INTERVAL = 1.0

g_running = True
g_processes = {}
g_metrics = {}

def signal_handler(sig, frame):
    global g_running
    print(f'\n[launcher] signal {sig}, stopping...')
    g_running = False

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)


def save_metrics():
    """保存实时指标到 JSON"""
    try:
        tmp = METRICS_FILE + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(g_metrics, f, indent=2, allow_nan=False)
        os.rename(tmp, METRICS_FILE)
    except:
        try:
            def nan_fix(obj):
                if isinstance(obj, dict):
                    return {k: nan_fix(v) for k, v in obj.items()}
                if isinstance(obj, float) and (obj != obj):
                    return None
                return obj
            with open(tmp, 'w') as f:
                json.dump(nan_fix(g_metrics), f, indent=2)
            os.rename(tmp, METRICS_FILE)
        except:
            pass


def read_cpu_stats():
    """读取 /proc/stat CPU 统计"""
    stats = {}
    try:
        with open('/proc/stat') as f:
            for line in f:
                if not line.startswith('cpu'):
                    continue
                parts = line.split()
                core = parts[0]
                vals = [int(x) for x in parts[1:8]]
                stats[core] = {'total': sum(vals), 'idle': vals[3] + vals[4]}
    except:
        pass
    return stats


def run_pipeline(node_name):
    """运行单个在线 UKF Pipeline 进程 (v4: 专用二进制 + CSV)"""
    global g_running, g_processes, g_metrics

    cfg = NODES[node_name]
    ns, nm = cfg['ns'], cfg['nm']
    label, cpu_core = cfg['label'], cfg['cpu']
    pipeline_bin = os.path.join(SCRIPT_DIR, cfg['bin'])

    if not os.path.exists(pipeline_bin):
        print(f'[{node_name}] ERROR: binary not found: {pipeline_bin}')
        g_metrics[node_name] = {
            'label': label, 'ns': ns, 'nm': nm,
            'cpu_core': cpu_core, 'status': 'binary_missing',
            'updated_at': datetime.now().isoformat(),
        }
        return 0

    print(f'[{node_name}] starting: {label} (ns={ns}, nm={nm}, cpu={cpu_core})')
    print(f'[{node_name}] binary: {pipeline_bin}')

    g_metrics[node_name] = {
        'label': label, 'ns': ns, 'nm': nm,
        'cpu_core': cpu_core,
        'ts': 0, 'frames': 0, 'fps': 0,
        'rmse': 0, 'latency_us': 0,
        'cpu_pct': 0,
        'status': 'starting',
        'started_at': datetime.now().isoformat(),
    }

    # CSV 输出文件
    csv_path = f'/tmp/ukf_out_{node_name}.csv'
    log_path = f'/tmp/ukf_log_{node_name}.log'

    # 构建命令 (不需要 --node 参数, 二进制已专用化)
    cmd = ['sudo', '-S', 'taskset', '-c', str(cpu_core), pipeline_bin]
    print(f'[{node_name}] cmd: {" ".join(cmd)} > {csv_path}')

    csv_fp = open(csv_path, 'w', buffering=1)  # 行缓冲
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=csv_fp,
        stderr=subprocess.PIPE,
        bufsize=0,
        shell=False
    )
    g_processes[node_name] = proc

    # sudo 密码
    try:
        proc.stdin.write(b'user\n')
        proc.stdin.flush()
        proc.stdin.close()
    except:
        pass

    # 日志
    log_fp = open(log_path, 'w', buffering=1)
    log_fp.write(f'[{node_name}] v4 started at {datetime.now().isoformat()}\n')
    log_fp.write(f'[{node_name}] ns={ns} nm={nm} cpu={cpu_core} csv={csv_path}\n')
    log_fp.flush()

    # 跟踪指标 (从 stderr 心跳解析)
    frame_count = 0
    last_rmse = 0.0
    last_lat = 0.0
    total_start = time.time()
    last_metrics = time.time()
    stderr_lines = []

    prev_cpu = read_cpu_stats()
    cpu_core_key = f'cpu{cpu_core}'

    # stderr 读取线程
    def read_stderr():
        nonlocal frame_count, last_rmse, last_lat
        try:
            for line in proc.stderr:
                if not g_running:
                    break
                decoded = line.decode('utf-8', errors='replace').strip()
                if not decoded:
                    continue
                if len(stderr_lines) >= 200:
                    stderr_lines.pop(0)
                stderr_lines.append(decoded)
                ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                log_line = f'[{ts}] {decoded}'
                try:
                    log_fp.write(log_line + '\n')
                except ValueError:
                    pass
                print(f'[{node_name}] {decoded}', flush=True)

                # 解析心跳: "[ukf-5bus] t=4.0s frames=251 rmse=0.0000 lat=55us"
                if 'frames=' in decoded and 'rmse=' in decoded:
                    m_frames = re.search(r'frames=(\d+)', decoded)
                    m_rmse = re.search(r'rmse=([\d.]+)', decoded)
                    m_lat = re.search(r'lat=(\d+)us', decoded)
                    if m_frames:
                        frame_count = int(m_frames.group(1))
                    if m_rmse:
                        last_rmse = float(m_rmse.group(1))
                    if m_lat:
                        last_lat = float(m_lat.group(1))
                elif 'idle:' in decoded and 'frames processed' in decoded:
                    m = re.search(r'idle:\s*(\d+)\s*frames', decoded)
                    if m:
                        frame_count = int(m.group(1))
        except Exception:
            pass

    stderr_thread = threading.Thread(target=read_stderr, daemon=True)
    stderr_thread.start()

    # 启动失败检测
    time.sleep(3.0)
    if proc.poll() is not None and proc.returncode != 0:
        exit_code = proc.returncode
        err_text = '\n'.join(stderr_lines[-5:]) if stderr_lines else '(no stderr)'
        print(f'[{node_name}] FAILED: exit={exit_code} stderr={err_text}', flush=True)
        log_fp.write(f'[{node_name}] FAILED: exit={exit_code}\n{err_text}\n')
        log_fp.close()
        csv_fp.close()
        g_metrics[node_name].update({
            'status': f'failed(code={exit_code})',
            'updated_at': datetime.now().isoformat(),
        })
        save_metrics()
        return 0

    # 监控循环
    last_report = time.time()
    try:
        while g_running and proc.poll() is None:
            time.sleep(1.0)
            now = time.time()

            if now - last_metrics > METRICS_INTERVAL:
                elapsed = now - total_start
                fps = frame_count / elapsed if elapsed > 0.001 else 0

                cpu_pct = 0
                curr_cpu = read_cpu_stats()
                if cpu_core_key in curr_cpu and cpu_core_key in prev_cpu:
                    d_total = curr_cpu[cpu_core_key]['total'] - prev_cpu[cpu_core_key]['total']
                    d_idle = curr_cpu[cpu_core_key]['idle'] - prev_cpu[cpu_core_key]['idle']
                    if d_total > 0:
                        cpu_pct = round(100 * (d_total - d_idle) / d_total, 1)
                prev_cpu = curr_cpu

                g_metrics[node_name] = {
                    'label': label, 'ns': ns, 'nm': nm,
                    'cpu_core': cpu_core, 'cpu_pct': cpu_pct,
                    'ts': elapsed, 'frames': frame_count,
                    'fps': round(fps, 1),
                    'rmse': round(last_rmse, 5),
                    'latency_us': round(last_lat, 1),
                    'status': 'running',
                    'output_csv': csv_path,
                    'updated_at': datetime.now().isoformat(),
                }
                save_metrics()
                last_metrics = now

            if now - last_report > 5.0:
                elapsed = now - total_start
                fps = frame_count / elapsed if elapsed > 0.001 else 0
                print(f'[{node_name}] t={elapsed:.0f}s frames={frame_count} '
                      f'rmse={last_rmse:.4f} fps={fps:.1f} lat={last_lat:.0f}us '
                      f'cpu={g_metrics.get(node_name,{}).get("cpu_pct",0)}%',
                      flush=True)
                last_report = now

    except Exception as e:
        import traceback
        err_text = '\n'.join(stderr_lines[-5:]) if stderr_lines else '(no stderr)'
        tb = traceback.format_exc()
        print(f'[{node_name}] error: {e!r}', flush=True)
        log_fp.write(f'[{node_name}] ERROR: {e!r}\n{tb}\nstderr:\n{err_text}\n')
    finally:
        elapsed = time.time() - total_start
        final_fps = frame_count / elapsed if elapsed > 0.001 else 0
        final_cpu = g_metrics.get(node_name, {}).get('cpu_pct', 0)

        g_metrics[node_name] = {
            'label': label, 'ns': ns, 'nm': nm,
            'cpu_core': cpu_core, 'cpu_pct': final_cpu,
            'ts': elapsed, 'frames': frame_count,
            'fps': round(final_fps, 1),
            'rmse': round(last_rmse, 5),
            'latency_us': round(last_lat, 1),
            'status': 'done' if proc.poll() == 0 else 'stopped',
            'output_csv': csv_path,
            'updated_at': datetime.now().isoformat(),
        }
        save_metrics()
        try:
            log_fp.write(f'[{node_name}] done: {frame_count} frames fps={final_fps:.1f} '
                        f'rmse={last_rmse:.5f} lat={last_lat:.0f}us\n')
            log_fp.close()
        except:
            pass
        csv_fp.close()
        print(f'[{node_name}] done: {frame_count} frames fps={final_fps:.1f} '
              f'rmse={last_rmse:.5f} lat={last_lat:.0f}us csv={csv_path}', flush=True)

    return frame_count


def main():
    global g_running
    import argparse
    parser = argparse.ArgumentParser(
        description='Multi-node Online UKF Pipeline Launcher v4')
    parser.add_argument('--nodes', default='5bus,39bus,9bus',
                        help='Comma-separated node list')
    parser.add_argument('--speed', type=int, default=10,
                        help='FreeRTOS sim speed (0=fastest, 1=realtime)')
    args = parser.parse_args()

    node_list = [n.strip() for n in args.nodes.split(',')]
    for n in node_list:
        if n not in NODES:
            print(f'Unknown node: {n}')
            sys.exit(1)

    # 检查二进制
    missing = []
    for n in node_list:
        bin_path = os.path.join(SCRIPT_DIR, NODES[n]['bin'])
        if not os.path.exists(bin_path):
            missing.append(f'  {NODES[n]["bin"]} (for {n})')
    if missing:
        print('Missing binaries:')
        for m in missing:
            print(m)
        print('\nBuild with: make all')
        print('Or: make deploy HOST=user@<IP>')
        sys.exit(1)

    print(f'[launcher] nodes: {node_list}')
    print(f'[launcher] speed: {args.speed}')
    print(f'[launcher] CPU affinity:')
    for n in node_list:
        print(f'  {n}: CPU{NODES[n]["cpu"]} ({NODES[n]["label"]})')

    # 清理旧指标
    if os.path.exists(METRICS_FILE):
        os.remove(METRICS_FILE)

    # 并行启动
    threads = []
    for node_name in node_list:
        t = threading.Thread(target=run_pipeline, args=(node_name,), daemon=True)
        t.start()
        threads.append(t)
        time.sleep(0.5)

    # 等待完成
    try:
        for t in threads:
            while t.is_alive() and g_running:
                t.join(timeout=1.0)
    except KeyboardInterrupt:
        pass
    finally:
        g_running = False
        for name, proc in g_processes.items():
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()

    print('[launcher] all done')


if __name__ == '__main__':
    main()
