#!/bin/bash
# task2 VNC 桌面启动脚本（飞腾派）
# 启动 xfce4 桌面 + Firefox 浏览器，打开 task2 多节点 UKF 面板
# 使用 display :3 (port 5903)，避免与 dashboard_board 的 :2 (port 5902) 冲突

set -e

DISPLAY_NUM=":3"
GEOMETRY="1920x1080"
DEPTH="24"
DASHBOARD_URL="http://localhost:5001"

# 1. 清理旧 VNC 会话
if vncserver -list 2>/dev/null | grep -q "$DISPLAY_NUM"; then
    echo "Killing old VNC $DISPLAY_NUM ..."
    vncserver -kill "$DISPLAY_NUM" 2>/dev/null || true
    sleep 2
fi

# 2. 创建 xstartup
mkdir -p "$HOME/.vnc"

cat > "$HOME/.vnc/xstartup_task2" << 'XEOF'
#!/bin/bash
# Task2 VNC xstartup — 多节点 UKF 面板

if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-launch >/dev/null 2>&1; then
    eval $(dbus-launch --sh-syntax --exit-with-session)
    export DBUS_SESSION_BUS_ADDRESS
fi

# 启动轻量桌面组件
if command -v xfdesktop >/dev/null 2>&1; then
    xfdesktop --display="$DISPLAY" &
fi
if command -v xfce4-panel >/dev/null 2>&1; then
    xfce4-panel --display="$DISPLAY" &
fi
if command -v xfwm4 >/dev/null 2>&1; then
    xfwm4 --display="$DISPLAY" &
fi

sleep 3

# 启动 Firefox 打开 task2 Dashboard
if command -v firefox-esr >/dev/null 2>&1; then
    firefox-esr --new-window http://localhost:5001 &
fi

wait
XEOF

chmod +x "$HOME/.vnc/xstartup_task2"
cp "$HOME/.vnc/xstartup_task2" "$HOME/.vnc/xstartup"

# 3. 启动 VNC
echo "Starting Task2 VNC $DISPLAY_NUM (port 5903)..."
vncserver "$DISPLAY_NUM" -geometry "$GEOMETRY" -depth "$DEPTH" -localhost no

sleep 2

echo "Task2 VNC ready: vncviewer 192.168.88.10:5903"
echo "Dashboard: $DASHBOARD_URL"