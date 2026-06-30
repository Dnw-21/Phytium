#!/usr/bin/env python3
"""
自然灾害风险判断引擎 — 以甘肃酒泉地区为场景
针对分布式微电网设备在极端天气下的风险评估

风险评估算法: 加权多灾害综合风险指数 (Weighted Multi-Hazard Risk Index)
  参考国际主流灾害风险评估方法:
  - IPCC AR6 复合灾害风险框架
  - FEMA HAZUS 多灾害风险评估模型
  - 中国气象局《气象灾害风险评估技术规范》

核心算法:
  1. 单灾种评分: 基于阈值触发 + 线性加权
  2. 综合风险指数 (CRI): Σ(weight_i × normalized_score_i) + correlation_boost
  3. 位置脆弱性因子: 基于酒泉戈壁地区特征的环境脆弱性加权

风险类型:
  1. 沙尘暴 — 光伏板积尘、线路闪络
  2. 雷暴 — 直击户外设备、感应过电压
  3. 暴雨/冰雹 — 设备进水、光伏板损坏
  4. 极端温差 — 设备热胀冷缩疲劳
  5. 冻雨/覆冰 — 线路断线、铁塔倒塌
  6. 高温 — 光伏板效率下降、设备过热
"""

import time
import json
import math

class RiskLevel:
    LOW = "low"
    MODERATE = "moderate"
    HIGH = "high"
    EXTREME = "extreme"

RISK_ORDER = [RiskLevel.LOW, RiskLevel.MODERATE, RiskLevel.HIGH, RiskLevel.EXTREME]

RISK_LABELS = {
    RiskLevel.LOW: "低风险",
    RiskLevel.MODERATE: "中风险",
    RiskLevel.HIGH: "高风险",
    RiskLevel.EXTREME: "紧急",
}

RISK_COLORS = {
    RiskLevel.LOW: "#059669",
    RiskLevel.MODERATE: "#d97706",
    RiskLevel.HIGH: "#dc2626",
    RiskLevel.EXTREME: "#7c3aed",
}

class RiskAssessment:
    def __init__(self):
        pass

    def evaluate(self, weather_data):
        if not weather_data or "now" not in weather_data:
            return self._empty_result()

        now = weather_data.get("now", {})
        forecast = weather_data.get("forecast", [])
        location = weather_data.get("location", {})

        temp = self._safe_float(now.get("temperature"))
        feels_like = self._safe_float(now.get("feels_like"))
        humidity = self._safe_float(now.get("humidity"))
        wind_speed = self._safe_float(now.get("wind_speed"))
        wind_scale = self._safe_int(now.get("wind_scale"))
        text = (now.get("text") or "").lower()
        visibility = self._safe_float(now.get("visibility"))
        pressure = self._safe_float(now.get("pressure"))

        risks = {}

        risks["sandstorm"] = self._eval_sandstorm(wind_speed, wind_scale, visibility, text)
        risks["thunderstorm"] = self._eval_thunderstorm(text, humidity)
        risks["rain_hail"] = self._eval_rain_hail(text, forecast)
        risks["extreme_temp"] = self._eval_extreme_temp(temp, forecast)
        risks["icing"] = self._eval_icing(temp, humidity, text)
        risks["heatwave"] = self._eval_heatwave(temp, feels_like)

        overall = self._compute_overall(risks)

        active_risks = [k for k, v in risks.items() if v["level"] != RiskLevel.LOW]

        return {
            "overall": overall,
            "overall_label": RISK_LABELS[overall],
            "overall_color": RISK_COLORS[overall],
            "risks": risks,
            "active_risks": active_risks,
            "evaluated_at": time.time(),
            "location": location.get("name", "酒泉"),
            "summary": self._generate_summary(overall, active_risks, risks),
        }

    def _eval_sandstorm(self, wind_speed, wind_scale, visibility, text):
        score = 0
        details = []

        if "沙尘" in text or "扬沙" in text:
            score += 60
            details.append("当前沙尘天气")
        if "浮尘" in text:
            score += 40
            details.append("浮尘天气")

        if wind_scale is not None and (wind_scale >= 8 or (wind_speed is not None and wind_speed >= 17.2)):
            score += 30
            details.append(f"{wind_scale}级大风携带沙尘")
        elif wind_scale is not None and (wind_scale >= 6 or (wind_speed is not None and wind_speed >= 10.8)):
            score += 15
            details.append(f"{wind_scale}级风可能扬沙")

        if visibility is not None and visibility < 5:
            score += 20
            details.append(f"能见度{visibility}km")
        elif visibility is not None and visibility < 10:
            score += 10
            details.append(f"能见度较低({visibility}km)")

        level, desc = self._score_to_level(score, "沙尘暴", [
            (0, "无沙尘风险"),
            (20, "轻度浮尘，注意光伏板积尘"),
            (50, "沙尘天气，建议加强巡检"),
            (100, "强沙尘暴！建议启动应急预案"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    def _eval_thunderstorm(self, text, humidity):
        score = 0
        details = []

        if "雷" in text:
            score += 60
            details.append("当前雷暴天气")
        if "暴雨" in text or "大雨" in text:
            score += 20
            details.append("强降雨伴随雷电")

        if humidity is not None and humidity > 85:
            score += 15
            details.append(f"湿度{humidity}%利于雷暴形成")
        elif humidity is not None and humidity > 70:
            score += 5
            details.append("湿度较高")

        level, desc = self._score_to_level(score, "雷暴", [
            (0, "无雷暴风险"),
            (25, "湿度较高，注意雷电预警"),
            (50, "雷暴天气，户外设备需做好防雷"),
            (100, "强雷暴！建议断开户外设备"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    def _eval_rain_hail(self, text, forecast):
        score = 0
        details = []

        if "冰雹" in text:
            score += 80
            details.append("冰雹天气！可能损坏光伏板")
        if "暴雨" in text:
            score += 40
            details.append("暴雨可能导致设备进水")
        if "大雨" in text:
            score += 20
            details.append("大雨天气")
        if "雨" in text and "小" not in text and "暴" not in text and "大" not in text:
            score += 10
            details.append("降雨天气")

        for day in forecast[:2]:
            rainfall = self._safe_float(day.get("rainfall"))
            if rainfall is not None and rainfall > 50:
                score += 20
                details.append(f"预报{day.get('date')}降雨{rainfall}mm")

        level, desc = self._score_to_level(score, "暴雨/冰雹", [
            (0, "无暴雨/冰雹风险"),
            (20, "注意天气变化"),
            (50, "暴雨预警，加强设备防水检查"),
            (100, "冰雹/暴雨红色预警！立即防护"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    def _eval_extreme_temp(self, temp, forecast):
        score = 0
        details = []

        if temp is not None:
            all_temps = [temp]
            for day in forecast[:3]:
                high = self._safe_float(day.get("high"))
                low = self._safe_float(day.get("low"))
                if high is not None:
                    all_temps.append(high)
                if low is not None:
                    all_temps.append(low)

            if all_temps:
                max_t = max(all_temps)
                min_t = min(all_temps)
                diff = max_t - min_t
                if diff >= 30:
                    score += 50
                    details.append(f"极端温差{diff:.0f}°C，设备热疲劳风险高")
                elif diff >= 25:
                    score += 30
                    details.append(f"较大温差{diff:.0f}°C")
                elif diff >= 20:
                    score += 15
                    details.append(f"温差{diff:.0f}°C")

        level, desc = self._score_to_level(score, "极端温差", [
            (0, "温差正常"),
            (20, "温差较大，注意设备热胀冷缩"),
            (40, "大幅温差，建议加强巡检"),
            (100, "极端温差！设备疲劳风险高"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    def _eval_icing(self, temp, humidity, text):
        score = 0
        details = []

        if "雪" in text or "冻" in text:
            score += 40
            details.append("降雪/冰冻天气")
        if temp is not None and temp < -10:
            score += 40
            details.append(f"温度{temp}°C，覆冰风险极高")
        elif temp is not None and temp < -5:
            score += 20
            details.append(f"温度{temp}°C，可能覆冰")
        elif temp is not None and temp < 0:
            score += 10
            details.append(f"温度{temp}°C，注意防冻")

        if temp is not None and temp < 0 and humidity is not None and humidity > 80:
            score += 20
            details.append("低温高湿，覆冰条件形成")

        level, desc = self._score_to_level(score, "冻雨/覆冰", [
            (0, "无覆冰风险"),
            (20, "低温天气，注意防冻"),
            (50, "覆冰预警，加强线路巡检"),
            (100, "严重覆冰！线路断线风险高"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    def _eval_heatwave(self, temp, feels_like):
        score = 0
        details = []

        t = feels_like if feels_like is not None else temp
        if t is not None:
            if t >= 40:
                score += 80
                details.append(f"体感温度{t}°C，设备过热风险极高")
            elif t >= 37:
                score += 50
                details.append(f"体感温度{t}°C，光伏板效率下降")
            elif t >= 35:
                score += 30
                details.append(f"体感温度{t}°C，注意设备散热")
            elif t >= 32:
                score += 15
                details.append(f"体感温度{t}°C")

        if temp is not None and temp >= 35:
            score += 20
            details.append("高温红色预警")

        level, desc = self._score_to_level(score, "高温", [
            (0, "温度正常"),
            (20, "天气较热，注意设备散热"),
            (50, "高温天气，光伏板效率降低"),
            (100, "极端高温！设备过热风险高"),
        ])
        return {"level": level, "label": RISK_LABELS[level], "score": score, "description": desc, "details": details}

    # 各灾害类型在酒泉戈壁地区的权重（基于历史发生频率和影响程度）
    # 沙尘暴是酒泉最频发的灾害，权重最高；雷暴次之
    DISASTER_WEIGHTS = {
        "sandstorm": 0.30,
        "thunderstorm": 0.18,
        "rain_hail": 0.15,
        "extreme_temp": 0.14,
        "icing": 0.08,
        "heatwave": 0.15,
    }

    # 灾害相关性矩阵：如果A和B同时活跃，对综合风险产生额外加成
    # (0=独立, 1=完全相关)
    CORRELATION_MATRIX = {
        ("sandstorm", "heatwave"): 0.3,
        ("sandstorm", "thunderstorm"): 0.2,
        ("thunderstorm", "rain_hail"): 0.5,
        ("rain_hail", "icing"): 0.4,
        ("extreme_temp", "heatwave"): 0.4,
        ("thunderstorm", "icing"): 0.2,
        ("sandstorm", "extreme_temp"): 0.25,
    }

    def _compute_overall(self, risks):
        """
        加权多灾害综合风险指数 (Composite Risk Index)
        算法: CRI = Σ(W_i × S_i_norm) + B_corr
        其中 W_i = 灾害权重, S_i_norm = 归一化分数(0~1), B_corr = 相关性加成
        """
        cri_total = 0.0
        active_list = []

        for key, info in risks.items():
            weight = self.DISASTER_WEIGHTS.get(key, 0.1)
            score = info.get("score", 0)
            normalized = min(score / 100.0, 1.0)
            weighted = weight * normalized
            cri_total += weighted
            if info["level"] != RiskLevel.LOW:
                active_list.append(key)

        correlation_boost = 0.0
        for i in range(len(active_list)):
            for j in range(i + 1, len(active_list)):
                pair = (min(active_list[i], active_list[j]),
                        max(active_list[i], active_list[j]))
                corr = self.CORRELATION_MATRIX.get(pair, 0.15)
                si = risks[active_list[i]]["score"] / 100.0
                sj = risks[active_list[j]]["score"] / 100.0
                correlation_boost += corr * si * sj * 0.5

        cri_total = min(cri_total + correlation_boost, 1.0)

        if cri_total >= 0.70:
            return RiskLevel.EXTREME
        elif cri_total >= 0.40:
            return RiskLevel.HIGH
        elif cri_total >= 0.15:
            return RiskLevel.MODERATE
        else:
            return RiskLevel.LOW

    def _generate_summary(self, overall, active_risks, risks):
        summaries = {
            RiskLevel.LOW: "气象条件良好，微电网系统运行正常，无需特别防护。",
            RiskLevel.MODERATE: "存在轻度天气风险，建议加强设备巡检，关注气象预警信息。",
            RiskLevel.HIGH: "恶劣天气来袭！建议启动应急巡检，做好设备防护准备。",
            RiskLevel.EXTREME: "极端天气红色预警！强烈建议启动应急预案，必要时切断户外设备。",
        }
        base = summaries.get(overall, "")
        if active_risks:
            details = "；".join([risks[r]["description"] for r in active_risks])
            base += f"（{details}）"
        return base

    def _score_to_level(self, score, name, thresholds):
        for threshold, desc in thresholds:
            if score >= threshold:
                level = self._threshold_to_level(threshold, thresholds)
                return level, desc
        return RiskLevel.LOW, thresholds[0][1] if thresholds else "正常"

    def _threshold_to_level(self, threshold, thresholds):
        idx = 0
        for i, (t, _) in enumerate(thresholds):
            if threshold == t:
                idx = i
                break
        mapping = [RiskLevel.LOW, RiskLevel.LOW, RiskLevel.MODERATE, RiskLevel.HIGH, RiskLevel.EXTREME]
        if idx < len(mapping):
            return mapping[idx]
        return RiskLevel.EXTREME

    def _safe_float(self, v):
        if v is None:
            return None
        try:
            return float(v)
        except (ValueError, TypeError):
            return None

    def _safe_int(self, v):
        if v is None:
            return None
        try:
            return int(v)
        except (ValueError, TypeError):
            return None

    def _empty_result(self):
        return {
            "overall": RiskLevel.LOW,
            "overall_label": "低风险",
            "overall_color": RISK_COLORS[RiskLevel.LOW],
            "risks": {},
            "active_risks": [],
            "evaluated_at": time.time(),
            "location": "酒泉",
            "summary": "暂无天气数据",
        }


if __name__ == "__main__":
    ra = RiskAssessment()
    test_weather = {
        "now": {
            "temperature": "28",
            "feels_like": "30",
            "humidity": "25",
            "wind_direction": "东风",
            "wind_speed": "12.5",
            "wind_scale": "6",
            "text": "晴",
            "visibility": "15",
        },
        "forecast": [
            {"date": "2026-05-27", "high": "32", "low": "8", "text_day": "晴", "text_night": "晴",
             "wind_direction": "东风", "wind_speed": "10", "wind_scale": "5", "rainfall": "0", "humidity": "20"},
        ],
    }
    result = ra.evaluate(test_weather)
    print(json.dumps(result, ensure_ascii=False, indent=2))