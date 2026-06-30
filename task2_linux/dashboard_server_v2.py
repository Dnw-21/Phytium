#!/usr/bin/env python3
"""
dashboard_server_v2.py — Task2 多节点 UKF 对比面板
==================================================
运行在飞腾派上，直接读取本地 /tmp/ukf_*.npz 和 /tmp/ukf_metrics.json。
浏览器访问: http://192.168.88.10:5001
VNC访问:    vncviewer 192.168.88.10:5903
"""

import os, sys, json, time, threading, subprocess
import numpy as np
from flask import Flask, render_template, jsonify, request

app = Flask(__name__, template_folder='templates')

CACHE_FILES = {
    '5bus':  '/tmp/ukf_results_5bus.npz',
    '39bus': '/tmp/ukf_results_39bus.npz',
    '9bus':  '/tmp/ukf_results_9bus.npz',
}
NODE_META = {
    '5bus':  {'ns': 4,  'n_gen': 2,  'n_bus': 5,  'label': 'IEEE 5-Bus'},
    '39bus': {'ns': 20, 'n_gen': 10, 'n_bus': 39, 'label': 'IEEE 39-Bus'},
    '9bus':  {'ns': 6,  'n_gen': 3,  'n_bus': 9,  'label': 'IEEE 9-Bus'},
}
NODE_CPU_MAP = {'5bus': 0, '39bus': 2, '9bus': 1}  # 与 launch_ukf_multi.py 保持一致

# SHM 区域配置 (与 freertos/inc/shm_data.h 一致)
SHM_REGIONS = {
    '5bus':  {'base': 0xC8100000, 'total_size': 0x40000,  'frame_size': 64},   # 256 KiB
    '39bus': {'base': 0xC8140000, 'total_size': 0x80000,  'frame_size': 400}, # 512 KiB
    '9bus':  {'base': 0xC81C0000, 'total_size': 0x20000,  'frame_size': 104}, # 128 KiB
}

cache = {}
cache_time = {}
cache_lock = threading.Lock()

# 本地 CPU 采样 (用于利用率计算)
_cpu_prev = {}
_cpu_lock = threading.Lock()


def load_cache():
    """加载多节点 UKF 结果缓存 (支持 npz 和 json 格式)"""
    global cache, cache_time
    for node_name, npz_path in CACHE_FILES.items():
        try:
            mtime = os.path.getmtime(npz_path)
            if node_name in cache_time and mtime <= cache_time[node_name]:
                continue
            d = np.load(npz_path, allow_pickle=True)
            ns = NODE_META[node_name]['ns']
            with cache_lock:
                X_est = d['X_est']
                entry = {
                    'time': d['time'].tolist(),
                    'X_est': [X_est[i].tolist() for i in range(ns)],
                    'rmse': d['rmse'].tolist(),
                }
                # Z 是可选字段 (新版本才有, 旧版本 npz 跳过)
                if 'Z' in d.files:
                    Z = d['Z']
                    entry['Z'] = [Z[i].tolist() for i in range(Z.shape[0])]
                else:
                    entry['Z'] = []
                cache[node_name] = entry
                cache_time[node_name] = mtime
        except:
            # 尝试 JSON 格式
            json_path = npz_path.replace('.npz', '.json')
            try:
                mtime = os.path.getmtime(json_path)
                if node_name in cache_time and mtime <= cache_time[node_name]:
                    continue
                with open(json_path) as f:
                    d = json.load(f)
                with cache_lock:
                    entry = {
                        'time': d.get('time', []),
                        'X_est': d.get('X_est', []),
                        'rmse': d.get('rmse', []),
                        'Z': d.get('Z', []),
                    }
                    cache[node_name] = entry
                    cache_time[node_name] = mtime
            except:
                pass


def read_local_cpu():
    """读取本机 /proc/stat 获取各核 CPU 时间"""
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


def get_cpu_usage():
    """计算各核 CPU 利用率 (delta)"""
    global _cpu_prev
    curr = read_local_cpu()
    result = {}
    with _cpu_lock:
        for core, cur in curr.items():
            prev = _cpu_prev.get(core)
            if prev and cur['total'] > prev['total']:
                d_total = cur['total'] - prev['total']
                d_idle = cur['idle'] - prev['idle']
                if d_total > 0:
                    pct = round(100 * (d_total - d_idle) / d_total, 1)
                    result[core] = min(100, max(0, pct))
            _cpu_prev[core] = cur
    return result


def get_local_mem():
    """读取本机 /proc/meminfo"""
    try:
        with open('/proc/meminfo') as f:
            ml = f.read()
        total = avail = 0
        for line in ml.split('\n'):
            if 'MemTotal' in line:
                total = int(line.split()[1])
            if 'MemAvailable' in line:
                avail = int(line.split()[1])
        used = total - avail
        return {
            'total_mb': total // 1024,
            'used_mb': used // 1024,
            'pct': round(100 * used / max(total, 1), 1)
        }
    except:
        return {'total_mb': 0, 'used_mb': 0, 'pct': 0}


def get_freertos_status():
    """读取 FreeRTOS 状态 (本地 remoteproc + devmem)"""
    status = {'state': 'offline', 'hb': 0, 'wi': 0}
    try:
        with open('/sys/class/remoteproc/remoteproc0/state') as f:
            status['state'] = f.read().strip()
    except:
        pass
    try:
        import subprocess
        r = subprocess.run(['sudo', 'busybox', 'devmem', '0xC8000000', '32'],
                          capture_output=True, text=True, timeout=2)
        if r.returncode == 0:
            status['hb'] = int(r.stdout.strip(), 16)
    except:
        pass
    try:
        import subprocess
        r = subprocess.run(['sudo', 'busybox', 'devmem', '0xC8000008', '32'],
                          capture_output=True, text=True, timeout=2)
        if r.returncode == 0:
            status['wi'] = int(r.stdout.strip(), 16)
    except:
        pass
    return status


# ─── Routes ───

# ── 资源监控辅助函数 ──
def get_core_info():
    """读取 4 核的实时频率与类型 (A55/A76), CPU 3 不可用时优雅降级"""
    cores = {}
    # 核类型表: CPU 0/1=A55 小核, CPU 2=A76 大核, CPU 3=FreeRTOS 占用 (Linux 不可见)
    core_types = {0: 'A55', 1: 'A55', 2: 'A76', 3: 'A76'}
    core_roles = {0: '5bus', 1: '9bus', 2: '39bus', 3: 'FreeRTOS'}
    for c in range(4):
        freq = freq_max = 0
        try:
            freq = int(open(f'/sys/devices/system/cpu/cpu{c}/cpufreq/scaling_cur_freq').read().strip()) // 1000
            freq_max = int(open(f'/sys/devices/system/cpu/cpu{c}/cpufreq/cpuinfo_max_freq').read().strip()) // 1000
        except Exception:
            pass
        cores[f'cpu{c}'] = {
            'freq_mhz': freq,
            'max_mhz': freq_max,
            'type': core_types[c],
            'role': core_roles[c],
        }
    return cores


def get_shm_status():
    """读取 3 个 SHM ring buffer 水位 (一次 sudo 调用读全部 12 个地址)"""
    import subprocess as sp
    shm = {}
    # 构造一条 shell 命令, 串行读 3 节点 × 4 字段 = 12 次 devmem
    cmds = []
    for name, info in SHM_REGIONS.items():
        b = info['base']
        cmds.append(f'sudo busybox devmem {b:#x} 32')      # wi
        cmds.append(f'sudo busybox devmem {b+4:#x} 32')    # ri
        cmds.append(f'sudo busybox devmem {b+8:#x} 32')    # count
        cmds.append(f'sudo busybox devmem {b+12:#x} 32')   # frame_size
    shell_cmd = '\n'.join(cmds)
    try:
        r = sp.run(['bash', '-c', shell_cmd], capture_output=True, text=True, timeout=6)
        if r.returncode != 0:
            for name in SHM_REGIONS:
                shm[name] = {'error': f'devmem rc={r.returncode}'}
            return shm
        lines = r.stdout.strip().split('\n')
        idx = 0
        for name, info in SHM_REGIONS.items():
            try:
                if idx + 3 >= len(lines) or not lines[idx].strip().startswith('0x'):
                    shm[name] = {'error': f'parse fail at line {idx}'}
                    idx += 4
                    continue
                wi = int(lines[idx].strip(), 16)
                ri = int(lines[idx+1].strip(), 16)
                cnt = int(lines[idx+2].strip(), 16)
                fs = int(lines[idx+3].strip(), 16)
                idx += 4
                if fs == 0:
                    fs = info['frame_size']  # fallback to known value
                data_size = info['total_size'] - 16  # 减去 16 字节头
                filled_bytes = (wi - ri) % data_size
                filled_pct = round(100 * filled_bytes / data_size, 1)
                frames_in_buf = filled_bytes // fs
                shm[name] = {
                    'wi': wi, 'ri': ri, 'count': cnt, 'frame_size': fs,
                    'filled_bytes': filled_bytes,
                    'data_size': data_size,
                    'fill_pct': filled_pct,
                    'frames_in_buf': frames_in_buf,
                    'cpu_core': NODE_CPU_MAP.get(name, -1),
                }
            except Exception as e:
                shm[name] = {'error': str(e)[:50]}
    except Exception as e:
        for name in SHM_REGIONS:
            shm[name] = {'error': str(e)[:50]}
    return shm


def get_ukf_processes():
    """读取 3 个 ukf_pipeline 进程的资源 (CPU/RSS/启动时长)"""
    import subprocess as sp
    procs = {}
    for node in ['5bus', '39bus', '9bus']:
        try:
            # 用 --node Xbus 锁定, 避免 9bus 误匹配 39bus (子串问题)
            r = sp.run(['pgrep', '-f', f'ukf_pipeline --node {node}'],
                       capture_output=True, text=True, timeout=1)
            pids = r.stdout.strip().split()
            if not pids:
                procs[f'ukf-{node}'] = {'error': 'not running'}
                continue
            pid = pids[0]
            r2 = sp.run(['ps', '-o', 'pid,pcpu,pmem,rss,etime,comm', '-p', pid],
                        capture_output=True, text=True, timeout=1)
            lines = r2.stdout.strip().split('\n')
            if len(lines) >= 2:
                parts = lines[1].split()
                procs[f'ukf-{node}'] = {
                    'pid': int(parts[0]),
                    'pcpu': float(parts[1]),
                    'pmem': float(parts[2]),
                    'rss_mb': int(parts[3]) // 1024,
                    'etime': parts[4],
                    'cpu_core': NODE_CPU_MAP.get(node, -1),
                }
            else:
                procs[f'ukf-{node}'] = {'error': 'ps parse fail'}
        except Exception as e:
            procs[f'ukf-{node}'] = {'error': str(e)[:50]}
    return procs


@app.route('/api/resources')
def api_resources():
    """返回实时资源监控: 4 核频率/SHM 水位/3 个 UKF 进程 CPU+RSS"""
    return jsonify({
        'cores': get_core_info(),
        'shm': get_shm_status(),
        'processes': get_ukf_processes(),
    })


# ─── Routes ───

@app.route('/')
def index():
    return render_template('dashboard_v2.html')


@app.route('/api/status')
def api_status():
    load_cache()
    cpu = get_cpu_usage()
    mem = get_local_mem()
    frtos = get_freertos_status()

    # 计算总 CPU 利用率
    n_cores = max(len(cpu), 1)
    cpu_total = round(sum(cpu.values()) / n_cores, 1)

    nodes = []
    for node_name in ['5bus', '39bus', '9bus']:
        with cache_lock:
            c = cache.get(node_name, {})
        t_list = c.get('time', [])
        rmse_list = c.get('rmse', [])
        n = len(t_list)
        meta = NODE_META[node_name]
        nodes.append({
            'id': node_name,
            'label': meta['label'],
            'gen': meta['n_gen'],
            'bus': meta['n_bus'],
            'measurements': 2 * meta['n_gen'] + 2 * meta['n_bus'],
            'state_dim': meta['ns'],
            'status': 'active' if n > 0 else 'idle',
            'frames': n,
            'sim_time': t_list[-1] if n > 0 else 0,
            'rmse': round(rmse_list[-1], 5) if n > 0 else 0,
        })

    return jsonify({
        'status': 'running' if any(n['frames'] > 0 for n in nodes) else 'idle',
        'nodes': nodes,
        'cpu': {'cores': cpu, 'total': cpu_total},
        'mem': mem,
        'frtos': frtos,
    })


@app.route('/api/history')
def api_history():
    node = request.args.get('node', '5bus')
    load_cache()
    with cache_lock:
        c = cache.get(node, {})
    t_list = c.get('time', [])
    X_est = c.get('X_est', [[]])
    rmse_list = c.get('rmse', [])
    Z_list = c.get('Z', [])
    if not t_list:
        return jsonify({'time': [], 'states': [], 'rmse': [], 'Z_dim': 0})

    W = 10
    t_max = t_list[-1]
    t_min = max(0, t_max - W)
    idx = [i for i, t in enumerate(t_list) if t >= t_min]

    ns = NODE_META.get(node, {}).get('ns', 4)
    result = {
        'time': [t_list[i] for i in idx],
        'rmse': [rmse_list[i] for i in idx],
        'Z_dim': len(Z_list),
    }
    for i in range(ns):
        if i < len(X_est):
            result[f'state_{i}'] = [X_est[i][j] for j in idx]
    result['states'] = [[X_est[i][j] for j in idx] for i in range(min(ns, len(X_est)))]
    # 测量数据 Z (按维度, 每个维度是一个数组) — key 命名 Z_0..Z_{N-1}
    for i, z in enumerate(Z_list):
        result[f'Z_{i}'] = [z[j] for j in idx]
    return jsonify(result)


@app.route('/api/overview')
def api_overview():
    node = request.args.get('node', '5bus')
    load_cache()
    with cache_lock:
        c = cache.get(node, {})
    t_list = c.get('time', [])
    X_est = c.get('X_est', [[]])
    n = len(t_list)
    ns = NODE_META.get(node, {}).get('ns', 4)
    if n == 0:
        return jsonify({'time': [], 'states': []})
    step = max(1, n // 800)
    result = {'time': t_list[::step]}
    for i in range(ns):
        if i < len(X_est):
            result[f'state_{i}'] = X_est[i][::step]
    result['states'] = [X_est[i][::step] for i in range(min(ns, len(X_est)))]
    return jsonify(result)


@app.route('/api/compare')
def api_compare():
    """返回三节点对比数据 (发电机数, 状态维, 绑定核, 帧数, RMSE, 延迟, FPS, CPU%)"""
    load_cache()

    # 读取实时指标
    metrics = {}
    try:
        with open('/tmp/ukf_metrics.json') as f:
            metrics = json.load(f)
    except:
        pass

    result = {}
    for node_name in ['5bus', '39bus', '9bus']:
        with cache_lock:
            c = cache.get(node_name, {})
        t_list = c.get('time', [])
        rmse_list = c.get('rmse', [])
        meta = NODE_META[node_name]
        n = len(t_list)
        m = metrics.get(node_name, {})

        # 从 npz 数据估算 FPS (fallback)
        fallback_fps = 0
        if n > 1 and len(t_list) >= 2:
            dt = t_list[-1] - t_list[0]
            if dt > 0:
                fallback_fps = round(n / dt, 1)

        result[node_name] = {
            'label': meta['label'],
            'n_gen': meta['n_gen'],
            'ns': meta['ns'],
            'n_bus': meta['n_bus'],
            'cpu_core': NODE_CPU_MAP.get(node_name, -1),
            'frames': m.get('frames', n),
            'sim_time': t_list[-1] if n > 0 else 0,
            'rmse': round(rmse_list[-1], 5) if n > 0 else 0,
            'fps': m.get('fps', fallback_fps),
            'latency_us': m.get('latency_us', 0),
            'cpu_pct': m.get('cpu_pct', 0),
            'status': m.get('status', 'active' if n > 0 else 'idle'),
        }
    return jsonify(result)


@app.route('/api/logs')
def api_logs():
    node = request.args.get('node', '5bus')
    log_path = f'/tmp/ukf_log_{node}.log'
    try:
        with open(log_path) as f:
            lines = f.readlines()
        return jsonify({'node': node, 'lines': [l.strip() for l in lines[-50:]]})
    except:
        return jsonify({'node': node, 'lines': []})


@app.route('/api/control', methods=['POST'])
def api_control():
    """统一控制接口: start/pause/reset/speed
       通过 shell 调用 board 上的 start_sim_nodes / reset_shm 等二进制。
       这些二进制自行通过 RPMsg 给 FreeRTOS 发命令, 不需要 dashboard 关心。
    """
    data = request.get_json(force=True, silent=True) or {}
    action = data.get('action', '')
    nodes = data.get('nodes', ['5bus', '39bus', '9bus'])
    speed = data.get('speed', 1)
    cwd = '/home/user/Phytium/task2_linux'

    try:
        if action == 'start':
            # 启动 sim 进程 (它会通过 RPMsg 给 FreeRTOS 发 SPEED+START)
            # start_sim_nodes 接受 1~3 个节点名, 一次启动
            subprocess.Popen(
                ['sudo', './start_sim_nodes'] + nodes,
                cwd=cwd,
                stdout=open(f'{cwd}/logs/sim.log', 'a'),
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,
                start_new_session=True,
            )
            return jsonify({'status': 'ok', 'action': 'start', 'nodes': nodes})

        elif action == 'pause':
            # 暂存当前 sim 状态 (简单实现: 杀掉 sim 进程, 但保留数据)
            subprocess.run(['sudo', 'pkill', '-f', 'start_sim_nodes'],
                          cwd=cwd, capture_output=True)
            return jsonify({'status': 'ok', 'action': 'pause'})

        elif action == 'reset':
            # 重置 SHM + 杀掉 sim 进程
            subprocess.run(['sudo', './reset_shm'],
                          cwd=cwd, capture_output=True, timeout=5)
            subprocess.run(['sudo', 'pkill', '-f', 'start_sim_nodes'],
                          cwd=cwd, capture_output=True)
            # 清空缓存, 强制重新加载 npz
            global cache, cache_time
            with cache_lock:
                cache = {}
                cache_time = {}
            return jsonify({'status': 'ok', 'action': 'reset'})

        elif action == 'speed':
            # 调速: 通过 sim 进程的 stdin? 这里简化为写个标记文件
            # FreeRTOS 端的 sim 任务有 sim_speed 全局变量, 但目前只能通过 RPMsg 重发命令
            return jsonify({'status': 'ok', 'action': 'speed', 'speed': speed,
                            'message': '调速需重启 sim'})

        else:
            return jsonify({'status': 'error', 'message': f'unknown action: {action}'}), 400

    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)[:200]}), 500


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=5001)
    args = parser.parse_args()
    print(f'[dashboard] http://0.0.0.0:{args.port}')
    print(f'[dashboard] 浏览器: http://192.168.88.10:{args.port}')
    print(f'[dashboard] VNC:    vncviewer 192.168.88.10:5903')
    app.run(host='0.0.0.0', port=args.port, threaded=True)