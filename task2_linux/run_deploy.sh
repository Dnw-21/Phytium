#!/bin/bash
# 完整部署脚本：部署 + 停旧进程 + 跑测试
set -e

BOARD="user@192.168.4.51"
BDIR="/home/user/Phytium/task2_linux"
PASS="user"
SSHPASS="sshpass -p $PASS"
SSH="$SSHPASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
SCP="$SSHPASS scp -o StrictHostKeyChecking=no -o ConnectTimeout=10"

echo "=========================================="
echo "  Multi-Node UKF v5 部署与测试"
echo "=========================================="

# Step 1: 部署所有文件
echo ""
echo "--- Step 1: 部署脚本 ---"
for f in start_all.sh monitor.sh launch_ukf_multi.py status.sh; do
    echo "  scp $f ..."
    $SCP "$f" "$BOARD:$BDIR/" 2>&1
done

echo ""
echo "--- Step 2: 部署 ARM64 二进制 ---"
for f in ukf_pipeline start_sim_nodes reset_shm; do
    echo "  scp $f ..."
    $SCP "$f" "$BOARD:$BDIR/" 2>&1
done

echo ""
echo "--- 设置权限 ---"
$SSH "$BOARD" "cd $BDIR && chmod +x start_all.sh monitor.sh status.sh ukf_pipeline start_sim_nodes reset_shm" 2>&1

echo ""
echo "--- 验证文件 ---"
$SSH "$BOARD" "cd $BDIR && file ukf_pipeline start_sim_nodes reset_shm" 2>&1
$SSH "$BOARD" "cd $BDIR && ls -lh start_all.sh monitor.sh status.sh" 2>&1

# Step 2: 停止旧进程
echo ""
echo "--- Step 3: 停止旧进程 ---"
$SSH "$BOARD" "
    for p in ukf_pipeline launch_ukf_multi start_sim_nodes; do
        sudo killall -9 \$p 2>/dev/null || true
    done
    sleep 1
    echo 'Cleaned'
" 2>&1

# Step 3: 重载 FreeRTOS 固件
echo ""
echo "--- Step 4: 重载 FreeRTOS 固件 ---"
$SSH "$BOARD" "
    echo '$PASS' | sudo -S sh -c 'echo stop > /sys