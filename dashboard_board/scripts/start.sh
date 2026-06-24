#!/bin/bash
# 手动启动 dashboard HTTP 服务
cd /home/user/dashboard_board
echo "Starting dashboard HTTP server..."
nohup python3 server/dashboard_server.py >> logs/server.log 2>&1 &
echo $! > server.pid
sleep 2
if kill -0 $(cat server.pid) 2>/dev/null; then
    echo "Dashboard started (PID: $(cat server.pid))"
    echo "  HTTP: http://localhost:5000"
else
    echo "ERROR: Failed to start"
    exit 1
fi