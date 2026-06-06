#!/bin/bash
# 停止 dashboard 服务
cd /home/user/dashboard_board
if [ -f server.pid ]; then
    PID=$(cat server.pid)
    if kill -0 $PID 2>/dev/null; then
        kill $PID
        echo "Dashboard stopped (PID: $PID)"
    fi
    rm -f server.pid
fi
# also stop VNC
vncserver -kill :1 2>/dev/null
echo "Done"