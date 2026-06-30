#!/usr/bin/env python3
"""
天气数据获取模块 — 心知天气 API
获取甘肃酒泉的实时天气、3天预报及生活指数
"""

import requests
import time
import threading
import json
import os

API_KEY = "SCZ1a8ZgyOnrVG8Oy"
LOCATION = "酒泉"
BASE_URL = "https://api.seniverse.com/v3"

CACHE_FILE = os.path.join(os.path.dirname(__file__), "weather_cache.json")

class WeatherService:
    def __init__(self, update_interval=900, risk_engine=None, decision_engine=None):
        self.update_interval = update_interval
        self.lock = threading.Lock()
        self._stop_event = threading.Event()
        self._thread = None

        self.current = None
        self.daily = None
        self.suggestion = None
        self.last_update = 0
        self.error = None

        # 风险评估
        self.risk_engine = risk_engine
        self.decision_engine = decision_engine
        self._last_risk_level = "low"

        self._load_cache()
        self._update()

    def _load_cache(self):
        try:
            if os.path.exists(CACHE_FILE):
                with open(CACHE_FILE, "r") as f:
                    data = json.load(f)
                    self.current = data.get("current")
                    self.daily = data.get("daily")
                    self.suggestion = data.get("suggestion")
                    self.last_update = data.get("last_update", 0)
        except Exception:
            pass

    def _save_cache(self):
        try:
            with open(CACHE_FILE, "w") as f:
                json.dump({
                    "current": self.current,
                    "daily": self.daily,
                    "suggestion": self.suggestion,
                    "last_update": self.last_update,
                }, f)
        except Exception:
            pass

    def _update(self):
        now = time.time()
        if now - self.last_update < self.update_interval:
            return

        try:
            self.current = self._fetch_now()
            self.daily = self._fetch_daily()
            self.suggestion = self._fetch_suggestion()
            self.last_update = now
            self.error = None
            self._save_cache()
            self._check_and_notify()
        except Exception as e:
            self.error = str(e)

    def _check_and_notify(self):
        """检查风险等级变化，触发飞书预警"""
        if not self.risk_engine:
            return
        try:
            from feishu_notifier import send_weather_risk_alert
            weather = self.get_weather()
            if weather.get("error"):
                return
            risk = self.risk_engine.evaluate(weather)
            current_level = risk.get("overall", "low")

            # 风险升级时发送预警
            level_order = {"low": 0, "moderate": 1, "high": 2, "extreme": 3}
            if level_order.get(current_level, 0) > level_order.get(self._last_risk_level, 0):
                advice = None
                if self.decision_engine:
                    advice = self.decision_engine.generate_advice(risk)
                send_weather_risk_alert(weather, risk, advice)

            self._last_risk_level = current_level
        except Exception as e:
            print(f"[WeatherService] 风险检查异常: {e}")

    def _fetch_now(self):
        url = f"{BASE_URL}/weather/now.json"
        params = {"key": API_KEY, "location": LOCATION, "language": "zh-Hans", "unit": "c"}
        resp = requests.get(url, params=params, timeout=10)
        data = resp.json()
        if "results" in data and len(data["results"]) > 0:
            return data["results"][0]
        raise Exception(f"API error: {data}")

    def _fetch_daily(self):
        url = f"{BASE_URL}/weather/daily.json"
        params = {"key": API_KEY, "location": LOCATION, "language": "zh-Hans", "unit": "c", "start": 0, "days": 3}
        resp = requests.get(url, params=params, timeout=10)
        data = resp.json()
        if "results" in data and len(data["results"]) > 0:
            return data["results"][0]
        raise Exception(f"API error: {data}")

    def _fetch_suggestion(self):
        url = f"{BASE_URL}/life/suggestion.json"
        params = {"key": API_KEY, "location": LOCATION, "language": "zh-Hans"}
        resp = requests.get(url, params=params, timeout=10)
        data = resp.json()
        if "results" in data and len(data["results"]) > 0:
            return data["results"][0]
        return None

    def get_weather(self):
        with self.lock:
            # 如果有模拟数据，直接返回模拟数据
            if hasattr(self, '_simulated') and self._simulated:
                return self._simulated

            self._update()
            if self.current is None:
                return {"error": self.error or "暂无天气数据", "location": LOCATION}

            now = self.current.get("now", {})
            daily_list = []
            if self.daily:
                daily_list = self.daily.get("daily", [])

            # 心知天气免费版 now.json 只返回 temperature + text + code
            # 用今天预报数据补充缺失字段
            today = daily_list[0] if daily_list else {}

            def _pick(now_key, daily_key):
                v = now.get(now_key)
                if v is not None and v != "":
                    return v
                return today.get(daily_key) if today else None

            return {
                "location": self.current.get("location", {}),
                "now": {
                    "temperature": now.get("temperature"),
                    "feels_like": _pick("feels_like", "high"),
                    "humidity": _pick("humidity", "humidity"),
                    "wind_direction": _pick("wind_direction", "wind_direction"),
                    "wind_speed": _pick("wind_speed", "wind_speed"),
                    "wind_scale": _pick("wind_scale", "wind_scale"),
                    "text": now.get("text"),
                    "code": now.get("code"),
                    "pressure": _pick("pressure", None),
                    "visibility": _pick("visibility", None),
                },
                "forecast": [
                    {
                        "date": d.get("date"),
                        "high": d.get("high"),
                        "low": d.get("low"),
                        "text_day": d.get("text_day"),
                        "text_night": d.get("text_night"),
                        "wind_direction": d.get("wind_direction"),
                        "wind_speed": d.get("wind_speed"),
                        "wind_scale": d.get("wind_scale"),
                        "rainfall": d.get("rainfall"),
                        "humidity": d.get("humidity"),
                    }
                    for d in daily_list
                ],
                "last_update": self.last_update,
                "error": None,
            }

    def set_simulated_weather(self, simulated_data):
        """
        设置模拟天气数据（用于测试风险预警）
        simulated_data: dict 包含 now 和 forecast 字段
        设置后会覆盖真实API数据，直到调用 clear_simulated_weather()
        """
        with self.lock:
            self._simulated = simulated_data
            self.last_update = time.time()

    def clear_simulated_weather(self):
        """清除模拟数据，恢复真实API数据"""
        with self.lock:
            self._simulated = None

    def start_auto_update(self):
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._auto_update_loop, daemon=True)
        self._thread.start()

    def _auto_update_loop(self):
        while not self._stop_event.is_set():
            self._update()
            self._stop_event.wait(self.update_interval)

    def stop(self):
        self._stop_event.set()


if __name__ == "__main__":
    ws = WeatherService(update_interval=60)
    data = ws.get_weather()
    print(json.dumps(data, ensure_ascii=False, indent=2))