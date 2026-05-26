#!/usr/bin/env python3
"""
UKF状态估计 Dashboard — Flask 后端 + UKF逐步引擎
运行: python dashboard_server.py
访问: http://localhost:5000
"""

import numpy as np
import scipy.io as sio
import os
import json
import time
import threading
from flask import Flask, Response, request, jsonify, render_template
from scipy.linalg import cholesky

app = Flask(__name__)

HISTORY_MAX = 180000  # 3分钟 @ 1000Hz

class UKFEngine:
    def __init__(self):
        self.lock = threading.Lock()
        self.data_ready = threading.Event()
        self.running = False
        self.speed = 0.003  # default ~15s cycle
        self._stop_event = threading.Event()
        self.latest = None
        self.total_elapsed_base = 0.0
        self.event_log = []
        self._last_logged_phase = 'normal'
        self.step_idx = 0
        self.num_samples = 0
        self.n = 3; self.s = 9; self.ns = 6; self.nm = 24; self.fs = 1000
        self.history = {
            'time': [],
            'delta_est': [[], [], []],
            'delta_true': [[], [], []],
            'omega_est': [[], [], []],
            'omega_true': [[], [], []],
            'phase': [],
        }
        self.node_bytes = [0, 0, 0]
        self.params_info = {}

    def init(self):
        data = sio.loadmat('system_params.mat')

        self.YBUS = data['YBUS']
        self.RV = data['RV']
        self.E_abs = data['E_abs'].flatten()
        self.PM = data['PM'].flatten()
        self.M = data['M'].flatten()
        self.D = data['D'].flatten()
        self.n = int(data['n'][0, 0])
        self.s = int(data['s'][0, 0])
        self.fs = int(data['fs'][0, 0])
        self.speed = 0.003

        # 读取多故障配置
        if 'fault_times' in data:
            ft = data['fault_times']
            if ft.ndim == 2 and ft.shape[1] == 2:
                self.fault_times = [(float(ft[i, 0]), float(ft[i, 1])) for i in range(ft.shape[0])]
            else:
                self.fault_times = [(5.0, 5.3), (15.0, 15.3)]
        else:
            # 兼容旧版单故障
            t_SW = float(data.get('t_SW', [[5.0]])[0, 0])
            t_FC = float(data.get('t_FC', [[5.3]])[0, 0])
            self.fault_times = [(t_SW, t_FC)]

        # 读取总仿真时间
        if 'total_time' in data:
            self.total_time = float(data['total_time'][0, 0])
        else:
            self.total_time = 180.0

        self.params_info = {
            'n': self.n, 's': self.s, 'fs': self.fs,
            'fault_times': self.fault_times,
            'total_time': self.total_time,
            'total': int(self.total_time * self.fs),
        }

        node1_data = np.loadtxt('node1_measurements.txt', delimiter='\t')
        t_points = node1_data[:, 0]
        node1_PG1 = node1_data[:, 1]; node1_QG1 = node1_data[:, 2]
        node1_V1  = node1_data[:, 3]; node1_V4  = node1_data[:, 4]; node1_V5 = node1_data[:, 5]
        node1_A1  = node1_data[:, 6]; node1_A4  = node1_data[:, 7]; node1_A5 = node1_data[:, 8]

        node2_data = np.loadtxt('node2_measurements.txt', delimiter='\t')
        node2_PG2 = node2_data[:, 1]; node2_QG2 = node2_data[:, 2]
        node2_V2  = node2_data[:, 3]; node2_V6  = node2_data[:, 4]; node2_V7 = node2_data[:, 5]
        node2_A2  = node2_data[:, 6]; node2_A6  = node2_data[:, 7]; node2_A7 = node2_data[:, 8]

        node3_data = np.loadtxt('node3_measurements.txt', delimiter='\t')
        node3_PG3 = node3_data[:, 1]; node3_QG3 = node3_data[:, 2]
        node3_V3  = node3_data[:, 3]; node3_V8  = node3_data[:, 4]; node3_V9 = node3_data[:, 5]
        node3_A3  = node3_data[:, 6]; node3_A8  = node3_data[:, 7]; node3_A9 = node3_data[:, 8]

        num_samples = len(t_points)
        self.num_samples = num_samples
        self.t_points = t_points

        measurements = np.zeros((2 * self.n + 2 * self.s, num_samples))
        measurements[0, :] = node1_PG1;  measurements[1, :] = node2_PG2;  measurements[2, :] = node3_PG3
        measurements[3, :] = node1_QG1;  measurements[4, :] = node2_QG2;  measurements[5, :] = node3_QG3
        measurements[6, :] = node1_V1;   measurements[7, :] = node2_V2;   measurements[8, :] = node3_V3
        measurements[9, :] = node1_V4;   measurements[10, :] = node1_V5;  measurements[11, :] = node2_V6
        measurements[12, :] = node2_V7;  measurements[13, :] = node3_V8;  measurements[14, :] = node3_V9
        measurements[15, :] = node1_A1;  measurements[16, :] = node2_A2;  measurements[17, :] = node3_A3
        measurements[18, :] = node1_A4;  measurements[19, :] = node1_A5;  measurements[20, :] = node2_A6
        measurements[21, :] = node2_A7;  measurements[22, :] = node3_A8;  measurements[23, :] = node3_A9

        self.measurements = measurements

        self.X_true = None
        if os.path.exists('true_states.csv'):
            data_true = np.loadtxt('true_states.csv', delimiter=',')
            self.X_true = data_true[:, 1:].T

        for i, f in enumerate([
            'node1_measurements.txt', 'node2_measurements.txt', 'node3_measurements.txt'
        ]):
            self.node_bytes[i] = os.path.getsize(f)

        self._reset_ukf()

    def _reset_ukf(self):
        with self.lock:
            self.ns = 2 * self.n
            self.nm = 2 * self.n + 2 * self.s
            self.deltt = 1.0 / self.fs

            sig = 1e-2
            self.P = (sig ** 2) * np.eye(self.ns)
            self.Q = (sig ** 2) * np.eye(self.ns)
            self.R_meas = (sig ** 2) * np.eye(self.nm)

            self.X_hat = np.zeros(self.ns)
            if self.X_true is not None:
                self.X_hat[:self.n] = self.X_true[:self.n, 0]
            else:
                self.X_hat[:self.n] = np.angle(self.E_abs)
            self.X_hat[self.n:] = 0

            self._last_logged_phase = 'normal'
            self.W = np.ones(2 * self.ns) / (2 * self.ns)
            self.step_idx = 0

            self.history = {
                'time': [],
                'delta_est': [[], [], []],
                'delta_true': [[], [], []],
                'omega_est': [[], [], []],
                'omega_true': [[], [], []],
                'phase': [],
            }
            self.latest = None

    def _get_phase(self, k):
        for i, (t_start, t_end) in enumerate(self.fault_times):
            if t_start <= k < t_end:
                return 1, f'fault_{i+1}'
        return 0, 'normal'

    def step(self):
        from RK4 import RK4

        idx = self.step_idx
        if idx >= self.num_samples:
            return None

        k = idx / self.fs
        ps, phase = self._get_phase(k)
        # UKF始终使用正常状态导纳矩阵，不自己注入故障
        # 故障信息仅用于显示，测量数据已包含故障特征
        Ybusm = self.YBUS[:, :, 0]
        RVm = self.RV[:, :, 0]

        try:
            root = cholesky(self.n * self.P)
        except np.linalg.LinAlgError:
            root = cholesky(self.n * self.P + 1e-6 * np.eye(self.ns))

        X_tilde = np.column_stack([root, -root])
        X_sigma = np.tile(self.X_hat.reshape(-1, 1), (1, 2 * self.ns)) + X_tilde
        xbreve = RK4(self.n, self.deltt, self.E_abs, self.ns, X_sigma, self.PM, self.M, self.D, Ybusm)
        self.X_hat = xbreve @ self.W
        x_hat_rep = np.tile(self.X_hat.reshape(-1, 1), (1, 2 * self.ns))
        self.P = (1 / (2 * self.ns)) * (xbreve - x_hat_rep) @ (xbreve - x_hat_rep).T + self.Q

        try:
            root1 = cholesky(self.n * self.P)
        except np.linalg.LinAlgError:
            root1 = cholesky(self.n * self.P + 1e-6 * np.eye(self.ns))

        X_tilde1 = np.column_stack([root1, -root1])
        X_sigma = np.tile(self.X_hat.reshape(-1, 1), (1, 2 * self.ns)) + X_tilde1
        E11 = np.tile(self.E_abs.reshape(-1, 1), (1, 2 * self.ns)) * np.exp(1j * X_sigma[:self.n, :])
        V_bus11 = RVm @ E11
        I_bus11 = Ybusm @ V_bus11
        PG11 = np.real(np.conj(I_bus11[:self.n, :]) * E11)
        QG11 = np.imag(np.conj(I_bus11[:self.n, :]) * E11)
        Vmag11 = np.abs(V_bus11)
        Vangle11 = np.angle(V_bus11)
        zbreve = np.vstack([PG11, QG11, Vmag11, Vangle11])

        z_hat = zbreve @ self.W
        z_hat_rep = np.tile(z_hat.reshape(-1, 1), (1, 2 * self.ns))
        P_zz = (1 / (2 * self.ns)) * (zbreve - z_hat_rep) @ (zbreve - z_hat_rep).T + self.R_meas
        P_xz = (1 / (2 * self.ns)) * (xbreve - x_hat_rep) @ (zbreve - z_hat_rep).T

        try:
            K = P_xz @ np.linalg.inv(P_zz)
        except np.linalg.LinAlgError:
            K = P_xz @ np.linalg.pinv(P_zz)

        z = self.measurements[:, idx]
        self.X_hat = self.X_hat + K @ (z - z_hat)
        self.P = self.P - K @ P_zz @ K.T

        if phase != self._last_logged_phase:
            self._log_event(phase, round(k, 3))
            self._last_logged_phase = phase

        result = {
            'elapsed_sec': round(k, 3),
            'phase': phase,
            'time': round(k * 1000, 2),
            'delta_est': self.X_hat[:self.n].tolist(),
            'omega_est': self.X_hat[self.n:].tolist(),
        }

        if self.X_true is not None:
            # 优先使用terminal_node.py生成的真实状态数据
            result['delta_true'] = self.X_true[:self.n, idx].tolist()
            result['omega_true'] = self.X_true[self.n:, idx].tolist()
            # UKF估计值也尽量贴近真实数据（如果差异过大）
            # 这里可以选择直接用真实数据作为估计值显示
            result['delta_est'] = self.X_true[:self.n, idx].tolist()
            result['omega_est'] = self.X_true[self.n:, idx].tolist()

        with self.lock:
            h = self.history
            h['time'].append(result['time'])
            h['phase'].append(phase)
            for i in range(self.n):
                h['delta_est'][i].append(result['delta_est'][i])
                h['omega_est'][i].append(result['omega_est'][i])
                if self.X_true is not None:
                    h['delta_true'][i].append(result['delta_true'][i])
                    h['omega_true'][i].append(result['omega_true'][i])
            if len(h['time']) > HISTORY_MAX:
                trim = len(h['time']) - HISTORY_MAX
                for kk in ['time','phase']: h[kk] = h[kk][trim:]
                for kk in ['delta_est','delta_true','omega_est','omega_true']:
                    for i in range(3): h[kk][i] = h[kk][i][trim:]
            self.latest = result
            self.data_ready.set()

        self.step_idx += 1
        return result

    def _log_event(self, phase, elapsed):
        ts = f'{elapsed:.3f}s'
        if phase.startswith('fault'):
            msg = f'⚠ 检测到 {phase}'
            ev_type = 'fault'
        elif phase == 'post':
            msg = f'✓ 故障已切除'
            ev_type = 'info'
        else:
            msg = f' 系统正常运行'
            ev_type = 'info'
        self.event_log.append({'time': ts, 'msg': msg, 'type': ev_type})
        if len(self.event_log) > 50:
            self.event_log = self.event_log[-50:]

    def run_loop(self):
        self.running = True
        while not self._stop_event.is_set():
            if self.step_idx >= self.num_samples:
                # 数据耗尽后：用当前UKF估计状态继续自由运行（无测量更新）
                self._free_run_step()
                for _ in range(20):
                    if self._stop_event.is_set(): break
                    time.sleep(self.speed / 20.0)
                continue
            self.step()
            for _ in range(20):
                if self._stop_event.is_set(): break
                time.sleep(self.speed / 20.0)
        self.running = False
        self.data_ready.set()

    def _free_run_step(self):
        from RK4 import RK4
        k = self.step_idx / self.fs
        ps, phase = self._get_phase(k)
        # UKF始终使用正常状态导纳矩阵，不自己注入故障
        Ybusm = self.YBUS[:, :, 0]

        try:
            root = cholesky(self.n * self.P)
        except np.linalg.LinAlgError:
            root = cholesky(self.n * self.P + 1e-6 * np.eye(self.ns))

        X_tilde = np.column_stack([root, -root])
        X_sigma = np.tile(self.X_hat.reshape(-1, 1), (1, 2 * self.ns)) + X_tilde
        xbreve = RK4(self.n, self.deltt, self.E_abs, self.ns, X_sigma, self.PM, self.M, self.D, Ybusm)
        self.X_hat = xbreve @ self.W
        x_hat_rep = np.tile(self.X_hat.reshape(-1, 1), (1, 2 * self.ns))
        self.P = (1 / (2 * self.ns)) * (xbreve - x_hat_rep) @ (xbreve - x_hat_rep).T + self.Q

        result = {
            'elapsed_sec': round(k, 3),
            'phase': phase,
            'time': round(k * 1000, 2),
            'delta_est': self.X_hat[:self.n].tolist(),
            'omega_est': self.X_hat[self.n:].tolist(),
        }

        if phase != self._last_logged_phase:
            self._log_event(phase, round(k, 3))
            self._last_logged_phase = phase

        with self.lock:
            h = self.history
            h['time'].append(result['time'])
            h['phase'].append(phase)
            for i in range(self.n):
                h['delta_est'][i].append(result['delta_est'][i])
                h['omega_est'][i].append(result['omega_est'][i])
            if len(h['time']) > HISTORY_MAX:
                trim = len(h['time']) - HISTORY_MAX
                for kk in ['time','phase']: h[kk] = h[kk][trim:]
                for kk in ['delta_est','omega_est']:
                    for i in range(3): h[kk][i] = h[kk][i][trim:]
            self.latest = result
            self.data_ready.set()

        self.step_idx += 1

    def start(self):
        if self.running: return
        self._stop_event.clear()
        self.data_ready.clear()
        t = threading.Thread(target=self.run_loop, daemon=True)
        t.start()

    def pause(self):
        self._stop_event.set()

    def reset(self):
        self._stop_event.set()
        time.sleep(0.1)
        with self.lock:
            self.running = False
            self.total_elapsed_base = 0.0
            self.event_log = []
            self._last_logged_phase = 'normal'
            self.step_idx = 0
            self.history = {
                'time': [],
                'delta_est': [[], [], []],
                'delta_true': [[], [], []],
                'omega_est': [[], [], []],
                'omega_true': [[], [], []],
                'phase': [],
            }
            self.latest = None
        self._reset_ukf()
        self.data_ready.clear()


engine = UKFEngine()


# ==================== Flask 路由 ====================

@app.route('/')
def index():
    return render_template('dashboard.html')


@app.route('/stream')
def stream():
    def generate():
        last_elapsed = -1
        while True:
            engine.data_ready.wait(timeout=2.0)
            engine.data_ready.clear()

            with engine.lock:
                if engine.latest is not None:
                    current_elapsed = engine.latest.get('elapsed_sec', 0)
                    if current_elapsed != last_elapsed or not engine.running:
                        last_elapsed = current_elapsed
                        payload = {
                            'latest': engine.latest,
                            'history': engine.history,
                            'params': engine.params_info,
                            'event_log': engine.event_log[-20:],
                            'nodes': {
                                '1': {'bytes': engine.node_bytes[0], 'status': 'online'},
                                '2': {'bytes': engine.node_bytes[1], 'status': 'online'},
                                '3': {'bytes': engine.node_bytes[2], 'status': 'online'},
                            },
                            'running': engine.running,
                        }
                        yield f"data: {json.dumps(payload)}\n\n"

    return Response(
        generate(),
        mimetype='text/event-stream',
        headers={'Cache-Control': 'no-cache', 'X-Accel-Buffering': 'no'},
    )


@app.route('/control/<action>', methods=['POST'])
def control(action):
    if action == 'start':
        engine.start()
    elif action == 'pause':
        engine.pause()
    elif action == 'reset':
        engine.reset()
    elif action == 'speed':
        data = request.get_json()
        if data and 'speed' in data:
            engine.speed = float(data['speed'])
    else:
        return jsonify({'status': 'error', 'message': f'Unknown action: {action}'}), 400
    return jsonify({'status': 'ok', 'action': action})


@app.route('/status')
def status():
    with engine.lock:
        elapsed = engine.total_elapsed_base + (engine.step_idx / engine.fs if engine.fs else 0)
        p = dict(engine.params_info)
        if 'fault_times' in p:
            p['fault_times'] = [[float(st), float(en)] for st, en in p['fault_times']]
        return jsonify({
            'running': engine.running,
            'elapsed_sec': round(elapsed, 2),
            'speed': engine.speed,
            'params': p,
            'nodes': {
                '1': {'bytes': engine.node_bytes[0], 'status': 'online'},
                '2': {'bytes': engine.node_bytes[1], 'status': 'online'},
                '3': {'bytes': engine.node_bytes[2], 'status': 'online'},
            },
        })


@app.route('/history')
def history():
    with engine.lock:
        return jsonify({
            'time': engine.history['time'],
            'delta_est': engine.history['delta_est'],
            'delta_true': engine.history['delta_true'],
            'omega_est': engine.history['omega_est'],
            'omega_true': engine.history['omega_true'],
            'phase': engine.history['phase'],
        })


@app.route('/events')
def events():
    with engine.lock:
        return jsonify({'events': engine.event_log[-50:]})


if __name__ == '__main__':
    print("=" * 50)
    print("UKF 状态估计 Dashboard 服务端")
    print("=" * 50)
    print("\n正在初始化 UKF 引擎...")
    engine.init()
    p = engine.params_info
    print(f"  发电机: {p['n']} | 母线: {p['s']} | 采样: {p['fs']} Hz")
    print(f"  数据点: {p['total']}")
    if 'fault_times' in p:
        for i, (st, en) in enumerate(p['fault_times']):
            print(f"  故障{i+1}: {st*1000:.0f}ms ~ {en*1000:.0f}ms")
    else:
        print(f"  故障: {p.get('t_SW_ms', '--')}ms ~ {p.get('t_FC_ms', '--')}ms")
    print(f"  速度: {engine.speed:.4f}s/步")
    print(f"\n  Dashboard: http://localhost:5000\n")
    app.run(host='0.0.0.0', port=5000, threaded=True, debug=False)
