#!/bin/bash
# VNC 桌面启动脚本（飞腾派）
# 启动完整的 xfce4 桌面 + Firefox 浏览器
# 由 dashboard-vnc.service 调用
# 注意：LightDM 占用 :0 和 :1，VNC 使用 :2

set -e

DISPLAY_NUM=":2"
GEOMETRY="1920x1080"
DEPTH="24"

# 1. 清理旧 VNC 会话
if vncserver -list 2>/dev/null | grep -q "$DISPLAY_NUM"; then
    echo "Killing old VNC $DISPLAY_NUM ..."
    vncserver -kill "$DISPLAY_NUM" 2>/dev/null || true
    sleep 2
fi

# 2. 确保 home 下 xstartup 就位
mkdir -p "$HOME/.vnc"
XSTARTUP_SRC="/home/user/dashboard_board/scripts/xstartup_vnc"
XSTARTUP_DST="$HOME/.vnc/xstartup"
if [ -f "$XSTARTUP_SRC" ]; then
    cp "$XSTARTUP_SRC" "$XSTARTUP_DST"
    chmod +x "$XSTARTUP_DST"
    echo "xstartup installed to $XSTARTUP_DST"
fi

# 3. 启动 VNC 服务
echo "Starting VNC $DISPLAY_NUM with geometry $GEOMETRY ..."
vncserver "$DISPLAY_NUM" -geometry "$GEOMETRY" -depth "$DEPTH" -localhost no

sleep 2

echo "VNC ready: vncviewer 192.168.88.10:5902"
echo "Desktop: xfce4 + Firefox http://localhost:5000"