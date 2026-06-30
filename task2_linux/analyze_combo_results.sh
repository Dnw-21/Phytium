#!/bin/bash
# analyze_combo_results.sh — 解析 /tmp/combo_*.log 并生成测试报告摘要
# 运行环境: 开发板 Linux 侧

set -e

REPORT="/tmp/combo_analysis_report.txt"

echo "=== 多节点组合测试结果分析 ===" > "$REPORT"
echo "" >> "$REPORT"

printf "%-10s %-10s %-10s %-8s %-8s %-8s %-12s %-12s %-12s %-10s\n" \
    "E5" "E9" "E39" "CPU0%" "CPU1%" "CPU2%" "5bus_frames" "9bus_frames" "39bus_frames" "status" >> "$REPORT"
printf "%-10s %-10s %-10s %-8s %-8s %-8s %-12s %-12s %-12s %-10s\n" \
    "---" "---" "---" "-----" "-----" "-----" "-----------" "-----------" "------------" "------" >> "$REPORT"

for log in /tmp/combo_*.log; do
    [ -f "$log" ] || continue
    tag=$(basename "$log" .log | sed 's/^combo_//')
    E5=$(echo "$tag" | cut -d'_' -f1)
    E9=$(echo "$tag" | cut -d'_' -f2)
    E39=$(echo "$tag" | cut -d'_' -f3)

    cpu0=$(grep "CPU0 total:" "$log" | awk '{print $3}' | tr -d '%' || echo "-")
    cpu1=$(grep "CPU1 total:" "$log" | awk '{print $3}' | tr -d '%' || echo "-")
    cpu2=$(grep "CPU2 total:" "$log" | awk '{print $3}' | tr -d '%' || echo "-")

    # 基础实例帧数
    f5=$(grep -A1 "--- 5bus base ---" "$log" | grep "frames=" | head -1 | sed 's/.*frames=//' | awk '{print $1}' || echo "0")
    f9=$(grep -A1 "--- 9bus base ---" "$log" | grep "frames=" | head -1 | sed 's/.*frames=//' | awk '{print $1}' || echo "0")
    f39=$(grep -A1 "--- 39bus base ---" "$log" | grep "frames=" | head -1 | sed 's/.*frames=//' | awk '{print $1}' || echo "0")

    # 判断状态: 基础实例必须都产生足够帧数
    status="OK"
    if [ "$f5" = "0" ] || [ -z "$f5" ]; then status="FAIL(5bus)"; fi
    if [ "$f9" = "0" ] || [ -z "$f9" ]; then status="FAIL(9bus)"; fi
    if [ "$f39" = "0" ] || [ -z "$f39" ]; then status="FAIL(39bus)"; fi

    printf "%-10s %-10s %-10s %-8s %-8s %-8s %-12s %-12s %-12s %-10s\n" \
        "$E5" "$E9" "$E39" "$cpu0" "$cpu1" "$cpu2" "$f5" "$f9" "$f39" "$status" >> "$REPORT"
done

echo "" >> "$REPORT"
echo "生成时间: $(date)" >> "$REPORT"
echo "报告文件: $REPORT" >> "$REPORT"

cat "$REPORT"
