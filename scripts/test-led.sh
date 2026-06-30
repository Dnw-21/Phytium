#!/bin/bash
# LED控制快速测试脚本 - 无需手动输入sudo密码
# 使用方法: ./test-led.sh

BOARD_IP="192.168.88.10"
USER="user"
PASS="user"

echo "=========================================="
echo "  飞腾派 LED 控制测试脚本"
echo "=========================================="
echo ""

sshpass -p "$PASS" ssh "$USER@$BOARD_IP" bash << 'EOF'
cd ~/iot-monitoring-system

echo "[1/4] 编译程序..."
make > /dev/null 2>&1 && echo "✓ 编译成功" || { echo "✗ 编译失败"; exit 1; }

echo ""
echo "[2/4] 测试LED闪烁 (请仔细观察板子上的所有指示灯)..."
echo "    程序将让LED闪烁5次，每次间隔500ms"
echo ""

# 使用sudo运行，自动传入密码
echo "$PASS" | sudo -S timeout 8 ./build/iot-main sysled --blink --interval 500 --count 5 2>/dev/null

echo ""
echo "[3/4] 测试完成！"
echo ""
echo "[4/4] 如果没有看到任何LED变化："
echo "    - sysled 可能是内部状态LED（不可见）"
echo "    - 板子上的2个绿色LED是硬件电源灯（无法软件控制）"
echo "    - 如需控制外部LED，需要外接到GPIO引脚"
EOF

echo ""
echo "=========================================="
