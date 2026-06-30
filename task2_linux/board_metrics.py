#!/usr/bin/env python3
"""Phytium 开发板实时指标采集 — 通过 SSH 获取"""
import json, time, subprocess, sys

BOARD = "192.168.88.10"
SSH = ["sshpass", "-p", "user", "ssh", "-o", "StrictHostKeyChecking=no",
       "-o", "ConnectTimeout=3", f"user@{BOARD}"]

def get_board_metrics():
    """SSH 到 Phytium 板获取 CPU/内存/FreeRTOS 状态"""
    try:
        # 一条 SSH 命令获取所有指标, 避免多次连接
        cmd = """
printf "user\\n" | sudo -S busybox devmem 0xC8000008 32 2>/dev/null | tail -1
echo "---HB_END---"
printf "user\\n" | sudo -S busybox devmem 0xC8000000 32 2>/dev/null | tail -1
echo "---WI_END---"
cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null
echo "---STATE_END---"
grep "^cpu[0-9]" /proc/stat
echo "---CPU_END---"
grep -E "MemTotal|MemAvailable" /proc/meminfo
echo "---MEM_END---"
cat /proc/uptime
echo "---UP_END---"
"""
        r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=8)
        out = r.stdout

        # 解析
        result = {"frtos": {}, "cpu": {"cores": {}, "total": 0}, "mem": {}, "uptime": 0}

        # Heartbeat
        if "---HB_END---" in out:
            result["frtos"]["hb"] = out.split("---HB_END---")[0].strip().split("\n")[-1]

        # Write index
        if "---WI_END---" in out:
            result["frtos"]["wi"] = out.split("---WI_END---")[0].split("---HB_END---")[1].strip()

        # remoteproc state
        if "---STATE_END---" in out:
            result["frtos"]["state"] = out.split("---STATE_END---")[0].split("---WI_END---")[1].strip()

        # CPU
        if "---CPU_END---" in out:
            cpu_lines = out.split("---CPU_END---")[0].split("---STATE_END---")[1].strip().split("\n")
            # First pass: store current values
            # We need prev values for delta, so we cache globally
            for line in cpu_lines:
                if not line.startswith("cpu"): continue
                parts = line.split()
                core = parts[0]
                vals = sum(int(x) for x in parts[1:8])
                result["cpu"]["cores"][core] = vals  # store raw for delta calc later

        # Memory
        if "---MEM_END---" in out:
            mem_lines = out.split("---MEM_END---")[0].split("---CPU_END---")[1].strip().split("\n")
            total = avail = 0
            for line in mem_lines:
                if "MemTotal" in line: total = int(line.split()[1])
                if "MemAvailable" in line: avail = int(line.split()[1])
            result["mem"] = {"total_mb": total//1024, "used_mb": (total-avail)//1024,
                           "pct": round(100*(total-avail)/max(total,1), 1)}

        # Uptime
        if "---UP_END---" in out:
            up = out.split("---UP_END---")[0].split("---MEM_END---")[1].strip().split()[0]
            result["uptime"] = int(float(up))

        return result
    except Exception as e:
        return {"error": str(e), "frtos": {}, "cpu": {"cores": {}, "total": 0}, "mem": {}}


# CPU delta tracking
_prev_cpu = {}

def get_cpu_pct(current_raw):
    """计算 CPU 使用率 (需要前后两次采样)"""
    global _prev_cpu
    cores_pct = {}
    total_pct = 0
    n = 0
    for core, total in current_raw.items():
        if core in _prev_cpu:
            d = total - _prev_cpu[core]
            # Approximate: assume idle fraction from previous sample
            # More accurate would need idle breakdown, but raw total gives relative load
            if d > 0:
                # Use system-wide idle ratio from /proc/stat cpu line
                pct = min(100, max(0, d / 100))  # rough proxy
                cores_pct[core] = round(pct, 1)
                total_pct += pct
                n += 1
        _prev_cpu[core] = total
    return cores_pct, round(total_pct / max(n, 1), 1) if n > 0 else 0


if __name__ == "__main__":
    # Test run
    m = get_board_metrics()
    print(json.dumps(m, indent=2))
