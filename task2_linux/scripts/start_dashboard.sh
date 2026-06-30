#!/bin/bash
# 启动 task2 多节点 UKF Dashboard HTTP 服务
# 端口: 5001 (dashboard_board 占用 5000)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TASK2_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$TASK2_DIR/logs"

mkdir -p "$LOG_DIR"

echo "Starting Task2 UKF Dashboard..."
nohup python3 "$TASK2_DIR/dashboard_server_v2.py" --port 5001 \
    >> "$LOG_DIR/dashboard.log" 2>&1 &
echo $! > "$TASK2_DIR/dashboard.pid"

sleep 2
if kill -0 $(cat "$TASK2_DIR/dashboard.pid") 2>/dev/null; then
    echo "Task2 Dashboard started (PID: $(cat "$TASK2_DIR/dashboard.pid"))"
    echo "  浏览器: http://192.168.88.10:5001"
    echo "  VNC:    vncviewer 192.168.88.10:5903"
else
    echo "ERROR: Failed to start Task2 Dashboard"
    exit 1
fi