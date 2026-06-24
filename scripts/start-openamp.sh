#!/bin/bash
# OpenAMP 一键启动 (在开发板上直接运行)
# 用法: ~/start-openamp.sh

echo '=== Phytium Pi OpenAMP 启动 ==='

if [ -f /tmp/openamp.running ]; then
    echo '[WARN] OpenAMP 已在运行中! 如需重启请先运行 stop-openamp.sh'
    exit 1
fi

PASS="user"

echo '[1/4] 加载模块...'
echo "$PASS" | sudo -S modprobe rpmsg_char rpmsg_ctrl 2>/dev/null

echo '[2/4] 启动 FreeRTOS 主控侧（实际 CPU1，设备树写 CPU3）...'
echo "$PASS" | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 2
STATE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null)
[ "$STATE" != "running" ] && echo "[ERROR] 从核启动失败! $STATE" && exit 1
echo "  从核: $STATE"

echo '[3/4] 绑定通道...'
CH=$(ls /sys/bus/rpmsg/devices/ 2>/dev/null | grep openamp-demo)
[ -z "$CH" ] && echo "[ERROR] 无RPMsg通道!" && exit 1
echo "$PASS" | sudo -S sh -c "echo rpmsg_chrdev > /sys/bus/rpmsg/devices/$CH/driver_override"
echo "$PASS" | sudo -S sh -c "echo $CH > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind"
echo "$PASS" | sudo -S chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0 2>/dev/null

echo '[4/4] OpenAMP 通道已就绪...'
echo '  当前 UKF Dashboard 请在开发环境运行: cd /home/alientek/Phytium/state_estimation && python dashboard_server.py'
touch /tmp/openamp.running

echo '========================================'
echo '  OpenAMP 已启动!'
echo '  Dashboard: state_estimation/dashboard_server.py (端口 5000，开发环境运行)'
echo '  停止: ~/stop-openamp.sh'
echo '========================================'
