#!/bin/bash
# 查看 dashboard 状态
echo "=== systemd ==="
systemctl status dashboard-board.service --no-pager 2>/dev/null | head -5
echo ""
echo "=== VNC ==="
vncserver -list 2>/dev/null
echo ""
echo "=== Logs (recent) ==="
tail -5 /home/user/dashboard_board/logs/server.log 2>/dev/null