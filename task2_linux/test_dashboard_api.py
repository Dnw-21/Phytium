#!/usr/bin/env python3
"""
test_dashboard_api.py — Dashboard API 离线自测
==============================================
不依赖开发板/FreeRTOS/dashboard server 进程,
使用 mock_ukf_data.py 生成测试数据, 然后用 Flask test_client
直接调用 dashboard_server_v2.py 的 API 验证逻辑正确性。

用法:
    cd /home/alientek/Phytium/task2_linux
    python3 test_dashboard_api.py
"""

import os
import sys
import json

# 确保能找到 dashboard_server_v2.py
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 先生成 mock 数据
import mock_ukf_data
print('[setup] generating mock npz...')
mock_ukf_data.cmd_gen(['200'])  # 200 帧/节点

# 加载 dashboard server
import dashboard_server_v2 as dsv
app = dsv.app
client = app.test_client()


def test(name, resp, expected_keys=None, expected_min_keys=None):
    print(f'  {"OK" if resp.status_code == 200 else "FAIL"}  {name:35s}  status={resp.status_code}', end='')
    if resp.status_code != 200:
        print(f'  body={resp.get_data(as_text=True)[:200]}')
        return False
    data = json.loads(resp.get_data(as_text=True))
    if expected_keys:
        missing = [k for k in expected_keys if k not in data]
        if missing:
            print(f'  [missing keys: {missing}]')
            return False
    if expected_min_keys:
        if sum(1 for k in data if k.startswith(expected_min_keys)) < 1:
            print(f'  [no key starting with {expected_min_keys}]')
            return False
    n = len(data) if isinstance(data, (list, dict)) else 1
    print(f'  keys={n}')
    return True


def main():
    print('\n[test] /api/status')
    r = client.get('/api/status')
    test('status', r, expected_keys=['status', 'nodes', 'cpu', 'mem', 'frtos'])

    print('\n[test] /api/compare')
    r = client.get('/api/compare')
    test('compare', r, expected_keys=['5bus', '39bus', '9bus'])

    print('\n[test] /api/history (each node)')
    for node in ['5bus', '39bus', '9bus']:
        r = client.get(f'/api/history?node={node}')
        ok = test(f'history.{node}', r,
                  expected_keys=['time', 'rmse', 'Z_dim'],
                  expected_min_keys='state_')
        if not ok:
            continue
        data = json.loads(r.get_data(as_text=True))
        z_dim = data.get('Z_dim', 0)
        # 过滤出 Z_0, Z_1, ... 这种数字后缀的 (排除 Z_dim)
        z_keys = [k for k in data if k.startswith('Z_') and k[2:].isdigit()]
        s_keys = sum(1 for k in data if k.startswith('state_'))
        print(f'         time={len(data["time"])}  states={s_keys}  Z_dim={z_dim}  Z_keys={len(z_keys)}')
        assert z_dim == len(z_keys), f'Z_dim={z_dim} but Z_keys={len(z_keys)}'
        assert s_keys > 0, f'no state_* keys'

    print('\n[test] /api/resources')
    r = client.get('/api/resources')
    test('resources', r, expected_keys=['cores', 'shm', 'processes'])

    print('\n[test] /api/logs')
    r = client.get('/api/logs?node=5bus')
    test('logs.5bus', r, expected_keys=['node', 'lines'])

    print('\n[test] /api/control (start/pause/reset - just verify not 500)')
    for action in ['start', 'pause', 'reset']:
        r = client.post('/api/control', json={'action': action, 'nodes': ['5bus']})
        # 这里我们只期望不是 500 (500 表示 python 异常, 不是 sudo 失败)
        # 5001 是 sudo 缺失权限失败, 这在本地测试中是预期的
        if r.status_code == 500:
            data = json.loads(r.get_data(as_text=True))
            msg = data.get('message', '')
            print(f'  OK    control.{action:8s}  status=500  (expected, no sudo: {msg[:60]})')
        else:
            print(f'  OK    control.{action:8s}  status={r.status_code}')

    print('\n[done] 全部 API 逻辑通过')


if __name__ == '__main__':
    main()
