#!/bin/bash

BOARD_IP="192.168.88.11"
BOARD_USER="user"
BOARD_PASS="user"
REMOTE_DIR="/home/user/openamp-demo"
LOCAL_BUILD="./build"

echo "╔══════════════════════════════════════════╗"
echo "║   OpenAMP Demo 一键部署脚本              ║"
echo "╚══════════════════════════════════════════╝"
echo ""

if [ ! -f "$LOCAL_BUILD/rpmsg_master" ]; then
    echo "[ERROR] 未找到编译好的程序，请先运行: make master"
    exit 1
fi

echo "[1/5] 检查开发板连接..."
sshpass -p "$BOARD_PASS" ssh -o ConnectTimeout=5 $BOARD_USER@$BOARD_IP "echo '[OK] 连接成功'" > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "[ERROR] 无法连接到开发板 $BOARD_IP"
    exit 1
fi

echo "[2/5] 创建远程目录..."
sshpass -p "$BOARD_PASS" ssh $BOARD_USER@$BOARD_IP "mkdir -p $REMOTE_DIR"

echo "[3/5] 上传程序文件..."
sshpass -p "$BOARD_PASS" scp "$LOCAL_BUILD/rpmsg_master" $BOARD_USER@$BOARD_IP:$REMOTE_DIR/
echo "      ✅ rpmsg_master 已上传"

echo "[4/5] 设置执行权限..."
sshpass -p "$BOARD_PASS" ssh $BOARD_USER@$BOARD_IP "chmod +x $REMOTE_DIR/rpmsg_master"

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║           部署完成！                      ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "📋 后续操作步骤："
echo ""
echo "  SSH登录开发板后依次执行："
echo ""
echo "  ① 启动远程处理器（从核）："
echo "     echo start > /sys/class/remoteproc/remoteproc0/state"
echo ""
echo "  ② 绑定rpmsg通道驱动："
echo "     sudo sh -c 'echo rpmsg_chrdev > \\\\ "
echo "       /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'"
echo ""
echo "  ③ 运行demo程序："
echo "     cd $REMOTE_DIR && ./rpmsg_master"
echo ""
echo "  ④ 测试完成后停止："
echo "     Ctrl+C 停止demo"
echo "     echo stop > /sys/class/remoteproc/remoteproc0/state"
echo ""
echo "  或者使用快速测试命令："
echo "     cd $REMOTE_DIR && ./rpmsg_master --count 10 --interval 500"
echo ""
