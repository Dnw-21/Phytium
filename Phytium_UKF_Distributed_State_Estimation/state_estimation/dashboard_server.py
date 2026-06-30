#!/usr/bin/env python3
"""
UKF 状态估计 Dashboard 服务端 (state_new 版本)
- 3分钟数据，双故障点 (5.0-5.3s, 15.0-15.3s)
- 1秒刷新，完全实时仿真
- 北京时间时间戳
- 故障实时检测与标记
- 天气服务 + 自然灾害风险评估 + 仿真模拟
"""

import os
import sys
import time
import json
import threading
import urllib.request
from datetime import datetime, timezone, timedelta
from flask import Flask, render_template, jsonify, request
import numpy as np
from scipy.io import loadmat

sys.path.insert(0, '/home/alientek/Phytium/state_new')
from ukf_estimation import ukf_estimation, get_system_state

# 飞书 webhook 地址
FEISHU_WEBHOOK = "https://open.feishu.cn/open-apis/bot/v2/hook/19d68380-d876-4806-80be-091c3b23a8ea"

# 天气和风险评估
from weather_service import WeatherService
from risk_assessment import RiskAssessment

app = Flask(__name__, template_folder='templates')

# ============ 全局配置 ============
DATA_DIR = '/home/alientek/Phytium/state_new'
REFRESH_INTERVAL = 1.0  # 1秒刷新

# ============ 天气 & 风险服务 ============
risk_engine = RiskAssessment()
weather_service = WeatherService(update_interval=600, risk_engine=risk_engine)

# ============ UKF 引擎 ============
class UKFDashboardEngine:
    def __init__(self):
        self.lock = threading.Lock()
        self.running = False
        self.paused = False
        self.step_idx = 0
        self.num_samples = 0
        self.fs = 1000
        self.total_time = 180  # 3分钟
        self.n = 3
        self.s = 9
        self.ns = 6
        self.nm = 24

        # 故障时间
        self.fault1_start = 5.0
        self.fault1_end = 5.3
        self.fault2_start = 15.0
        self.fault2_end = 15.3

        # 系统参数
        self.sp = None
        self.Z_mes = None
        self.X_true = None
        self.X_est_full = None
        self.RMSE_full = None

        # 运行状态
        self.history = {
            'time': [],
            'delta_est': [[], [], []],
            'delta_true': [[], [], []],
            'omega_est': [[], [], []],
            'omega_true': [[], [], []],
            'fault_flags': [],  # 故障标志位
            'beijing_time': [],
        }
        self.latest = None
        self.fault_detected = []  # 记录检测到的故障
        self.events = []
        self.finished = False
        self.speed_sec = REFRESH_INTERVAL
        self.data_ready = threading.Event()
        self._stop_event = threading.Event()
        self._thread = None

        self._load_data()

    def _load_data(self):
        """加载系统参数和测量数据"""
        os.chdir(DATA_DIR)

        data = loadmat('system_params.mat')

        self.fs = float(data['fs'][0, 0])
        self.total_time = float(data['total_time'][0, 0])
        self.num_samples = int(self.total_time * self.fs)
        self.n = int(data['n'][0, 0])
        self.s = int(data['s'][0, 0])
        self.ns = 2 * self.n
        self.nm = 2 * self.n + 2 * self.s

        if 'fault_times' in data:
            ft = data['fault_times']
            self.fault1_start = float(ft[0, 0])
            self.fault1_end = float(ft[0, 1])
            self.fault2_start = float(ft[1, 0])
            self.fault2_end = float(ft[1, 1])

        sig = float(data['sig'][0, 0])
        P_init = sig**2 * np.eye(self.ns)
        Q_mat = sig**2 * np.eye(self.ns)
        R_meas = sig**2 * np.eye(self.nm)
        W = np.ones((2 * self.ns, 1)) / (2 * self.ns)

        self.sp = {
            'YBUS': data['YBUS'],
            'RV': data['RV'],
            'E_abs': data['E_abs'].flatten(),
            'PM': data['PM'].flatten(),
            'M': data['M'].flatten(),
            'D': data['D'].flatten(),
            'n': self.n,
            's': self.s,
            'ns': self.ns,
            'nm': self.nm,
            'fs': self.fs,
            'deltt': 1.0 / self.fs,
            'num_samples': self.num_samples,
            'fault1_start': self.fault1_start,
            'fault1_end': self.fault1_end,
            'fault2_start': self.fault2_start,
            'fault2_end': self.fault2_end,
            'P': P_init,
            'Q_mat': Q_mat,
            'R_meas': R_meas,
            'W': W,
            'X_hat': data['X_0'].flatten(),
        }

        # 加载测量数据
        self.Z_mes = np.zeros((self.nm, self.num_samples))
        node_assignments = [[0, 3, 4], [1, 5, 6], [2, 7, 8]]

        for node_idx, buses in enumerate(node_assignments):
            filename = f'node{node_idx+1}_measurements.txt'
            gen_i = node_idx

            with open(filename, 'r') as f:
                lines = f.readlines()

            for line_idx, line in enumerate(lines[1:]):
                parts = line.strip().split('\t')
                i = line_idx
                self.Z_mes[gen_i, i] = float(parts[1])
                self.Z_mes[self.n + gen_i, i] = float(parts[2])
                for j, b in enumerate(buses):
                    self.Z_mes[2*self.n + b, i] = float(parts[3 + j])
                    self.Z_mes[2*self.n + self.s + b, i] = float(parts[3 + len(buses) + j])

        # 加载真实状态
        try:
            self.X_true = np.loadtxt('true_states.csv', delimiter=',', skiprows=1).T
        except FileNotFoundError:
            self.X_true = None

        # 生成故障标签数组（每个样本的故障标志）
        self.fault_labels = np.zeros(self.num_samples, dtype=bool)
        for idx in range(self.num_samples):
            k = idx / self.fs
            ps = get_system_state(k, self.fault1_start, self.fault1_end,
                                   self.fault2_start, self.fault2_end)
            self.fault_labels[idx] = (ps == 1)

        # UKF预计算结果（后台线程计算）
        self.X_est_full = None
        self.RMSE_full = None
        self._ukf_ready = threading.Event()
        self._ukf_thread = None

        self._reset()

        # 服务启动时自动开始UKF后台计算
        self._ukf_thread = threading.Thread(target=self._run_ukf, daemon=True)
        self._ukf_thread.start()
        print("[初始化] UKF后台计算已自动启动")

    def _run_ukf(self):
        cache = os.path.join(os.path.dirname(__file__), 'ukf_cache.npz')
        try:
            if os.path.exists(cache):
                data = np.load(cache)
                self.X_est_full = data['X_est_full']
                self.RMSE_full = data['RMSE_full']
                print(f"[后台] 从缓存加载UKF结果: {self.num_samples} 样本")
            else:
                print("[后台] 开始UKF预计算...")
                self.X_est_full, self.RMSE_full = ukf_estimation(self.sp, self.Z_mes)
                np.savez(cache, X_est_full=self.X_est_full, RMSE_full=self.RMSE_full)
                print(f"[后台] UKF预计算完成(已缓存): {self.num_samples} 样本")
        except Exception as e:
            print(f"[后台] UKF预计算失败: {e}")
        finally:
            self._ukf_ready.set()

    def _ensure_ukf_ready(self):
        """确保UKF计算完成"""
        if self.X_est_full is None:
            if self._ukf_thread is None or not self._ukf_thread.is_alive():
                self._ukf_thread = threading.Thread(target=self._run_ukf, daemon=True)
                self._ukf_thread.start()
            print("等待UKF预计算完成...")
            self._ukf_ready.wait()

    def _reset(self):
        """重置到初始状态"""
        with self.lock:
            self.step_idx = 0
            self.start_beijing_ms = None
            self.history = {
                'time': [],
                'beijing_time_ms': [],
                'delta_est': [[], [], []],
                'delta_true': [[], [], []],
                'omega_est': [[], [], []],
                'omega_true': [[], [], []],
                'fault_flags': [],
                'phase': [],
                'beijing_time': [],
            }
            self.latest = None
            self.fault_detected = []
            self.fault_snapshots = []
            self.events = []
            self.finished = False
            self.data_ready.clear()

    def _get_beijing_time(self):
        """获取当前北京时间"""
        utc = datetime.now(timezone.utc)
        beijing = utc.astimezone(timezone(timedelta(hours=8)))
        return beijing.strftime('%Y-%m-%d %H:%M:%S')

    def _send_feishu_alert(self, fault_record, engine_result=None):
        """发送飞书故障告警（增强版卡片）"""
        try:
            from feishu_notifier import send_fault_alert

            pre = self._get_snapshot_state(fault_record['start'] - 1.0)
            post = self._get_snapshot_state(fault_record['start'])

            fault_info = {
                "bus": "8-9",
                "phase": "三相短路",
                "time": fault_record['start'],
                "severity": "critical",
                "pre_fault": pre,
                "post_fault": post,
            }
            send_fault_alert(fault_info, bypass_rate_limit=True)
            try:
                from wechat_notifier import send_fault_alert as wechat_fault_alert
                wechat_fault_alert(fault_info)
            except Exception as we:
                print(f"[WeChat] 故障告警发送失败: {we}")
        except Exception as e:
            print(f"[Feishu] 故障告警发送失败: {e}")
            # 回退到基础消息
            try:
                msg = {
                    "msg_type": "post",
                    "content": {
                        "post": {
                            "zh_cn": {
                                "title": "⚠️ UKF 状态估计 - 故障检测告警",
                                "content": [
                                    [{"tag": "text", "text": f"检测时间: {fault_record['detected_at']}"}],
                                    [{"tag": "text", "text": f"故障开始: {fault_record['start']}s"}],
                                    [{"tag": "text", "text": "系统检测到异常状态，请立即检查！"}]
                                ]
                            }
                        }
                    }
                }
                req = urllib.request.Request(
                    FEISHU_WEBHOOK,
                    data=json.dumps(msg).encode('utf-8'),
                    headers={'Content-Type': 'application/json'},
                    method='POST'
                )
                urllib.request.urlopen(req, timeout=5)
            except Exception:
                pass

    def _capture_fault_snapshot(self, center_sec, current_sec):
        """捕获故障前后±1s的数据快照用于回放"""
        try:
            span = int(1.0 * self.fs)
            c_idx = int(center_sec * self.fs)
            cur_idx = min(int(current_sec * self.fs), self.num_samples - 1)
            start_idx = max(0, c_idx - span)
            end_idx = min(self.num_samples, c_idx + span)

            if self.X_true is not None:
                delta_true = [[float(self.X_true[i][j]) for j in range(start_idx, end_idx)] for i in range(self.n)]
                omega_true = [[float(self.X_true[self.n + i][j]) for j in range(start_idx, end_idx)] for i in range(self.n)]
            else:
                delta_true = [[], [], []]
                omega_true = [[], [], []]

            if self.X_est_full is not None:
                delta_est = [[float(self.X_est_full[i][j]) for j in range(start_idx, end_idx)] for i in range(self.n)]
                omega_est = [[float(self.X_est_full[self.n + i][j]) for j in range(start_idx, end_idx)] for i in range(self.n)]
            else:
                delta_est = [[], [], []]
                omega_est = [[], [], []]

            times = [round(j / self.fs, 4) for j in range(start_idx, end_idx)]

            snapshot = {
                'id': f"故障{len(self.fault_snapshots) + 1} @ {round(center_sec, 3)}s",
                'center_sec': round(center_sec, 3),
                'time_range': f"{times[0]:.3f}s ~ {times[-1]:.3f}s",
                'times': times,
                'delta_true': delta_true,
                'delta_est': delta_est,
                'omega_true': omega_true,
                'omega_est': omega_est,
            }
            self.fault_snapshots.append(snapshot)
        except Exception as e:
            print(f"[Snapshot] 故障快照捕获失败: {e}")

    def _get_snapshot_state(self, time_sec):
        """获取指定仿真时刻的估计状态（用于故障前后对比）"""
        try:
            idx = int(time_sec * self.fs)
            idx = max(0, min(idx, self.num_samples - 1))
            if self.X_est_full is not None and self.X_true is not None:
                return {
                    "delta": [round(float(self.X_true[i][idx]), 5) for i in range(self.n)],
                    "omega": [round(float(self.X_true[self.n + i][idx]), 5) for i in range(self.n)],
                }
        except Exception:
            pass
        return {"delta": [0, 0, 0], "omega": [0, 0, 0]}

    def step(self):
        """执行一步（1秒的数据）"""
        if self.step_idx >= self.num_samples:
            return None

        # 确保UKF计算完成
        self._ensure_ukf_ready()

        # 每次处理1秒的数据 (fs个样本)
        start_idx = self.step_idx
        end_idx = min(start_idx + int(self.fs), self.num_samples)

        # 取该秒起始样本作为显示点，保证t=0就有数据
        display_idx = start_idx
        k = display_idx / self.fs

        # 当前刷新帧内读取到的数据标签
        fault_indices = np.where(self.fault_labels[start_idx:end_idx])[0]
        is_fault = bool(fault_indices.size > 0)
        fault_sample_idx = int(start_idx + fault_indices[0]) if is_fault else None
        fault_sample_time = fault_sample_idx / self.fs if fault_sample_idx is not None else None

        # 显示点的故障相位
        ps = get_system_state(k, self.fault1_start, self.fault1_end,
                               self.fault2_start, self.fault2_end)

        # 获取该秒的数据
        X_est = self.X_est_full[:, display_idx]
        X_true = self.X_true[:, display_idx] if self.X_true is not None else None
        RMSE = float(self.RMSE_full[display_idx])

        # 构建结果
        result = {
            'step': end_idx,
            'total': self.num_samples,
            'time_sec': round(k, 3),
            'is_fault': is_fault,
            'fault_phase': ps,
            'rmse': RMSE,
            'beijing_time': self._get_beijing_time(),
            'delta_est': X_est[:self.n].tolist(),
            'omega_est': X_est[self.n:].tolist(),
        }

        if X_true is not None:
            result['delta_true'] = X_true[:self.n].tolist()
            result['omega_true'] = X_true[self.n:].tolist()

        # 更新历史
        with self.lock:
            # 首次运行时记录起始北京时间(毫秒)
            if self.start_beijing_ms is None:
                utc_now = datetime.now(timezone.utc)
                beijing_now = utc_now.astimezone(timezone(timedelta(hours=8)))
                self.start_beijing_ms = int(beijing_now.timestamp() * 1000)

            beijing_time_ms = self.start_beijing_ms + int(k * 1000)
            phase = 'fault' if is_fault else ('pre' if k < self.fault1_start else 'post')
            self.history.setdefault('phase', []).append(phase)
            self.history['time'].append(round(k * 1000, 3))
            self.history['beijing_time_ms'].append(beijing_time_ms)
            self.history['fault_flags'].append(is_fault)
            self.history['beijing_time'].append(result['beijing_time'])
            for i in range(self.n):
                self.history['delta_est'][i].append(float(X_est[i]))
                self.history['omega_est'][i].append(float(X_est[self.n + i]))
                if X_true is not None:
                    self.history['delta_true'][i].append(float(X_true[i]))
                    self.history['omega_true'][i].append(float(X_true[self.n + i]))

            # 故障检测记录
            if is_fault and (len(self.fault_detected) == 0 or
                             self.fault_detected[-1]['end'] is not None):
                fault_record = {
                    'start': round(fault_sample_time, 3),
                    'end': None,
                    'detected_at': self._get_beijing_time()
                }
                self.fault_detected.append(fault_record)
                self.events.append({'type': 'fault', 'time': fault_record['detected_at'], 'msg': f"读取到故障数据，仿真时间 {fault_record['start']}s"})
                self.events = self.events[-50:]

                self._capture_fault_snapshot(fault_sample_time, end_idx / self.fs)

                self._send_feishu_alert(fault_record)
            elif not is_fault and len(self.fault_detected) > 0 and \
                 self.fault_detected[-1]['end'] is None:
                last_fault_idx = int(start_idx - 1)
                last_fault_time = last_fault_idx / self.fs
                self.fault_detected[-1]['end'] = round(last_fault_time, 3)
                self.events.append({'type': 'info', 'time': self._get_beijing_time(), 'msg': f"故障数据段结束，仿真时间 {round(last_fault_time, 3)}s"})
                self.events = self.events[-50:]

            self.latest = result
            self.data_ready.set()

        self.step_idx = end_idx
        return result

    def run_loop(self):
        """主循环：每秒执行一步"""
        self.running = True
        self._stop_event.clear()

        while not self._stop_event.is_set():
            if self.paused:
                time.sleep(0.1)
                continue

            if self.step_idx >= self.num_samples:
                # 数据结束，循环播放
                self._reset()
                continue

            self.step()
            time.sleep(REFRESH_INTERVAL)

        self.running = False
        self.data_ready.set()

    def start(self):
        """启动引擎"""
        if self.running:
            was_paused = self.paused
            self.paused = False
            return {'status': 'resumed' if was_paused else 'running'}

        # 检查UKF是否已计算完成
        if self.X_est_full is None:
            if self._ukf_thread is None or not self._ukf_thread.is_alive():
                self._ukf_thread = threading.Thread(target=self._run_ukf, daemon=True)
                self._ukf_thread.start()
                print("[启动] UKF后台计算已启动")
            return {'status': 'ukf_computing', 'message': 'UKF计算中，请稍后...'}

        self._reset()
        self._stop_event.clear()

        self._thread = threading.Thread(target=self.run_loop, daemon=True)
        self._thread.start()
        return {'status': 'started'}

    def pause(self):
        """暂停"""
        self.paused = True
        return {'status': 'paused'}

    def resume(self):
        """恢复"""
        self.paused = False
        return {'status': 'resumed'}

    def stop(self):
        """停止"""
        self._stop_event.set()
        self.running = False
        self.paused = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        return {'status': 'stopped'}

    def _params_payload(self):
        return {
            'n': self.n,
            's': self.s,
            'fs': int(self.fs),
            'total': self.num_samples,
            'fault1_start': round(self.fault1_start, 3),
            'fault1_end': round(self.fault1_end, 3),
            'fault2_start': round(self.fault2_start, 3),
            'fault2_end': round(self.fault2_end, 3),
            'refresh_interval': REFRESH_INTERVAL,
        }

    def _nodes_payload(self):
        samples = max(1, self.step_idx)
        per_sample = 16
        base = samples * per_sample

        is_fault = self.latest.get('is_fault', False) if self.latest else False
        multiplier = 2.0 if is_fault else 1.0

        node_bytes = int(base * multiplier)
        return {
            '1': {'bytes_total': node_bytes},
            '2': {'bytes_total': node_bytes},
            '3': {'bytes_total': node_bytes},
        }

    def _phase_from_latest(self):
        if not self.latest:
            return 'ready'
        if self.latest.get('is_fault'):
            return 'fault'
        return 'post' if self.latest.get('time_sec', 0) >= self.fault1_start else 'pre'

    def get_status(self):
        """获取当前状态"""
        with self.lock:
            elapsed = self.latest.get('time_sec', 0) if self.latest else 0

            weather_data = weather_service.get_weather()
            risk = risk_engine.evaluate(weather_data)

            weather_risk = {
                'now': weather_data.get('now', {'text': '--', 'temperature': '--'}),
                'location': weather_data.get('location', {}).get('name', '甘肃酒泉'),
                'overall': risk.get('overall', 'low'),
                'overall_label': risk.get('overall_label', '--'),
                'overall_color': risk.get('overall_color', '#059669'),
                'summary': risk.get('summary', '--'),
            }

            payload = {
                'running': self.running,
                'finished': self.finished,
                'elapsed_sec': elapsed,
                'start_beijing_ms': self.start_beijing_ms,
                'phase': self._phase_from_latest(),
                'params': self._params_payload(),
                'nodes': self._nodes_payload(),
                'weather_risk': weather_risk,
            }
            if self.latest:
                payload.update(self.latest)
            else:
                payload.update({
                    'step': 0,
                    'total': self.num_samples,
                    'time_sec': 0,
                    'is_fault': False,
                    'beijing_time': self._get_beijing_time(),
                    'delta_est': [0, 0, 0],
                    'omega_est': [0, 0, 0],
                })
            return payload

    def get_history(self):
        """获取历史数据"""
        with self.lock:
            return dict(self.history)

    def get_fault_info(self):
        """获取故障检测信息"""
        with self.lock:
            return {
                'fault_detected': self.fault_detected,
                'current_fault': bool(self.latest.get('is_fault', False)) if self.latest else False,
            }

    def get_events(self):
        with self.lock:
            return {'events': list(self.events)}


# ============ 全局引擎实例 ============
engine = UKFDashboardEngine()


# ============ API 路由 ============
@app.route('/')
def index():
    return render_template('dashboard.html')


@app.route('/api/start', methods=['POST'])
def api_start():
    return jsonify(engine.start())


@app.route('/api/pause', methods=['POST'])
def api_pause():
    return jsonify(engine.pause())


@app.route('/api/resume', methods=['POST'])
def api_resume():
    return jsonify(engine.resume())


@app.route('/api/stop', methods=['POST'])
def api_stop():
    return jsonify(engine.stop())


@app.route('/api/status')
def api_status():
    return jsonify(engine.get_status())


@app.route('/api/history')
def api_history():
    return jsonify(engine.get_history())


@app.route('/api/fault_info')
def api_fault_info():
    return jsonify(engine.get_fault_info())


@app.route('/api/fault_replays')
def api_fault_replays():
    with engine.lock:
        return jsonify({'snapshots': list(engine.fault_snapshots)})


@app.route('/api/config')
def api_config():
    return jsonify({
        'fs': engine.fs,
        'total_time': engine.total_time,
        'num_samples': engine.num_samples,
        'n': engine.n,
        'fault1_start': engine.fault1_start,
        'fault1_end': engine.fault1_end,
        'fault2_start': engine.fault2_start,
        'fault2_end': engine.fault2_end,
        'refresh_interval': REFRESH_INTERVAL,
    })


@app.route('/status')
def legacy_status():
    return jsonify(engine.get_status())


@app.route('/history')
def legacy_history():
    return jsonify(engine.get_history())


@app.route('/events')
def legacy_events():
    return jsonify(engine.get_events())


@app.route('/control/start', methods=['POST'])
def legacy_start():
    result = engine.start()
    if result.get('status') in ('started', 'running', 'resumed'):
        return jsonify({'status': 'ok'})
    return jsonify({'status': 'ok', 'ukf_pending': True, 'message': result.get('message', 'UKF计算中，请稍后...')})


@app.route('/control/pause', methods=['POST'])
def legacy_pause():
    engine.pause()
    return jsonify({'status': 'ok'})


@app.route('/control/reset', methods=['POST'])
def legacy_reset():
    engine.stop()
    engine._reset()
    return jsonify({'status': 'ok'})


@app.route('/control/speed', methods=['POST'])
def legacy_speed():
    return jsonify({'status': 'ok'})


# ============ 天气 & 风险 API ============
@app.route('/api/weather')
def api_weather():
    weather_data = weather_service.get_weather()
    risk = risk_engine.evaluate(weather_data)
    return jsonify({
        'weather': weather_data,
        'risk': risk,
    })


# ============ 自然灾害仿真模拟 API ============
DISASTER_PRESETS = {
    'sandstorm': {
        'name': '沙尘暴',
        'weather': {
            'now': {
                'temperature': '18', 'feels_like': '16', 'humidity': '15',
                'wind_direction': '西北风', 'wind_speed': '22.5', 'wind_scale': '9',
                'text': '沙尘暴', 'code': '31', 'visibility': '1.2',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '22', 'low': '8', 'text_day': '沙尘暴', 'text_night': '浮尘',
                 'wind_direction': '西北风', 'wind_speed': '25', 'wind_scale': '9', 'rainfall': '0', 'humidity': '12'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '强沙尘暴天气，能见度极低，光伏板积尘风险极高',
    },
    'thunderstorm': {
        'name': '雷暴',
        'weather': {
            'now': {
                'temperature': '22', 'feels_like': '20', 'humidity': '92',
                'wind_direction': '南风', 'wind_speed': '18.5', 'wind_scale': '8',
                'text': '雷阵雨', 'code': '11', 'visibility': '3.5',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '25', 'low': '14', 'text_day': '雷阵雨', 'text_night': '暴雨',
                 'wind_direction': '南风', 'wind_speed': '20', 'wind_scale': '8', 'rainfall': '45', 'humidity': '90'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '强雷暴天气，户外设备需做好防雷保护',
    },
    'rain_hail': {
        'name': '暴雨冰雹',
        'weather': {
            'now': {
                'temperature': '12', 'feels_like': '8', 'humidity': '95',
                'wind_direction': '北风', 'wind_speed': '16.5', 'wind_scale': '7',
                'text': '冰雹', 'code': '19', 'visibility': '2.0',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '16', 'low': '6', 'text_day': '冰雹', 'text_night': '暴雨',
                 'wind_direction': '北风', 'wind_speed': '18', 'wind_scale': '7', 'rainfall': '65', 'humidity': '95'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '暴雨伴随冰雹，光伏板及户外设备可能受损',
    },
    'extreme_temp': {
        'name': '极端温差',
        'weather': {
            'now': {
                'temperature': '35', 'feels_like': '38', 'humidity': '18',
                'wind_direction': '东风', 'wind_speed': '8.5', 'wind_scale': '5',
                'text': '晴', 'code': '0', 'visibility': '20',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '38', 'low': '2', 'text_day': '晴', 'text_night': '晴',
                 'wind_direction': '东风', 'wind_speed': '10', 'wind_scale': '5', 'rainfall': '0', 'humidity': '15'},
                {'date': '2026-05-28', 'high': '35', 'low': '5', 'text_day': '多云', 'text_night': '晴',
                 'wind_direction': '东风', 'wind_speed': '8', 'wind_scale': '4', 'rainfall': '0', 'humidity': '20'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '昼夜极端温差36°C，设备热胀冷缩疲劳风险',
    },
    'icing': {
        'name': '冻雨覆冰',
        'weather': {
            'now': {
                'temperature': '-8', 'feels_like': '-14', 'humidity': '88',
                'wind_direction': '北风', 'wind_speed': '14.5', 'wind_scale': '7',
                'text': '冻雨', 'code': '26', 'visibility': '2.5',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '-2', 'low': '-12', 'text_day': '冻雨', 'text_night': '大雪',
                 'wind_direction': '北风', 'wind_speed': '15', 'wind_scale': '7', 'rainfall': '0', 'humidity': '90'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '冻雨覆冰灾害，线路断线风险，铁塔倒塌风险',
    },
    'heatwave': {
        'name': '极端高温',
        'weather': {
            'now': {
                'temperature': '42', 'feels_like': '48', 'humidity': '10',
                'wind_direction': '西南风', 'wind_speed': '5.5', 'wind_scale': '3',
                'text': '晴', 'code': '0', 'visibility': '30',
            },
            'forecast': [
                {'date': '2026-05-27', 'high': '44', 'low': '28', 'text_day': '晴', 'text_night': '晴',
                 'wind_direction': '西南风', 'wind_speed': '6', 'wind_scale': '3', 'rainfall': '0', 'humidity': '8'},
            ],
            'location': {'name': '酒泉'},
        },
        'description': '极端高温42°C，体感48°C，光伏板效率骤降，设备过热',
    },
}


@app.route('/api/simulate_disaster', methods=['POST'])
def api_simulate_disaster():
    data = request.get_json()
    disaster_key = data.get('disaster', '')

    if disaster_key == 'clear':
        weather_service.clear_simulated_weather()
        return jsonify({'status': 'ok', 'message': '已清除模拟，恢复真实天气数据'})

    preset = DISASTER_PRESETS.get(disaster_key)
    if not preset:
        return jsonify({'status': 'error', 'message': f'未知灾害类型: {disaster_key}'}), 400

    weather_service.set_simulated_weather(preset['weather'])

    # 评估风险
    risk = risk_engine.evaluate(preset['weather'])

    # 仿真灾害一律推送到飞书（模拟真实告警场景）
    feishu_sent = False
    try:
        from feishu_notifier import send_weather_risk_alert
        feishu_sent = send_weather_risk_alert(preset['weather'], risk, bypass_rate_limit=True)
    except Exception as e:
        print(f"[DisasterSim] 飞书推送失败: {e}")

    try:
        from wechat_notifier import send_weather_risk_alert as wechat_weather_alert
        wechat_weather_alert(preset['weather'], risk)
    except Exception as we:
        print(f"[WeChat] 天气预警发送失败: {we}")

    return jsonify({
        'status': 'ok',
        'disaster': preset['name'],
        'risk': risk,
        'feishu_sent': feishu_sent,
    })


if __name__ == '__main__':
    print("=" * 50)
    print("UKF 状态估计 Dashboard 服务端")
    print("3分钟数据 | 双故障检测 | 1秒刷新")
    print("=" * 50)
    print(f"数据点数: {engine.num_samples}")
    print(f"故障1: {engine.fault1_start}s - {engine.fault1_end}s")
    print(f"故障2: {engine.fault2_start}s - {engine.fault2_end}s")
    print(f"Dashboard: http://localhost:5000")
    print("=" * 50)
    app.run(host='0.0.0.0', port=5000, threaded=True)
