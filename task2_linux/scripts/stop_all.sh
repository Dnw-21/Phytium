#!/bin/bash
# 一键停止 task2 所有服务
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Stopping Task2 services..."

# 停止 Dashboard
bash "$SCRIPT_DIR/stop_dashboard.sh"

# 停止 VNC :3
if vncserver -list 2>/dev/null | grep -q ":3"; then
    echo "Killing VNC :3..."
    vncserver -kill :3 2>/dev/null || true
fi

# 清理残留
pkill -f ukf_pipeline 2>/dev/null || true
pkill -f launch_ukf_multi 2>/dev/null || true

echo "All Task2 services stopped"