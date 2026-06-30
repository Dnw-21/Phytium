#!/bin/bash
# reload_firmware.sh — 热重载 FreeRTOS 固件，无需整板重启
# ============================================================
# 用法:
#   sudo ./reload_firmware.sh [firmware_path]
#
# 原理:
#   1. 停止 remoteproc (CPU1 停止执行 FreeRTOS)
#   2. 替换 /lib/firmware 下的固件文件
#   3. 重新启动 remoteproc (CPU1 重新加载并执行 FreeRTOS)
#
# 注意:
#   - 需要先杀掉所有使用 RPMsg 的 Linux 进程
#   - 停止期间 RPMsg 通道会断开，UKF pipeline 需要重启

set -e

FIRMWARE="${1:-/lib/firmware/openamp_core0.elf}"
REMOTEPROC_PATH="/sys/class/remoteproc/remoteproc0"
ORIGINAL="/lib/firmware/openamp_core0.elf"

echo "============================================"
echo "  FreeRTOS 固件热重载"
echo "============================================"
echo ""
echo "  新固件: $FIRMWARE"
echo "  目标:   $REMOTEPROC_PATH"
echo ""

# 1. 检查 remoteproc 是否存在
if [ ! -d "$REMOTEPROC_PATH" ]; then
    echo "[ERROR] remoteproc 未找到: $REMOTEPROC_PATH"
    echo "        请确认开发板已启动且 remoteproc 驱动已加载"
    exit 1
fi

# 2. 检查新固件
if [ ! -f "$FIRMWARE" ]; then
    echo "[ERROR] 固件文件不存在: $FIRMWARE"
    exit 1
fi

# 3. 杀掉持有 RPMsg 设备的所有进程
echo "[1/5] 清理持有 RPMsg 的进程..."
RPMsg_PIDS=$(lsof -t /dev/rpmsg* 2>/dev/null || true)
if [ -n "$RPMSG_PIDS" ]; then
    echo "      杀掉进程: $RPMSG_PIDS"
    kill -TERM $RPMSG_PIDS 2>/dev/null || true
    sleep 1
    kill -KILL $RPMSG_PIDS 2>/dev/null || true
fi

# 也杀掉 ukf_pipeline 和 start_sim_nodes
pkill -f ukf_pipeline 2>/dev/null || true
pkill -f start_sim_nodes 2>/dev/null || true
echo "      完成"

# 4. 停止 remoteproc
echo "[2/5] 停止 remoteproc (CPU1)..."
CURRENT_STATE=$(cat "$REMOTEPROC_PATH/state" 2>/dev/null || echo "unknown")
echo "      当前状态: $CURRENT_STATE"

if [ "$CURRENT_STATE" != "offline" ]; then
    echo "stop" > "$REMOTEPROC_PATH/state" 2>/dev/null || {
        echo "[ERROR] 无法停止 remoteproc"
        exit 1
    }
    # 等待停止
    for i in $(seq 1 20); do
        S=$(cat "$REMOTEPROC_PATH/state" 2>/dev/null)
        if [ "$S" = "offline" ]; then break; fi
        sleep 0.5
    done
    NEW_STATE=$(cat "$REMOTEPROC_PATH/state" 2>/dev/null)
    echo "      新状态: $NEW_STATE"
fi

# 5. 替换固件
echo "[3/5] 替换固件文件..."
if [ "$FIRMWARE" != "$ORIGINAL" ]; then
    cp "$FIRMWARE" "$ORIGINAL"
    echo "      已复制 $FIRMWARE → $ORIGINAL"
else
    echo "      固件路径未变，跳过复制"
fi
ls -la "$ORIGINAL"

# 6. 等待一小段时间确保资源释放
echo "[4/5] 等待资源释放..."
sleep 2

# 7. 启动 remoteproc
echo "[5/5] 启动 remoteproc (CPU1)..."
echo "start" > "$REMOTEPROC_PATH/state" 2>/dev/null || {
    echo "[ERROR] 无法启动 remoteproc"
    exit 1
}

# 等待启动
for i in $(seq 1 20); do
    S=$(cat "$REMOTEPROC_PATH/state" 2>/dev/null)
    echo "      状态: $S"
    if [ "$S" = "running" ]; then break; fi
    sleep 0.5
done

FINAL_STATE=$(cat "$REMOTEPROC_PATH/state" 2>/dev/null)
echo ""

if [ "$FINAL_STATE" = "running" ]; then
    echo "============================================"
    echo "  SUCCESS: FreeRTOS 已重新启动"
    echo "============================================"
    echo ""
    echo "  验证: cat $REMOTEPROC_PATH/state"
    echo "  trace: cat /sys/kernel/debug/remoteproc/remoteproc0/trace0"
    echo ""
    echo "  下一步: 重新启动 UKF pipeline"
    echo "    sudo python3 launch_ukf_multi.py"
else
    echo "============================================"
    echo "  WARNING: 状态为 '$FINAL_STATE' (期望 'running')"
    echo "  请检查 dmesg | tail -20"
    echo "============================================"
    exit 1
fi