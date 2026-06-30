#!/bin/bash
# run_combo_suite.sh — 批量执行多节点 UKF 组合测试
# 运行环境: 开发板 Linux 侧 (/home/user/Phytium/task2_linux)
#
# 组合设计思路:
#   - 覆盖单一类型高密度布局 (全 5bus / 全 9bus / 全 39bus)
#   - 覆盖典型混合布局 (5bus+9bus 不同比例)
#   - 覆盖带 39bus 大系统的混合布局
#   - 探索极限配置 (最大稳定节点数)

set -e
DIR=/home/user/Phytium/task2_linux
cd "$DIR"

DURATION=${1:-25}
SUMMARY="/tmp/combo_summary.txt"

# 清空汇总
> "$SUMMARY"
echo "N5 N9 N39 DURATION CPU0 CPU1 CPU2" > "$SUMMARY"

# 组合矩阵: "N5 N9 N39"
COMBOS=(
    "1 0 1"
    "0 1 1"
    "2 0 1"
    "0 2 1"
    "3 0 1"
    "2 1 1"
    "1 2 1"
    "0 3 1"
    "4 0 1"
    "3 1 1"
    "2 2 1"
    "5 0 1"
    "4 1 1"
    "3 2 1"
    "6 0 1"
    "5 1 1"
    "7 0 1"
    "0 0 2"
    "1 0 2"
    "0 1 2"
    "3 3 0"
    "4 2 0"
    "2 4 0"
)

TOTAL=${#COMBOS[@]}
echo "=== 开始组合测试套件: 共 ${TOTAL} 组, 每组 ${DURATION}s ==="

idx=1
for c in "${COMBOS[@]}"; do
    read N5 N9 N39 <<< "$c"
    echo ""
    echo "[$idx/$TOTAL] 测试组合: ${N5}×5bus + ${N9}×9bus + ${N39}×39bus"
    sudo ./multi_node_combo_test.sh "$N5" "$N9" "$N39" "$DURATION"
    idx=$((idx+1))
done

echo ""
echo "=== 全部测试完成 ==="
echo "汇总文件: $SUMMARY"
cat "$SUMMARY"
