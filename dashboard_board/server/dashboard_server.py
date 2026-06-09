#!/usr/bin/env python3
"""
飞腾派 Dashboard 服务端 (精简版)
- 读取预计算 dashboard_data.json（由 VM 端 prepare_data.py 生成）
- 不依赖 scipy / numpy
- 节点心跳 + 本地循环日志 + 飞书/微信故障推送
- VNC info 端点
"""
import os
import sys
import json
import time
import threading
from collections import deque
from datetime import datetime, timezone, timedelta
from flask import Flask, render_template, jsonify, request

# ── 配置路径 ──────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)
CONFIG_PATH = os.path.join(ROOT_DIR, 'config.json')
DATA_PATH = os.path.join(ROOT_DIR, 'data', 'dashboard_data.json')
TEMPLATE_DIR = os.path.join(ROOT_DIR, 'templates')
STATIC_DIR = os.path.join(ROOT_DIR, 'static')

# ── 加载配置 ──────────────────────────────────────────────
def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)

def save_config(cfg):
    with open(CONFIG_PATH, 'w') as f:
        json.dump(cfg, f, indent=2)

config = load_config()

# ── 心跳源 ────────────────────────────────────────────────
sys.path.insert(0, BASE_DIR)
from heartbeat_source import make_heartbeat_source
heartbeat_source = make_heartbeat_source(config)

# ── 通知模块（飞书 + 微信）──────────────────────────────────
try:
    from feishu_notifier import send_fault_alert as feishu_fault, send_weather_risk_alert as feishu_weather
    print("[feishu] 通知模块加载成功")
except ImportError as e:
    print(f"[feishu] 加载失败: {e}")
    feishu_fault = lambda *a, **kw: False
    feishu_weather = lambda *a, **kw: False
try:
    from wechat_notifier import send_fault_alert as wechat_fault, send_weather_risk_alert as wechat_weather
    print("[wechat] 通知模块加载成功")
except ImportError as e:
    print(f"[wechat] 加载失败: {e}")
    wechat_fault = lambda *a, **kw: False
    wechat_weather = lambda *a, **kw: False

# ── Flask 应用 ────────────────────────────────────────────
app = Flask(__name__, template_folder=TEMPLATE_DIR, static_folder=STATIC_DIR)

# ── 日志缓冲区 ────────────────────────────────────────────
log_buffer = deque(maxlen=config.get('log_buffer_size', 200))

def add_log(level, node_id, msg):
    entry = {
        'ts': datetime.now(timezone(timedelta(hours=8))).strftime('%H:%M:%S'),
        'level': level,
        'node_id': node_id,
        'msg': msg,
    }
    log_buffer.append(entry)
    print(f"[{entry['ts']}] [{level.upper()}] [{node_id}] {msg}")

def notify_fault(node_id, phase, time_sec, delta_est=None, omega_est=None):
    """飞书/微信故障推送（异步线程，不阻塞主循环）
    支持多节点同时触发（node_id 可以是逗号分隔的列表如 "node_1,node_2,node_3"）
    """
    if not config['notify'].get('feishu_enabled', True):
        return
    # 生成节点可读标签
    node_labels = {'node_1': '终端节点 1', 'node_2': '终端节点 2', 'node_3': '终端节点 3'}
    # 支持多节点
    if ',' in node_id:
        ids = node_id.split(',')
        node_label = ', '.join(node_labels.get(n, n) for n in ids)
    else:
        node_label = node_labels.get(node_id, node_id)
    def _do():
        fault_info = {
            'bus': f'{node_label} (ID: {node_id})',
            'phase': phase,
            'time': time_sec,
            'severity': 'critical',
        }
        # 附加 UKF 估计值（仅单节点时有）
        if delta_est and omega_est:
            fault_info['post_fault'] = {
                'delta': delta_est,
                'omega': omega_est,
            }
        add_log('error', 'system', f'[{node_label}] 故障告警: {node_id} @ {time_sec}s, phase={phase}')
        try:
            result = feishu_fault(fault_info, bypass_rate_limit=False)
            add_log('info', 'system', f'[{node_label}] 飞书推送: {"成功" if result else "已限流"}' )
        except Exception as e:
            add_log('warn', node_id, f'飞书故障推送失败: {e}')
        try:
            wechat_fault(fault_info)
        except Exception as e:
            add_log('warn', node_id, f'微信故障推送失败: {e}')
    threading.Thread(target=_do, daemon=True).start()

def notify_disaster(disaster_name, risk_level):
    """灾害仿真推送"""
    if not config['notify'].get('feishu_enabled', True):
        return
    weather = engine._build_weather_risk() if engine else {}
    risk = {
        'overall': risk_level,
        'overall_label': {'low': '低风险', 'medium': '中风险', 'high': '高风险', 'critical': '极高风险'}.get(risk_level, '未知'),
        'overall_color': {'low': '#059669', 'medium': '#f59e0b', 'high': '#d97706', 'critical': '#dc2626'}.get(risk_level, '#6b7280'),
        'summary': f'灾害仿真: {disaster_name}',
    }
    add_log('warning', 'system', f'灾害仿真: {disaster_name}, 风险等级={risk_level}')
    try:
        result = feishu_weather(weather, risk, bypass_rate_limit=True)
        add_log('info', 'system', f'飞书灾害推送: {"成功" if result else "已限流"}' )
    except Exception as e:
        add_log('warn', 'system', f'飞书灾害推送失败: {e}')
    try:
        wechat_weather(weather, risk)
    except Exception as e:
        add_log('warn', 'system', f'微信灾害推送失败: {e}')

# ── 数据引擎 ──────────────────────────────────────────────
class DashboardEngine:
    def __init__(self, data_path, config):
        self.lock = threading.Lock()
        self.config = config
        self.running = False
        self.paused = False
        self.step_idx = 0
        self._stop_event = threading.Event()
        self._thread = None
        self.refresh_interval = config.get('refresh_interval_ms', 1000) / 1000.0

        # 加载数据
        with open(data_path) as f:
            raw = json.load(f)
        self.params = raw['system_params']
        self.steps = raw['steps']  # list of frame dicts
        self.total_steps = len(self.steps)
        self.fault_snapshots = raw.get('fault_snapshots', [])

        # 检测是否三节点新格式
        self._is_3node = (self.total_steps > 0 and 'nodes' in self.steps[0])
        self._node_ids = raw.get('node_ids', ['node_1']) if self._is_3node else ['node_1']

        # 防止空数据
        if self.total_steps == 0:
            self.steps = [{
                'time_sec': 0, 'step': 0, 'rmse': 0.0,
                'fault_phase': 'normal', 'is_fault': False,
                'delta_est': [0,0,0], 'omega_est': [0,0,0],
            }]
            self.total_steps = 1

        self.latest = None
        self.history = {}
        self.fault_detected = []
        self.start_beijing_ms = None
        self.finished = False

        add_log('info', 'system', f'数据加载完成: {self.total_steps} 步, {self.params["total_time"]:.1f}s')

    def _get_enabled_nodes(self):
        return [n for n in self.config['nodes'] if n.get('enabled', False)]

    def _build_weather_risk(self):
        """构造 weather_risk 字段。优先尝试心知天气 API（如果有 key 且网络可达），否则用仿真天气数据。"""
        city = config.get('weather', {}).get('city', 'jiuquan')
        key = config.get('weather', {}).get('key', '')
        timeout = config.get('weather', {}).get('timeout_sec', 5)
        location_name = '甘肃酒泉'
        if city.lower() in ('jiuquan', '酒泉'):
            location_name = '甘肃酒泉'
        risk = {
            'now': {'text': '晴', 'temperature': '25'},
            'location': {'name': location_name},
            'overall': 'low',
            'overall_label': '低风险',
            'overall_color': '#059669',
            'summary': '系统正常',
        }
        # 1) 有心知 API key + 网络可达 → 拉真数据
        if key:
            try:
                import urllib.request, ssl
                url = f"https://api.seniverse.com/v3/weather/now.json?key={key}&location={city}&language=zh-Hans"
                ctx = ssl.create_default_context()
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
                resp = urllib.request.urlopen(url, timeout=timeout, context=ctx)
                raw = json.loads(resp.read().decode('utf-8'))
                now = raw.get('results', [{}])[0].get('now', {})
                if now:
                    risk['now']['text'] = now.get('text', '晴')
                    risk['now']['temperature'] = now.get('temperature', '25')
                loc0 = raw.get('results', [{}])[0].get('location', {})
                if loc0.get('name'):
                    risk['location']['name'] = loc0['name']
                risk['summary'] = f"{risk['now']['text']} {risk['now']['temperature']}°C"
            except Exception as e:
                add_log('warning', 'system', f'心知天气获取失败: {e}')
                risk['summary'] = '心知 API 不可达（已使用仿真数据）'
        else:
            # 2) 没 key → 使用天气仿真数据（基于酒泉地区典型气候模拟）
            import random, hashlib
            # 基于日期生成伪随机种子，让同一天的天气保持一致
            today = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d')
            seed = int(hashlib.md5(today.encode()).hexdigest()[:8], 16)
            rng = random.Random(seed)
            # 酒泉天气仿真：根据季节生成温度范围
            month = datetime.now(timezone(timedelta(hours=8))).month
            if month in (12, 1, 2):
                temp_base = -5; temp_range = 12  # 冬季: -5 ~ 7°C
                conditions = ['晴', '晴', '多云', '阴', '小雪']
            elif month in (3, 4, 5):
                temp_base = 8; temp_range = 18   # 春季: 8 ~ 26°C
                conditions = ['晴', '晴', '多云', '扬沙', '浮尘']
            elif month in (6, 7, 8):
                temp_base = 18; temp_range = 17  # 夏季: 18 ~ 35°C
                conditions = ['晴', '晴', '多云', '雷阵雨', '晴']
            else:
                temp_base = 5; temp_range = 16   # 秋季: 5 ~ 21°C
                conditions = ['晴', '晴', '多云', '阴', '晴']
            sim_temp = temp_base + rng.randint(0, temp_range)
            sim_cond = rng.choice(conditions)
            risk['now']['text'] = sim_cond
            risk['now']['temperature'] = str(sim_temp)
            risk['now']['feels_like'] = str(sim_temp - rng.randint(1, 4))
            risk['now']['humidity'] = str(rng.randint(15, 65))
            risk['now']['wind_direction'] = rng.choice(['西北风', '北风', '东风', '西风'])
            risk['now']['wind_speed'] = str(round(rng.uniform(2.0, 15.0), 1))
            risk['now']['wind_scale'] = str(rng.randint(1, 6))
            risk['now']['visibility'] = str(round(rng.uniform(5.0, 30.0), 1))
            risk['now']['code'] = '0'
            # 风险评级
            if sim_cond in ('扬沙', '浮尘', '沙尘暴', '雷阵雨'):
                risk['overall'] = 'high'
                risk['overall_label'] = '高风险'
                risk['overall_color'] = '#d97706'
            elif sim_cond in ('小雪', '阴'):
                risk['overall'] = 'moderate'
                risk['overall_label'] = '中风险'
                risk['overall_color'] = '#f59e0b'
            else:
                risk['overall'] = 'low'
                risk['overall_label'] = '低风险'
                risk['overall_color'] = '#059669'
            risk['summary'] = f'{sim_cond} {sim_temp}°C · 酒泉地区仿真'
        return risk

    def _make_node_frame(self, node_cfg, step_data):
        heartbeat_ok = heartbeat_source.is_alive(node_cfg['id'])
        status = 'ONLINE'
        if not heartbeat_ok:
            status = 'HIDDEN'
        elif step_data.get('fault_phase') in ('pre', 'fault'):
            status = 'FAULT'

        # 真实 bytes: 每条测量 24 字段 × 8 bytes = 192 bytes/step
        real_step_bytes = 24 * 8
        total_bytes = step_data['step'] * real_step_bytes

        # 电池电量固定
        battery = {'node_1': 98, 'node_2': 99, 'node_3': 99}

        frame = {
            'node_id': node_cfg['id'],
            'label': node_cfg['label'],
            'timestamp_ms': int(time.time() * 1000),
            'status': status,
            'last_heartbeat_ms': heartbeat_source.last_seen_ms(node_cfg['id']),
            'bytes_total': total_bytes,
            'battery_level': battery.get(node_cfg['id'], 98),
            'ukf': {
                'delta_est': step_data.get('delta_est', [0,0,0]),
                'omega_est': step_data.get('omega_est', [0,0,0]),
                'delta_true': step_data.get('delta_true', [0,0,0]),
                'omega_true': step_data.get('omega_true', [0,0,0]),
                'rmse': step_data.get('rmse', 0.0),
            },
            'fault': {
                'is_fault': step_data.get('is_fault', False),
                'phase': step_data.get('fault_phase', 'normal'),
            },
        }
        return frame

    def step(self):
        if self.step_idx >= self.total_steps:
            return None

        sd = self.steps[self.step_idx]
        # 时间 = step 编号 (1点=1秒)，不按原始采样率
        time_sec = float(sd['step'])

        # 首次记录起始北京时间
        if self.start_beijing_ms is None:
            utc = datetime.now(timezone.utc)
            bj = utc.astimezone(timezone(timedelta(hours=8)))
            self.start_beijing_ms = int(bj.timestamp() * 1000)

        beijing_ms = self.start_beijing_ms + int(time_sec * 1000)
        bj_time = (datetime.fromtimestamp(beijing_ms / 1000, tz=timezone(timedelta(hours=8)))
                   .strftime('%Y-%m-%d %H:%M:%S'))

        # 构建节点帧
        enabled = self._get_enabled_nodes()
        node_frames = []
        for nc in enabled:
            frame = self._make_node_frame(nc, sd)
            if frame['status'] != 'HIDDEN':
                node_frames.append(frame)

        # 兼容旧版 frontend 的扁平字段
        if self._is_3node:
            # 三节点新格式：从第一个节点取兼容字段
            nd0 = sd['nodes'].get('node_1', {})
            _de = nd0.get('delta_est', [0,0,0])
            _oe = nd0.get('omega_est', [0,0,0])
            _rmse = nd0.get('rmse', 0.0)
            _is_fault = nd0.get('is_fault', False)
            _phase = nd0.get('fault_phase', 'normal')
            _delta_true = sd.get('delta_true', [0,0,0])
            _omega_true = sd.get('omega_true', [0,0,0])
        else:
            # 旧格式
            _de = sd['delta_est']
            _oe = sd['omega_est']
            _rmse = sd.get('rmse', 0.0)
            _is_fault = sd.get('is_fault', False)
            _phase = sd.get('fault_phase', 'normal')
            _delta_true = sd.get('delta_true', [0,0,0])
            _omega_true = sd.get('omega_true', [0,0,0])

        compat = {
            'running': self.running,
            'elapsed_sec': time_sec,
            'time_sec': time_sec,
            'step': sd['step'],
            'total': self.params['num_samples'],
            'is_fault': _is_fault,
            'fault_phase': {'normal': 0, 'pre': 0, 'fault': 1, 'post': 2}.get(_phase, 0),
            'rmse': _rmse,
            'delta_est': _de,
            'omega_est': _oe,
            'delta_true': _delta_true,
            'omega_true': _omega_true,
            'phase': _phase,
            'params': self.params,
            'nodes': {
                '1': {'bytes_total': sd['step'] * 192, 'battery_level': 98},
                '2': {'bytes_total': sd['step'] * 192, 'battery_level': 99},
                '3': {'bytes_total': sd['step'] * 192, 'battery_level': 99},
            },
            'weather_risk': self._build_weather_risk(),
            'start_beijing_ms': self.start_beijing_ms,
        }
        result = {**compat, **{
            'nodes': node_frames,
            'finished': self.finished,
            'ts': round(time_sec, 3),
            'beijing_time': bj_time,
            'refresh_interval_ms': self.config.get('refresh_interval_ms', 1000),
            '_is_3node': self._is_3node,
            '_node_ids': self._node_ids,
        }}

        # 更新历史 + 故障检测（都在 lock 内，确保 get_fault_replays 能看到一致状态）
        with self.lock:
            if self._is_3node:
                # 三节点模式：每个节点独立存储历史
                for nid in self._node_ids:
                    nd = sd['nodes'].get(nid, {})
                    self.history.setdefault(nid, {
                        'time': [], 'beijing_time_ms': [],
                        'delta_est': [[],[],[]], 'omega_est': [[],[],[]],
                        'delta_true': [[],[],[]], 'omega_true': [[],[],[]],
                        'fault_flags': [], 'phase': [],
                    })
                    h = self.history[nid]
                    t_ms = round(time_sec * 1000, 3)
                    bj_ms = t_ms + (self.start_beijing_ms or 0)
                    h['time'].append(t_ms)
                    h['beijing_time_ms'].append(bj_ms)
                    h['fault_flags'].append(nd.get('is_fault', False))
                    h['phase'].append(nd.get('fault_phase', 'normal'))
                    de = nd.get('delta_est', [0,0,0])
                    oe = nd.get('omega_est', [0,0,0])
                    for i in range(3):
                        h['delta_est'][i].append(float(de[i]))
                        h['omega_est'][i].append(float(oe[i]))
            else:
                # 旧格式：按配置节点存储（共用同一份数据）
                for nc in enabled:
                    hid = nc['id']
                    self.history.setdefault(hid, {
                        'time': [], 'beijing_time_ms': [],
                        'delta_est': [[],[],[]], 'omega_est': [[],[],[]],
                        'delta_true': [[],[],[]], 'omega_true': [[],[],[]],
                        'fault_flags': [], 'phase': [],
                    })
                    h = self.history[hid]
                    t_ms = round(time_sec * 1000, 3)
                    bj_ms = t_ms + (self.start_beijing_ms or 0)
                    h['time'].append(t_ms)
                    h['beijing_time_ms'].append(bj_ms)
                    h['fault_flags'].append(sd.get('is_fault', False))
                    h['phase'].append(sd.get('fault_phase', 'normal'))
                    for i in range(3):
                        h['delta_est'][i].append(float(sd['delta_est'][i]))
                        h['omega_est'][i].append(float(sd['omega_est'][i]))
                        if 'delta_true' in sd:
                            h['delta_true'][i].append(float(sd['delta_true'][i]))
                            h['omega_true'][i].append(float(sd['omega_true'][i]))

            # 故障检测：三节点模式下每个节点独立检测故障
            if self._is_3node:
                entering = []  # 本轮进入故障的节点
                for nid in self._node_ids:
                    nd = sd['nodes'].get(nid, {})
                    node_fault = nd.get('is_fault', False)
                    node_phase = nd.get('fault_phase', 'normal')
                    prev_state = getattr(self, '_prev_fault_' + nid, False)
                    if node_fault and not prev_state:
                        entering.append(nid)
                        add_log('error', nid, f'[{nid}] 故障开始: {time_sec}s')
                    elif not node_fault and prev_state:
                        add_log('info', nid, f'[{nid}] 故障结束: {time_sec}s')
                    setattr(self, '_prev_fault_' + nid, node_fault)
                # 多个节点同时故障 → 合并推送
                if entering:
                    if len(entering) >= 2:
                        notify_fault(','.join(entering), 'fault', time_sec)
                    else:
                        nd = sd['nodes'].get(entering[0], {})
                        notify_fault(entering[0], nd.get('fault_phase', 'normal'), time_sec,
                                     nd.get('delta_est', []), nd.get('omega_est', []))
            else:
                phase = sd.get('fault_phase', 'normal')
                is_real_fault = (phase == 'fault')
                if is_real_fault and (not self.fault_detected or self.fault_detected[-1].get('end') is not None):
                    record = {'start': round(time_sec, 3), 'end': None, 'phase': phase}
                    self.fault_detected.append(record)
                    add_log('error', enabled[0]['id'] if enabled else 'system',
                            f'故障开始: {time_sec}s, phase={phase}')
                    notify_fault(enabled[0]['id'] if enabled else 'system', phase, time_sec)
                elif not is_real_fault and self.fault_detected and self.fault_detected[-1].get('end') is None:
                    self.fault_detected[-1]['end'] = round(time_sec, 3)
                    add_log('info', enabled[0]['id'] if enabled else 'system',
                            f'故障结束: {time_sec}s')

            self.latest = result
            self.step_idx += 1

        return result

    def run_loop(self):
        self.running = True
        self._stop_event.clear()
        while not self._stop_event.is_set():
            if self.paused:
                time.sleep(0.1)
                continue
            if self.step_idx >= self.total_steps:
                self.finished = True
                self._reset()
                continue
            self.step()
            time.sleep(self.refresh_interval)
        self.running = False

    def _reset(self):
        with self.lock:
            self.step_idx = 0
            self.start_beijing_ms = None
            self.latest = None
            self.history = {}
            self.fault_detected = []
            self.finished = False
        # 清空日志缓冲区（全局变量，不在 lock 范围内）
        log_buffer.clear()
        add_log('info', 'system', '系统已重置')

    def start(self):
        if self.running:
            self.paused = False
            return {'status': 'resumed'}
        self._reset()
        self._stop_event.clear()
        self._thread = threading.Thread(target=self.run_loop, daemon=True)
        self._thread.start()
        add_log('info', 'system', '引擎已启动')
        return {'status': 'started'}

    def pause(self):
        self.paused = True
        return {'status': 'paused'}

    def resume(self):
        self.paused = False
        return {'status': 'resumed'}

    def stop(self):
        self._stop_event.set()
        self.running = False
        self.paused = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        return {'status': 'stopped'}

    def get_status(self):
        with self.lock:
            if self.latest:
                return self.latest
            enabled = self._get_enabled_nodes()
            empty_frame = {
                'step': 0, 'time_sec': 0, 'rmse': 0.0,
                'fault_phase': 'normal', 'is_fault': False,
                'delta_est': [0,0,0], 'omega_est': [0,0,0],
            }
            node_frames = []
            for nc in enabled:
                f = self._make_node_frame(nc, empty_frame)
                if f['status'] != 'HIDDEN':
                    node_frames.append(f)
            compat = {
                'running': False, 'finished': False,
                'elapsed_sec': 0, 'time_sec': 0,
                'step': 0, 'total': self.params['num_samples'],
                'is_fault': False, 'fault_phase': 0, 'rmse': 0.0,
                'delta_est': [0,0,0], 'omega_est': [0,0,0],
                'delta_true': [0,0,0], 'omega_true': [0,0,0],
                'phase': 'normal',
                'params': self.params,
                'nodes': {'1': {'bytes_total': 0}, '2': {'bytes_total': 0}, '3': {'bytes_total': 0}},
                'weather_risk': self._build_weather_risk(),
                'start_beijing_ms': None,
            }
            return {**compat, **{
                'nodes': node_frames,
                'ts': 0,
                'finished': False,
                'beijing_time': datetime.now(timezone(timedelta(hours=8))).strftime('%Y-%m-%d %H:%M:%S'),
                'refresh_interval_ms': self.config.get('refresh_interval_ms', 1000),
            }}

    def get_history(self, node_id=None):
        with self.lock:
            if node_id:
                return self.history.get(node_id, {})
            return dict(self.history)

    def get_fault_info(self):
        with self.lock:
            return {
                'fault_detected': self.fault_detected,
                'params': self.params,
            }

    def get_fault_replays(self):
        with self.lock:
            # 只返回已经"经过"的故障快照（按 fault_detected 顺序逐个揭示）
            if not self.fault_detected:
                return {'snapshots': []}
            current_time = self.steps[min(self.step_idx, self.total_steps - 1)]['time_sec']
            enriched = []
            # 按快照中心时间排序，确保 fault1@5s 先于 fault2@15s 揭示
            sorted_snaps = sorted(self.fault_snapshots, key=lambda x: x.get('center_sec', 0))
            for s in sorted_snaps:
                center = s.get('center_sec', 0)
                # 只有当运行时间已超过故障中心时间（±0.5s 容忍），才返回该快照
                if current_time < center - 0.5:
                    continue
                snap = dict(s)
                # 1kHz 全分辨率估计数据（用于曲线展示 + 数据表格）
                # 注：故障窗口是 5.0-5.3s，全分辨率 1kHz 数据有 ~300 点（前后 1s 窗口是 2000 点）
                snap['delta_est_raw'] = snap.get('delta_est', [])
                snap['omega_est_raw'] = snap.get('omega_est', [])
                snap['times_raw'] = snap.get('times', [])
                # 1Hz 降采样（用于曲线快速浏览）
                def _downsample(arr_list):
                    if not arr_list:
                        return arr_list
                    result = []
                    for sub in arr_list:
                        if sub:
                            result.append(sub[0::100])
                        else:
                            result.append([])
                    return result
                snap['delta_est'] = _downsample(snap.get('delta_est', []))
                snap['omega_est'] = _downsample(snap.get('omega_est', []))
                snap['times'] = snap.get('times', [])[0::100] if 'times' in snap else []
                # 移除 true 数据
                snap.pop('delta_true', None)
                snap.pop('omega_true', None)
                # 时间范围显示
                if 'times_raw' in snap and len(snap['times_raw']) >= 2:
                    snap['time_range'] = f"{snap['times_raw'][0]:.3f}s ~ {snap['times_raw'][-1]:.3f}s"
                elif 'center_sec' in snap:
                    snap['time_range'] = f"{snap['center_sec']:.2f}s ±1s"
                else:
                    snap['time_range'] = '--'
                enriched.append(snap)
            return {'snapshots': enriched}

    def get_events(self):
        return {'events': list(log_buffer)[-50:]}


# ── 全局引擎 ──────────────────────────────────────────────
engine = DashboardEngine(DATA_PATH, config)


# ── API 路由 ──────────────────────────────────────────────
@app.route('/')
def index():
    return render_template('dashboard.html')


@app.route('/api/status')
def api_status():
    return jsonify(engine.get_status())


@app.route('/api/history')
def api_history():
    node_id = request.args.get('node_id', 'node_1')
    return jsonify(engine.get_history(node_id))


@app.route('/api/history_all')
def api_history_all():
    """返回所有节点的历史数据（三节点模式）"""
    return jsonify(engine.get_history(None))


@app.route('/api/config')
def api_config():
    return jsonify(config)


@app.route('/api/fault_info')
def api_fault_info():
    return jsonify(engine.get_fault_info())


@app.route('/api/fault_replays')
def api_fault_replays():
    return jsonify(engine.get_fault_replays())


@app.route('/api/events')
def api_events():
    return jsonify(engine.get_events())


@app.route('/api/stream')
def api_stream():
    """SSE 推送：每个 step 推一帧 status，让前端 EventSource 持续订阅"""
    from flask import Response, stream_with_context
    import json as _json

    def gen():
        last_ts = -1.0
        # 初始先发一帧（无论 running 与否），让前端不再显示"连接失败"
        s = engine.get_status()
        yield 'data: ' + _json.dumps(s, ensure_ascii=False) + '\n\n'
        last_ts = s.get('ts', 0)
        # 阻塞循环：每 200ms 检查一次 latest 是否有新数据
        import time as _t
        while True:
            _t.sleep(0.2)
            cur = engine.get_status()
            cur_ts = cur.get('ts', 0)
            if cur_ts != last_ts:
                yield 'data: ' + _json.dumps(cur, ensure_ascii=False) + '\n\n'
                last_ts = cur_ts
    return Response(stream_with_context(gen()), mimetype='text/event-stream',
                    headers={'Cache-Control': 'no-cache', 'X-Accel-Buffering': 'no'})


@app.route('/api/weather')
def api_weather():
    try:
        import urllib.request
        city = config['weather'].get('city', 'jiuquan')
        timeout = config['weather'].get('timeout_sec', 5)
        # 心知天气 API（如果有 key 则返回真实数据）
        key = config['weather'].get('key', '')
        if key:
            url = f"https://api.seniverse.com/v3/weather/now.json?key={key}&location={city}&language=zh-Hans"
            req = urllib.request.Request(url)
            resp = urllib.request.urlopen(req, timeout=timeout)
            data = json.loads(resp.read())
            return jsonify({'status': 'ok', 'data': data})
        else:
            return jsonify({'status': 'unavailable', 'reason': 'no_api_key'})
    except Exception as e:
        return jsonify({'status': 'unavailable', 'reason': str(e)})


@app.route('/api/control', methods=['POST'])
def api_control():
    data = request.get_json() or {}
    action = data.get('action', '')
    if action == 'start':
        engine.start()
        return jsonify({'status': 'ok'})
    elif action == 'pause':
        engine.pause()
        return jsonify({'status': 'ok'})
    elif action == 'resume':
        engine.resume()
        return jsonify({'status': 'ok'})
    elif action == 'stop':
        engine.stop()
        return jsonify({'status': 'ok'})
    elif action == 'reset':
        engine.stop()
        engine._reset()
        return jsonify({'status': 'ok'})
    elif action == 'speed':
        return jsonify({'status': 'ok'})
    return jsonify({'status': 'error', 'message': f'unknown action: {action}'}), 400


# 兼容旧版 API（统一返回 status=ok，让老前端 doStart/doPause 的 r.status === 'ok' 校验通过）
@app.route('/control/start', methods=['POST'])
def legacy_start():
    engine.start()
    return jsonify({'status': 'ok'})


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


@app.route('/api/refresh_interval', methods=['POST'])
def api_refresh_interval():
    global config
    data = request.get_json() or {}
    new_ms = data.get('interval_ms', 1000)
    config['refresh_interval_ms'] = new_ms
    save_config(config)
    engine.refresh_interval = new_ms / 1000.0
    engine.config = config
    add_log('info', 'system', f'刷新频率改为 {new_ms}ms')
    return jsonify({'ok': True, 'new_value_ms': new_ms})


@app.route('/api/vnc_info')
def api_vnc_info():
    return jsonify({
        'host': '192.168.88.10',
        'port': config['vnc']['port'],
        'display': config['vnc']['display'],
        'geometry': config['vnc']['geometry'],
    })


# ── 自然灾害仿真 ──────────────────────────────────────────
DISASTER_PRESETS = {
    'sandstorm': {
        'name': '沙尘暴',
        'weather': {
            'now': {
                'temperature': '18', 'feels_like': '16', 'humidity': '15',
                'wind_direction': '西北风', 'wind_speed': '22.5', 'wind_scale': '9',
                'text': '沙尘暴', 'code': '31', 'visibility': '1.2',
            },
            'location': {'name': '酒泉'},
        },
        'description': '强沙尘暴天气，能见度极低，光伏板积尘风险极高',
        'risk_level': 'high',
    },
    'thunderstorm': {
        'name': '雷暴',
        'weather': {
            'now': {
                'temperature': '22', 'feels_like': '20', 'humidity': '92',
                'wind_direction': '南风', 'wind_speed': '18.5', 'wind_scale': '8',
                'text': '雷阵雨', 'code': '11', 'visibility': '3.5',
            },
            'location': {'name': '酒泉'},
        },
        'description': '强雷暴天气，户外设备需做好防雷保护',
        'risk_level': 'high',
    },
    'rain_hail': {
        'name': '暴雨冰雹',
        'weather': {
            'now': {
                'temperature': '12', 'feels_like': '8', 'humidity': '95',
                'wind_direction': '北风', 'wind_speed': '16.5', 'wind_scale': '7',
                'text': '冰雹', 'code': '19', 'visibility': '2.0',
            },
            'location': {'name': '酒泉'},
        },
        'description': '暴雨伴随冰雹，光伏板及户外设备可能受损',
        'risk_level': 'critical',
    },
    'extreme_temp': {
        'name': '极端温差',
        'weather': {
            'now': {
                'temperature': '35', 'feels_like': '38', 'humidity': '18',
                'wind_direction': '东风', 'wind_speed': '8.5', 'wind_scale': '5',
                'text': '晴', 'code': '0', 'visibility': '20',
            },
            'location': {'name': '酒泉'},
        },
        'description': '昼夜极端温差36°C，设备热胀冷缩疲劳风险',
        'risk_level': 'medium',
    },
    'icing': {
        'name': '冻雨覆冰',
        'weather': {
            'now': {
                'temperature': '-8', 'feels_like': '-14', 'humidity': '88',
                'wind_direction': '北风', 'wind_speed': '14.5', 'wind_scale': '7',
                'text': '冻雨', 'code': '26', 'visibility': '2.5',
            },
            'location': {'name': '酒泉'},
        },
        'description': '冻雨覆冰灾害，线路断线风险，铁塔倒塌风险',
        'risk_level': 'critical',
    },
    'heatwave': {
        'name': '极端高温',
        'weather': {
            'now': {
                'temperature': '42', 'feels_like': '48', 'humidity': '10',
                'wind_direction': '西南风', 'wind_speed': '5.5', 'wind_scale': '3',
                'text': '晴', 'code': '0', 'visibility': '30',
            },
            'location': {'name': '酒泉'},
        },
        'description': '极端高温42°C，光伏板效率骤降，设备过热',
        'risk_level': 'high',
    },
}


@app.route('/api/simulate_disaster', methods=['POST'])
def api_simulate_disaster():
    data = request.get_json() or {}
    disaster_key = data.get('disaster', '')

    if disaster_key == 'clear':
        add_log('info', 'system', '清除灾害仿真')
        return jsonify({'status': 'ok', 'message': '已清除模拟'})

    preset = DISASTER_PRESETS.get(disaster_key)
    if not preset:
        return jsonify({'status': 'error', 'message': f'未知灾害: {disaster_key}'}), 400

    add_log('warn', 'system', f'灾害仿真触发: {preset["name"]}')
    notify_disaster(preset['name'], preset['risk_level'])

    # 构造与前端 simDisaster() 期望一致的响应格式
    risk_label_map = {'low': '低风险', 'medium': '中风险', 'high': '高风险', 'critical': '极高风险'}
    risk_color_map = {'low': '#059669', 'medium': '#f59e0b', 'high': '#d97706', 'critical': '#dc2626'}
    rl = preset['risk_level']

    return jsonify({
        'status': 'ok',
        'disaster': preset['name'],
        'description': preset['description'],
        'risk_level': preset['risk_level'],
        'weather': preset['weather'],
        # 兼容前端期望的 risk 对象
        'risk': {
            'overall': rl,
            'overall_label': risk_label_map.get(rl, '未知'),
            'overall_color': risk_color_map.get(rl, '#6b7280'),
            'summary': preset['description'],
        },
        'feishu_sent': config['notify'].get('feishu_enabled', False),
    })


# ── 主入口 ────────────────────────────────────────────────
if __name__ == '__main__':
    host = config['server']['host']
    port = config['server']['port']
    print("=" * 50)
    print("UKF 状态估计 Dashboard (飞腾派版)")
    print(f"数据步数: {engine.total_steps}")
    print(f"节点数: {len(engine._get_enabled_nodes())}")
    print(f"刷新频率: {config['refresh_interval_ms']}ms")
    print(f"心跳源: {config['heartbeat_source']}")
    print(f"VNC: {config['vnc']['host'] if 'host' in config['vnc'] else '192.168.88.10'}:{config['vnc']['port']}")
    print(f"Dashboard: http://{host}:{port}")
    print("=" * 50)
    add_log('info', 'system', f'服务启动 @ {host}:{port}')
    app.run(host=host, port=port, threaded=True)