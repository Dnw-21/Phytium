#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  飞腾派 LoRa 接收 — 快速 LoRa RX 开关
#
#  运行位置: 飞腾派开发板上
#  用法:
#    ~/Phytium/demo/lora_ctrl start     # 开启
#    ~/Phytium/demo/lora_ctrl stop      # 关闭  
#    ~/Phytium/demo/lora_ctrl status    # 查询
# ═══════════════════════════════════════════════════════════════════

LORA_CTRL="${HOME}/Phytium/demo/lora_ctrl"

if [ ! -x "$LORA_CTRL" ]; then
    echo "错误: lora_ctrl 未找到: $LORA_CTRL"
    echo "请先运行 deploy_and_start.sh"
    exit 1
fi

CMD="${1:-status}"

case "$CMD" in
    start|on)
        echo "=== 开启 LoRa 数据接收 ==="
        "$LORA_CTRL" start
        echo ""
        echo "接收已开启，用以下命令查看数据:"
        echo "  ~/Phytium/demo/master_receiver --monitor"
        ;;
    stop|off)
        echo "=== 关闭 LoRa 数据接收 ==="
        "$LORA_CTRL" stop
        echo ""
        echo "接收已暂停。重新开启:"
        echo "  ~/Phytium/demo/lora_ctrl start"
        ;;
    status)
        echo "=== 查询 LoRa 接收状态 ==="
        "$LORA_CTRL" status
        ;;
    *)
        echo "用法: $0 <start|stop|status>"
        exit 1
        ;;
esac
