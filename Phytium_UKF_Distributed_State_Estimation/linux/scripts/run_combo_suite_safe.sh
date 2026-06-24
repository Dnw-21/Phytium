#!/bin/bash
# run_combo_suite_safe.sh — 批量执行多节点 UKF 组合测试（额外实例模式）
# 运行环境: 开发板 Linux 侧 (/home/user/Phytium_UKF_Distributed_State_Estimation/linux)
#
# 说明:
#   - 每组测试始终保持 1×5bus + 1×9bus + 1×39bus 基础节点
#   - 组合参数表示额外增加的同类型只读实例数
#   - 总节点数 = (1+E5)×5bus + (1+E9)×9bus + (1+E39)×39bus

set -e
DIR=/home/user/Phytium_UKF_Distributed_State_Estimation/linux
cd "$DIR"

DURATION=${1:-25}
SUMMARY="/tmp/combo_summary_safe.txt"
export SUMMARY_FILE="$SUMMARY"

# 清空汇总
> "$SUMMARY"
echo "E5 E9 E39 DURATION CPU0 CPU1 CPU2" > "$SUMMARY"

# 组合矩阵: "E5 E9 E39" 表示额外实例数
# 本套件聚焦 250Hz 39bus + 500Hz 5bus/9bus 配置下，CPU 不满载的真实场景
COMBOS=(
    # 基准: 仅基础三节点
    "0 0 0"
    # 场景 A: 轻量级配网监测 (少量 5bus 馈线)
    "2 0 0"
    # 场景 B: 大量小馈线接入 (配电台区集中监测)
    "5 0 0"
    # 场景 C: 中型变电站群监测
    "0 3 0"
    # 场景 D: 均衡混合 (馈线 + 变电站群)
    "3 1 0"
    # 场景 E: 一般混合 (少量馈线 + 少量变电站)
    "1 1 0"
    # 场景 F: 双主干网 (探索多 39bus 上限)
    "0 0 1"
    # 场景 G: 混合 + 双主干网
    "1 0 1"
)

TOTAL=${#COMBOS[@]}
echo "=== 开始安全组合测试套件: 共 ${TOTAL} 组, 每组 ${DURATION}s ==="

idx=1
for c in "${COMBOS[@]}"; do
    read E5 E9 E39 <<< "$c"
    echo ""
    echo "[$idx/$TOTAL] 额外实例: ${E5}×5bus + ${E9}×9bus + ${E39}×39bus (总节点: $((1+E5))+$((1+E9))+$((1+E39)))"
    sudo -E scripts/multi_node_combo_test.sh "$E5" "$E9" "$E39" "$DURATION"
    idx=$((idx+1))
done

echo ""
echo "=== 全部测试完成 ==="
echo "汇总文件: $SUMMARY"
cat "$SUMMARY"
