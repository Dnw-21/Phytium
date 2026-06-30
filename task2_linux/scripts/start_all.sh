#!/bin/bash
# 一键启动 task2 完整流程:
#   1. 启动 Dashboard HTTP 服务 (port 5001)
#   2. 启动 VNC (display :3, port 5903)
#   3. 显示访问方式

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "============================================"
echo "  Task2 多节点 UKF 面板 — 一键启动"
echo "============================================"
echo ""

# 1. 启动 Dashboard
echo "[1/2] 启动 Dashboard HTTP 服务..."
bash "$SCRIPT_DIR/start_dashboard.sh"

# 2. 启动 VNC
echo ""
echo "[2/2] 启动 VNC 桌面..."
bash "$SCRIPT_DIR/start_vnc_task2.sh"

echo ""
echo "============================================"
echo "  Task2 Dashboard 已就绪"
echo "============================================"
echo ""
echo "  访问方式:"
echo "    浏览器: http://192.168.88.10:5001"
echo "    VNC:    vncviewer 192.168.88.10:5903"
echo ""
echo "  启动 UKF Pipeline:"
echo "    sudo python3 /home/alientek/Phytium/task2_linux/launch_ukf_multi.py"
echo ""
echo "  停止:"
echo "    bash $SCRIPT_DIR/stop_all.sh"