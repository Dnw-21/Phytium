#!/bin/bash
# deploy_to_board.sh — 部署所有文件到开发板 + 执行测试
set -e
BOARD="user@192.168.4.51"
BOARD_DIR="/home/user/Phytium/task2_linux"
SSH="sshpass -p user ssh -o StrictHostKeyChecking=no"
SCP="sshpass -p user scp -o StrictHostKeyChecking=no"

echo "=== Deploying to $BOARD ==="

# 部署脚本
for f in start_all.sh monitor.sh launch_ukf_multi.py; do
    echo "  scp $f ..."
    $SCP "$f" "$BOARD:$BOARD_DIR/"
done

# 部署 ARM64 二进制
for f in ukf_pipeline start_sim_nodes reset_shm; do
    echo "  scp $f ..."
    $SCP "$f" "$BOARD:$BOARD_DIR/"
done

# 设置权限
$SSH "$BOARD" "cd $BOARD_DIR && chmod +x start_all.sh monitor.sh ukf_pipeline start_sim_nodes reset_shm"
echo "=== Deploy done ==="

# 验证
echo "=== Verifying files on board ==="
$SSH "$BOARD" "cd $BOARD_DIR && file ukf_pipeline start_sim_nodes reset_shm && ls -lh start_all.sh monitor.sh launch_ukf_multi.py ukf_pipeline start_sim_nodes reset_shm"
