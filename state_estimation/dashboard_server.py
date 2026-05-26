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

# ==================== UKF 逐步引擎 ====================

class UKFEngine:
    def __init__(self):
        self.lock = threading.Lock()
        self.data_ready = threading.Event()
        self.running = False
        self.finished = False
        self.speed = 0.25
        self._stop_event = threading.Event()
        self.latest = None
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
        self.num_normal = int(data['num_normal'][0, 0])
        self.num_fault = int(data['num_fault'][0, 0])
        self.t_SW = float(data['t_SW'][0, 0])
        self.t_FC = float(data['t_FC'][0, 0])

        self.params_info = {
            'n': self.n, 's': self.s, 'fs': self.fs,
            'num_normal': self.num_normal, 'num_fault': self.num_fault,
            'total': self.num_normal + self.num_fault,
            't_SW_ms': self.t_SW * 1000, 't_FC_ms': self.t_FC * 1000,
        }

        # 读取节点测量数据
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
        self.num_samples = num_samples
        self.t_points = t_points

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
            self.X_hat[:self.n] = np.angle(self.E_abs)
            self.X_hat[self.n:] = 0

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
            self.finished = False

    def _get_phase(self, k):
        if k <= self.t_SW:
            return 0, 'normal'
        elif k <= self.t_FC:
            return 1, 'fault'
        else:
            return 2, 'post'

    def step(self):
        from RK4 import RK4

        idx = self.step_idx
        if idx >= self.num_samples:
            return None

        k = idx / self.fs
        ps, phase = self._get_phase(k)
        Ybusm = self.YBUS[:, :, ps]
        RVm = self.RV[:, :, ps]

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
        I11 = Ybusm @ E11
        PG11 = np.real(E11 * np.conj(I11))
        QG11 = np.imag(E11 * np.conj(I11))
        Vmag11 = np.abs(RVm @ E11)
        Vangle11 = np.angle(RVm @ E11)
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

        result = {
            'step': idx + 1, 'total': self.num_samples,
            'phase': phase,
            'time': round(k * 1000, 2),
            'delta_est': self.X_hat[:self.n].tolist(),
            'omega_est': self.X_hat[self.n:].tolist(),
        }

        if self.X_true is not None:
            result['delta_true'] = self.X_true[:self.n, idx].tolist()
            result['omega_true'] = self.X_true[self.n:, idx].tolist()

        with self.lock:
            self.history['time'].append(result['time'])
            self.history['phase'].append(phase)
            for i in range(self.n):
                self.history['delta_est'][i].append(result['delta_est'][i])
                self.history['omega_est'][i].append(result['omega_est'][i])
                if self.X_true is not None:
                    self.history['delta_true'][i].append(result['delta_true'][i])
                    self.history['omega_true'][i].append(result['omega_true'][i])
            self.latest = result
            self.data_ready.set()

        self.step_idx += 1
        return result

    def run_loop(self):
        self.running = True
        self.finished = False
        while not self._stop_event.is_set():
            # Loop forever: when data exhausted, auto-reset and continue
            if self.step_idx >= self.num_samples:
                self._reset_ukf()
                continue
            self.step()
            time.sleep(self.speed)
        self.running = False
        self.data_ready.set()

    def start(self):
        if self.running:
            return
        self._stop_event.clear()
        t = threading.Thread(target=self.run_loop, daemon=True)
        t.start()

    def pause(self):
        self._stop_event.set()
        self.running = False

    def reset(self):
        self._stop_event.set()
        self.running = False
        self._reset_ukf()


engine = UKFEngine()


# ==================== Flask 路由 ====================

@app.route('/')
def index():
    return render_template('dashboard.html')


@app.route('/stream')
def stream():
    def generate():
        last_step = 0
        while True:
            engine.data_ready.wait(timeout=2.0)
            engine.data_ready.clear()

            with engine.lock:
                if engine.latest is not None:
                    current_step = engine.latest['step']
                    if current_step > last_step or engine.finished:
                        last_step = current_step
                        payload = {
                            'latest': engine.latest,
                            'history': engine.history,
                            'params': engine.params_info,
                            'nodes': {
                                '1': {'bytes': engine.node_bytes[0], 'status': 'online'},
                                '2': {'bytes': engine.node_bytes[1], 'status': 'online'},
                                '3': {'bytes': engine.node_bytes[2], 'status': 'online'},
                            },
                            'running': engine.running,
                            'finished': engine.finished,
                        }
                        yield f"data: {json.dumps(payload)}\n\n"

            if engine.finished and last_step >= engine.num_samples:
                break

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
        return jsonify({
            'running': engine.running,
            'finished': engine.finished,
            'step': engine.step_idx,
            'total': engine.num_samples,
            'speed': engine.speed,
            'params': engine.params_info,
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


if __name__ == '__main__':
    print("=" * 50)
    print("UKF 状态估计 Dashboard 服务端")
    print("=" * 50)
    print("\n正在初始化 UKF 引擎...")
    engine.init()
    print(f"  - 发电机数: {engine.params_info['n']}, 节点数: {engine.params_info['s']}")
    print(f"  - 采样频率: {engine.params_info['fs']} Hz")
    print(f"  - 数据点数: {engine.params_info['total']} (正常{engine.params_info['num_normal']} + 故障{engine.params_info['num_fault']})")
    print(f"  - 故障时段: {engine.params_info['t_SW_ms']}ms ~ {engine.params_info['t_FC_ms']}ms")
    print(f"  - 节点数据量: N1={engine.node_bytes[0]}B, N2={engine.node_bytes[1]}B, N3={engine.node_bytes[2]}B")
    print(f"\n  Dashboard 地址: http://localhost:5000")
    print("\n按 Ctrl+C 停止服务器\n")
    app.run(host='0.0.0.0', port=5000, threaded=True, debug=False)