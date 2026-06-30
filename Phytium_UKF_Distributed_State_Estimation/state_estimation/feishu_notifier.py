#!/usr/bin/env python3
"""
飞书自定义机器人消息推送模块
用于微电网自然灾害预警和故障告警推送
"""

import requests
import json
import time
import threading
from datetime import datetime

WEBHOOK_URL = "https://open.feishu.cn/open-apis/bot/v2/hook/19d68380-d876-4806-80be-091c3b23a8ea"

# 限流控制：同一类型消息最小间隔（秒）
RATE_LIMIT = {
    "weather_risk": 300,      # 天气风险预警 5分钟
    "fault": 60,              # 故障告警 1分钟
    "system": 300,            # 系统状态 5分钟
}

_last_sent = {}
_lock = threading.Lock()


def _check_rate_limit(msg_type):
    """检查是否超过发送频率限制"""
    now = time.time()
    with _lock:
        last = _last_sent.get(msg_type, 0)
        interval = RATE_LIMIT.get(msg_type, 60)
        if now - last < interval:
            return False
        _last_sent[msg_type] = now
        return True


def _send_card(card_data):
    """发送飞书卡片消息"""
    payload = {
        "msg_type": "interactive",
        "card": card_data
    }
    try:
        resp = requests.post(WEBHOOK_URL, json=payload, timeout=10)
        result = resp.json()
        if result.get("code") != 0:
            print(f"[Feishu] 发送失败: {result}")
            return False
        return True
    except Exception as e:
        print(f"[Feishu] 发送异常: {e}")
        return False


def send_weather_risk_alert(weather, risk, advice=None, bypass_rate_limit=False):
    """
    发送天气风险预警卡片（增强版：醒目、直观、信息密集）
    """
    if not risk or risk.get("overall") == "low":
        return False

    if not bypass_rate_limit and not _check_rate_limit("weather_risk"):
        return False
    else:
        with _lock:
            _last_sent["weather_risk"] = time.time()

    overall = risk.get("overall", "low")
    level_icons = {
        "low": "🟢",
        "moderate": "🟡",
        "high": "🟠",
        "extreme": "🔴",
    }
    level_colors = {
        "low": "green",
        "moderate": "orange",
        "high": "red",
        "extreme": "carmine",
    }
    title_prefix = {
        "moderate": "⚠️",
        "high": "🚨",
        "extreme": "🔴🔴",
    }
    icon = level_icons.get(overall, "🟢")
    color = level_colors.get(overall, "grey")
    prefix = title_prefix.get(overall, "📢")

    now = weather.get("now", {}) if weather else {}
    loc = weather.get("location", {}) if weather else {}
    city = loc.get("name", "酒泉")
    temp = now.get("temperature", "--")
    text = now.get("text", "--")
    humidity = now.get("humidity", "--")
    wind = now.get("wind_scale", "--")
    wind_dir = now.get("wind_direction", "--")
    visibility = now.get("visibility", "--")

    active_risks = []
    risk_names = {
        "sandstorm": "🌪 沙尘暴",
        "thunderstorm": "⛈ 雷暴",
        "rain_hail": "🌧 暴雨/冰雹",
        "extreme_temp": "🌡 极端温差",
        "icing": "❄ 冻雨/覆冰",
        "heatwave": "🔥 极端高温",
    }
    for key, name in risk_names.items():
        rd = risk.get("risks", {}).get(key)
        if rd and rd.get("level") != "low":
            active_risks.append(f"**{name}** ({rd.get('label','')}): {rd.get('description','')}")

    risk_text = "\n".join(active_risks) if active_risks else "• 暂无具体风险项"

    card = {
        "config": {"wide_screen_mode": True},
        "header": {
            "title": {"tag": "plain_text", "content": f"{prefix} {city}微电网气象灾害预警"},
            "template": color
        },
        "elements": [
            {
                "tag": "div",
                "text": {"tag": "lark_md", "content": f"## {icon} 综合风险等级：{risk.get('overall_label', '--')}"}
            },
            {"tag": "hr"},
            {
                "tag": "div",
                "fields": [
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**🌤 当前天气**\n{text} · {temp}°C"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**💨 风力风向**\n{wind_dir} · {wind}级"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**💧 相对湿度**\n{humidity}%"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**👁 能见度**\n{visibility}km"}},
                ]
            },
            {"tag": "hr"},
            {
                "tag": "div",
                "text": {"tag": "lark_md", "content": f"**{icon} 风险摘要**\n{risk.get('summary', '--')}"}
            },
            {
                "tag": "div",
                "text": {"tag": "lark_md", "content": f"**⚠️ 灾害详情**\n{risk_text}"}
            },
        ]
    }

    if advice:
        card["elements"].append({"tag": "hr"})
        card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": f"**💡 应对建议**\n{str(advice)[:500]}"}})

    card["elements"].append({"tag": "note", "elements": [{"tag": "plain_text", "content": f"📍 甘肃酒泉微电网灾害预警 · {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"}]})

    return _send_card(card)


def send_fault_alert(fault_info, ukf_status=None, bypass_rate_limit=False):
    """
    发送故障告警卡片（高亮醒目版）
    """
    if not bypass_rate_limit and not _check_rate_limit("fault"):
        return False
    else:
        with _lock:
            _last_sent["fault"] = time.time()

    severity = fault_info.get("severity", "warning")
    color_map = {"critical": "red", "warning": "orange", "info": "blue"}
    color = color_map.get(severity, "red")

    bus = fault_info.get("bus", "--")
    phase = fault_info.get("phase", "三相短路")
    t = fault_info.get("time", 0)

    pre = fault_info.get("pre_fault", {})
    pre_delta = pre.get("delta", [])
    pre_omega = pre.get("omega", [])
    pre_text = ""
    if pre_delta and pre_omega:
        pre_text = f"δ₁={pre_delta[0]:.3f}  δ₂={pre_delta[1]:.3f}  δ₃={pre_delta[2]:.3f}\nω₁={pre_omega[0]:.3f}  ω₂={pre_omega[1]:.3f}  ω₃={pre_omega[2]:.3f}"

    post = fault_info.get("post_fault", {})
    post_delta = post.get("delta", [])
    post_omega = post.get("omega", [])
    post_text = ""
    if post_delta and post_omega:
        post_text = f"δ₁={post_delta[0]:.3f}  δ₂={post_delta[1]:.3f}  δ₃={post_delta[2]:.3f}\nω₁={post_omega[0]:.3f}  ω₂={post_omega[1]:.3f}  ω₃={post_omega[2]:.3f}"
    elif ukf_status:
        delta = ukf_status.get("delta", [])
        omega = ukf_status.get("omega", [])
        if delta and omega:
            post_text = f"δ₁={delta[0]:.3f}  δ₂={delta[1]:.3f}  δ₃={delta[2]:.3f}\nω₁={omega[0]:.3f}  ω₂={omega[1]:.3f}  ω₃={omega[2]:.3f}"

    card = {
        "config": {"wide_screen_mode": True},
        "header": {
            "title": {"tag": "plain_text", "content": "⚡⚡ 微电网故障告警 ⚡⚡"},
            "template": color
        },
        "elements": [
            {"tag": "div", "text": {"tag": "lark_md", "content": f"## 🔴 检测到系统故障！\n**母线 {bus} · {phase}**"}},
            {"tag": "hr"},
            {
                "tag": "div",
                "fields": [
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**🔧 故障位置**\n母线 {bus}"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**⚡ 故障类型**\n{phase}"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**⏱ 故障时刻**\n仿真 T = {t:.3f}s"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**🔴 严重程度**\n**{severity.upper()}**"}},
                ]
            },
        ]
    }

    if pre_text:
        card["elements"].append({"tag": "hr"})
        card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": f"**🟢 故障前状态**\n```\n{pre_text}\n```"}})

    if post_text:
        card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": f"**🔴 故障后状态（异常）**\n```\n{post_text}\n```"}})

    card["elements"].append({"tag": "hr"})
    card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": f"⚠️ **请立即检查微电网运行状态，采取保护措施！**"}})

    card["elements"].append({"tag": "note", "elements": [{"tag": "plain_text", "content": f"📍 甘肃酒泉微电网故障监控 · {datetime.now().strftime('%m-%d %H:%M:%S')}"}]})

    return _send_card(card)


def send_system_status(status, weather=None, risk=None):
    """
    发送系统状态摘要（定时或手动触发）
    """
    if not _check_rate_limit("system"):
        return False

    elapsed = status.get("elapsed_sec", 0)
    running = "运行中" if status.get("running") else "已停止"
    fault_detected = status.get("fault_detected", False)

    weather_text = ""
    if weather and not weather.get("error"):
        now = weather.get("now", {})
        weather_text = f"**天气:** {now.get('text', '--')} {now.get('temperature', '--')}°C"

    risk_text = ""
    if risk:
        risk_text = f"**风险:** {risk.get('overall_label', '--')}"

    card = {
        "config": {"wide_screen_mode": True},
        "header": {
            "title": {"tag": "plain_text", "content": "📊 微电网运行状态"},
            "template": "blue"
        },
        "elements": [
            {
                "tag": "div",
                "fields": [
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**仿真状态**\n{running}"}},
                    {"is_short": True, "text": {"tag": "lark_md", "content": f"**运行时长**\n{elapsed:.2f}s"}},
                ]
            },
        ]
    }

    if weather_text or risk_text:
        card["elements"].append({"tag": "hr"})
        if weather_text:
            card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": weather_text}})
        if risk_text:
            card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": risk_text}})

    if fault_detected:
        card["elements"].append({"tag": "hr"})
        card["elements"].append({"tag": "div", "text": {"tag": "lark_md", "content": "⚠️ **故障已检测到，请关注系统状态**"}})

    card["elements"].append({"tag": "note", "elements": [{"tag": "plain_text", "content": f"📍 甘肃酒泉微电网 · {datetime.now().strftime('%m-%d %H:%M')}"}]})

    return _send_card(card)


if __name__ == "__main__":
    # 测试发送
    print("测试发送天气风险预警...")
    test_weather = {
        "location": {"name": "酒泉"},
        "now": {"temperature": "14", "text": "多云", "humidity": "28", "wind_scale": "2"}
    }
    test_risk = {
        "overall": "moderate",
        "overall_label": "中风险",
        "overall_color": "#d97706",
        "summary": "沙尘天气，建议加强巡检",
        "risks": {
            "sandstorm": {"level": "moderate", "label": "中风险", "description": "沙尘天气，建议加强巡检"},
            "thunderstorm": {"level": "low", "label": "低风险", "description": "无雷暴风险"},
            "rain_hail": {"level": "low", "label": "低风险", "description": "无暴雨/冰雹风险"},
            "extreme_temp": {"level": "low", "label": "低风险", "description": "温差正常"},
            "icing": {"level": "low", "label": "低风险", "description": "无覆冰风险"},
            "heatwave": {"level": "low", "label": "低风险", "description": "温度正常"},
        }
    }
    result = send_weather_risk_alert(test_weather, test_risk)
    print(f"发送结果: {result}")
