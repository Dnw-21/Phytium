#!/usr/bin/env python3
"""
辅助决策建议模块 — 结合天气风险 + UKF状态估计结果
为分布式微电网运维提供具体操作建议
"""

import time

class DecisionSupport:
    def __init__(self):
        self.history = []

    def generate_advice(self, risk_result, ukf_status=None):
        """
        生成辅助决策建议
        risk_result: risk_assessment 的输出
        ukf_status: UKF引擎的最新状态 (delta/omega等)
        """
        overall = risk_result.get("overall", "low")
        active_risks = risk_result.get("active_risks", [])
        risks = risk_result.get("risks", {})

        advices = []
        priority = "normal"

        if overall == "extreme":
            priority = "immediate"
            advices.append({
                "type": "emergency",
                "icon": "🚨",
                "title": "极端天气红色预警",
                "content": "立即启动应急预案！评估是否需要切断户外设备电源。",
                "actions": ["通知运维人员", "检查设备接地", "准备应急电源"],
            })

            if "thunderstorm" in active_risks:
                advices.append({
                    "type": "warning",
                    "icon": "⚡",
                    "title": "雷暴防护建议",
                    "content": "强雷暴可能直击户外设备，建议断开非必要户外设备连接。",
                    "actions": ["断开户外天线/传感器连接", "检查防雷器状态", "避免户外作业"],
                })

        elif overall == "high":
            priority = "urgent"
            advices.append({
                "type": "warning",
                "icon": "⚠️",
                "title": "恶劣天气预警",
                "content": risk_result.get("summary", "恶劣天气即将影响微电网运行。"),
                "actions": ["启动应急巡检流程", "检查关键设备状态", "准备应急物资"],
            })

            if "sandstorm" in active_risks:
                sand = risks.get("sandstorm", {})
                advices.append({
                    "type": "maintenance",
                    "icon": "🌪️",
                    "title": "沙尘暴防护建议",
                    "content": sand.get("description", "沙尘暴可能影响光伏发电效率及线路绝缘。"),
                    "actions": ["检查光伏板积尘情况", "加强线路绝缘检测", "准备清洁设备"],
                })

            if "icing" in active_risks:
                advices.append({
                    "type": "maintenance",
                    "icon": "❄️",
                    "title": "覆冰防护建议",
                    "content": "低温覆冰可能导致线路断线，需加强巡检。",
                    "actions": ["检查线路覆冰情况", "准备除冰设备", "监测线路张力"],
                })

        elif overall == "moderate":
            priority = "attention"
            advices.append({
                "type": "info",
                "icon": "📋",
                "title": "关注天气变化",
                "content": risk_result.get("summary", "气象条件基本正常，但需关注后续天气变化。"),
                "actions": ["关注气象预警", "做好日常巡检"],
            })

            if "heatwave" in active_risks:
                heat = risks.get("heatwave", {})
                advices.append({
                    "type": "info",
                    "icon": "🌡️",
                    "title": "高温运行建议",
                    "content": heat.get("description", "高温天气影响光伏板效率及设备散热。"),
                    "actions": ["检查散热系统", "监测设备温度", "优化负载调度"],
                })

            if "sandstorm" in active_risks:
                advices.append({
                    "type": "info",
                    "icon": "💨",
                    "title": "浮尘/扬沙提醒",
                    "content": "轻度浮尘天气，注意光伏板积尘影响。",
                    "actions": ["安排光伏板清洁", "检查空气过滤系统"],
                })

        else:
            priority = "normal"
            advices.append({
                "type": "info",
                "icon": "✅",
                "title": "系统运行正常",
                "content": "气象条件良好，微电网系统正常运行。",
                "actions": ["保持常规巡检", "做好日常运维记录"],
            })

        if ukf_status:
            advices = self._merge_ukf_advice(advices, ukf_status, overall)

        result = {
            "priority": priority,
            "advices": advices,
            "generated_at": time.time(),
            "count": len(advices),
        }

        self.history.append({
            "time": time.time(),
            "priority": priority,
            "summary": advices[0]["title"] if advices else "无建议",
        })
        if len(self.history) > 100:
            self.history = self.history[-100:]

        return result

    def _merge_ukf_advice(self, advices, ukf_status, risk_level):
        if not ukf_status:
            return advices

        phase = ukf_status.get("phase", "normal")
        omega_est = ukf_status.get("omega_est", [])
        delta_est = ukf_status.get("delta_est", [])

        if phase and "fault" in phase:
            advices.append({
                "type": "warning",
                "icon": "⚡",
                "title": "电网故障检测",
                "content": f"UKF检测到{phase}，结合天气风险评估，建议启动故障响应。",
                "actions": ["确认故障位置", "启动备用电源", "通知调度中心"],
            })

        for i, w in enumerate(omega_est):
            if abs(w) > 0.1:
                advices.append({
                    "type": "info",
                    "icon": "🔄",
                    "title": f"发电机G{i+1}功率摆荡",
                    "content": f"G{i+1}转速偏差{w:.3f}rad/s，天气风险可能加剧不稳定。",
                    "actions": ["监测功角变化", "评估系统阻尼", "必要时降负荷运行"],
                })

        return advices

    def get_history(self):
        return self.history[-50:]


if __name__ == "__main__":
    ds = DecisionSupport()
    test_risk = {
        "overall": "high",
        "active_risks": ["sandstorm", "thunderstorm"],
        "risks": {
            "sandstorm": {"level": "high", "description": "强沙尘暴，注意光伏板积尘"},
            "thunderstorm": {"level": "high", "description": "雷暴天气"},
        },
        "summary": "强沙尘暴+雷暴同时来袭！",
    }
    test_ukf = {
        "phase": "normal",
        "omega_est": [0.02, -0.15, 0.03],
        "delta_est": [0.5, 0.3, 0.8],
    }
    result = ds.generate_advice(test_risk, test_ukf)
    import json
    print(json.dumps(result, ensure_ascii=False, indent=2))