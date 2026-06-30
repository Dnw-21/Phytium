#!/bin/bash
# 停止 task2 多节点 UKF Dashboard
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TASK2_DIR="$(dirname "$SCRIPT_DIR")"

if [ -f "$TASK2_DIR/dashboard.pid" ]; then
    PID=$(cat "$TASK2_DIR/dashboard.pid")
    if kill -0 "$PID" 2>/dev/null; then
        echo "Stopping Task2 Dashboard (PID: $PID)..."
        kill "$PID"
        sleep 1
        if kill -0 "$PID" 2>/dev/null; then
            kill -9 "$PID"
        fi
    fi
    rm -f "$TASK2_DIR/dashboard.pid"
fi

# 杀掉残留的 dashboard_server_v2
pkill -f dashboard_server_v2.py 2>/dev/null || true

echo "Task2 Dashboard stopped"