#!/usr/bin/env python3
"""
Server酱微信推送模块
每日限制5次，与飞书推送同步
"""

import requests
import time
import threading
from datetime import datetime, timezone, timedelta

SENDKEY = "SCT349501T29odKj78y05tBloqdBhfoxSv"
SERVER_URL = f"https://sctapi.ftqq.com/{SENDKEY}.send"

DAILY_LIMIT = 5

_sent_count = 0
_sent_date = None
_lock = threading.Lock()


def _can_send():
    global _sent_count, _sent_date
    today = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d")
    with _lock:
        if _sent_date != today:
            _sent_count = 0
            _sent_date = today
        if _sent_count >= DAILY_LIMIT:
            return False
        _sent_count += 1
        return True


def _send(title, content):
    try:
        resp = requests.post(
            SERVER_URL,
            data={"title": title, "desp": content.encode("utf-8")},
            timeout=8,
        )
        result = resp.json()
        if result.get("code") == 0:
            print(f"[WeChat] 推送成功: {title}")
            return True
        else:
            print(f"[WeChat] 推送失败: {result}")
            return False
    except Exception as e:
        print(f"[WeChat] 推送异常: {e}")
        return False


def send_fault_alert(fault_info):
    if not _can_send():
        print("[WeChat] 今日推送次数已用完，跳过故障告警")
        return False

    bus = fault_info.get("bus", "--")
    phase = fault_info.get("phase", "三相短路")
    t = fault_info.get("time", 0)
    severity = fault_info.get("severity", "critical")

    pre = fault_info.get("pre_fault", {})
    pre_delta = pre.get("delta", [])
    pre_omega = pre.get("omega", [])

    post = fault_info.get("post_fault", {})
    post_delta = post.get("delta", [])
    post_omega = post.get("omega", [])

    title = f"⚡ 微电网故障告警 - 母线{bus} {phase}"

    lines = [
        f"## ⚡ 微电网故障告警",
        f"",
        f"**故障位置**: 母线 {bus} · {phase}",
        f"**故障时刻**: 仿真 T = {t:.3f}s",
        f"**严重程度**: 🔴 CRITICAL",
    ]

    if pre_delta and pre_omega:
        lines.append(f"")
        lines.append(f"**🟢 故障前状态**:")
        lines.append(f"δ₁={pre_delta[0]:.3f}  δ₂={pre_delta[1]:.3f}  δ₃={pre_delta[2]:.3f}")
        lines.append(f"ω₁={pre_omega[0]:.3f}  ω₂={pre_omega[1]:.3f}  ω₃={pre_omega[2]:.3f}")

    if post_delta and post_omega:
        lines.append(f"")
        lines.append(f"**🔴 故障后状态（异常）**:")
        lines.append(f"δ₁={post_delta[0]:.3f}  δ₂={post_delta[1]:.3f}  δ₃={post_delta[2]:.3f}")
        lines.append(f"ω₁={post_omega[0]:.3f}  ω₂={post_omega[1]:.3f}  ω₃={post_omega[2]:.3f}")

    lines.append(f"")
    lines.append(f"⚠️ 请立即检查微电网运行状态，采取保护措施！")

    content = "\n".join(lines)
    return _send(title, content)


def send_weather_risk_alert(weather, risk):
    if not _can_send():
        print("[WeChat] 今日推送次数已用完，跳过天气预警")
        return False

    now = weather.get("now", {})
    location = weather.get("location", {}).get("name", "甘肃酒泉")
    overall_label = risk.get("overall_label", "--")
    summary = risk.get("summary", "")

    title = f"🌪 微电网天气风险预警 - {location}"
    content = (
        f"## 🌪 天气风险预警\n\n"
        f"**位置**: {location}\n"
        f"**天气**: {now.get('text', '--')} {now.get('temperature', '--')}°C\n"
        f"**风速**: {now.get('wind_speed', '--')} m/s {now.get('wind_direction', '--')}\n"
        f"**能见度**: {now.get('visibility', '--')} km\n"
        f"**湿度**: {now.get('humidity', '--')}%\n\n"
        f"**风险等级**: {overall_label}\n"
        f"**评估**: {summary}\n\n"
        f"⚠️ 请密切关注天气变化，做好防灾准备！"
    )
    return _send(title, content)


def send_system_status(status_info):
    if not _can_send():
        print("[WeChat] 今日推送次数已用完，跳过系统状态")
        return False

    title = f"📊 微电网系统状态汇报"
    content = f"## 📊 系统状态\n\n{status_info}"
    return _send(title, content)