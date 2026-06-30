#!/bin/bash
# ============================================================
# status.sh — 一键综合状态诊断脚本
# ============================================================
# 用法: ./status.sh
# 
# 显示内容:
#   1. 系统概览 (内核/架构/可用CPU)
#   2. FreeRTOS 状态 (remoteproc state)
#   3. 三节点 SHM 写入帧数 + 速度
#   4. UKF Pipeline 状态 (进程数/帧数/FPS/RMSE/CPU绑定)
#   5. 三核 CPU 占用率
#   6. 内存使用
#   7. 进程详情 (PID/RSS/CPU绑定)
#   8. 最近的错误日志
# ============================================================

PASS="user"

sep() { echo "══════════════════════════════════════════════════════════════"; }
hdr() { echo ""; echo "▸ $*"; echo "──────────────────────────────────────────────────────────────"; }

hdr "1. 系统概览"
echo "  内核   : $(uname -r)"
echo "  架构   : $(uname -m)"
echo "  可用CPU: $(nproc)"
echo "  在线CPU: $(cat /sys/devices/system/cpu/online 2>/dev/null)"
echo "  运行时间: $(uptime | cut -d',' -f1)"
echo "  负载   : $(uptime | awk -F'load average:' '{print $2}')"

hdr "2. FreeRTOS (remoteproc) 状态"
REMOTE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null || echo "NOT_FOUND")
if [ "$REMOTE" = "running" ]; then
    echo "  ✅ FreeRTOS: RUNNING (Core 1 独占)"
else
    echo "  ❌ FreeRTOS: $REMOTE"
fi

hdr "3. 三节点 SHM 帧数 (FreeRTOS 侧)"
declare -A SHM
SHM["5bus"]="0xC8100000"
SHM["39bus"]="0xC8140000"
SHM["9bus"]="0xC81C0000"

# 先采样一次
declare -A CNT1 CNT2
for node in 5bus 39bus 9bus; do
    addr="${SHM[$node]}"
    cnt_offset=$((addr + 8))
    val=$(echo "$PASS" | sudo -S dd if=/dev/mem bs=1 skip=$cnt_offset count=4 2>/dev/null | od -A n -t u4 | tr -d ' ')
    CNT1[$node]=${val:-0}
done

sleep 1

# 再采样一次
for node in 5bus 39bus 9bus; do
    addr="${SHM[$node]}"
    cnt_offset=$((addr + 8))
    val=$(echo "$PASS" | sudo -S dd if=/dev/mem bs=1 skip=$cnt_offset count=4 2>/dev/null | od -A n -t u4 | tr -d ' ')
    CNT2[$node]=${val:-0}
done

for node in 5bus 39bus 9bus; do
    addr="${SHM[$node]}"
    cnt=${CNT2[$node]}
    rate=$((CNT2[$node] - CNT1[$node]))
    if [ "${CNT2[$node]}" -gt 0 ] && [ "$rate" -gt 0 ]; then
        printf "  %-6s @ %s: %8d frames  [+%d fps]  ✅ 写入中\n" \
            "$node" "$addr" "$cnt" "$rate"
    elif [ "${CNT2[$node]}" -gt 0 ]; then
        printf "  %-6s @ %s: %8d frames  [完成]  ✅\n" \
            "$node" "$addr" "$cnt"
    else
        printf "  %-6s @ %s: %8d frames  ⏳ 等待中\n" \
            "$node" "$addr" "$cnt"
    fi
done

hdr "4. UKF Pipeline 状态"
METRICS="/tmp/ukf_metrics.json"
if [ -f "$METRICS" ]; then
    python3 -c "
import json
with open('$METRICS') as f:
    d = json.load(f)
for n in ['5bus','39bus','9bus']:
    x = d.get(n,{})
    print(f\"  {n:6s}: frames={x.get('frames',0):>6d}  fps={x.get('fps',0):>6.0f}  rmse={x.get('rmse',0):.5f}  cpu={x.get('cpu_pct',0):.0f}%  core={x.get('cpu_core','?')}  status={x.get('status','?')}\")
" 2>/dev/null
else
    echo "  ⏳ 指标文件尚未生成 (UKF 未启动或未开始接收数据)"
fi

hdr "5. CPU 占用率 (三核)"
read_cpu() { awk '/^cpu[0-2] /{total=$2+$3+$4+$5+$6+$7+$8; idle=$5+$6; print $1,total,idle}' /proc/stat; }
declare -A T1 I1
while read -r core total idle; do T1[$core]=$total; I1[$core]=$idle; done < <(read_cpu)
sleep 1
while read -r core total idle; do
    dt=$((total - ${T1[$core]:-0})); di=$((idle - ${I1[$core]:-0}))
    pct=0; [ $dt -gt 0 ] && pct=$((100 * (dt - di) / dt))
    case "${core#cpu}" in
        0) tag="5bus+9bus+OS" ;; 1) tag="FreeRTOS(RK4)" ;; 2) tag="39bus UKF" ;;
    esac
    bar=""; for ((i=0; i<pct/5; i++)); do bar="${bar}#"; done
    printf "  Core %s (%s): %3d%% %s\n" "${core#cpu}" "$tag" "$pct" "$bar"
done < <(read_cpu)

hdr "6. 内存使用"
read -r total avail < <(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{print t,a}' /proc/meminfo)
used=$((total - avail))
pct=$((100 * used / total))
bar=""
for ((i=0; i<pct/5; i++)); do bar="${bar}#"; done
printf "  已用: %d/%d MB (%d%%) %s\n" $((used/1024)) $((total/1024)) "$pct" "$bar"

hdr "7. 进程详情"
UKF_COUNT=$(pgrep -c ukf_pipeline 2>/dev/null)
UKF_COUNT=${UKF_COUNT:-0}
echo "  C UKF 进程: $UKF_COUNT/3"
if [ "$UKF_COUNT" -gt 0 ]; then
    printf "  %-20s %-8s %-10s %-12s %s\n" "Binary" "PID" "RSS(KB)" "CPU绑定" "状态"
    printf "  %-6s %-8s %-10s %-12s %s\n" "------" "--------" "----------" "------------" "-----"
    for pid in $(pgrep ukf_pipeline 2>/dev/null); do
        rss=$(ps -p $pid -o rss --no-headers 2>/dev/null | tr -d ' ')
        comm=$(ps -p $pid -o comm --no-headers 2>/dev/null | tr -d ' ')
        aff=$(taskset -p $pid 2>/dev/null | awk -F': ' '{print $NF}')
        state=$(ps -p $pid -o state --no-headers 2>/dev/null | tr -d ' ')
        printf "  %-20s %-8s %-10s %-12s %s\n" "${comm:-?}" "$pid" "${rss:-?}" "$aff" "$state"
    done
fi
echo ""
PY_COUNT=$(pgrep -c launch_ukf_multi 2>/dev/null)
PY_COUNT=${PY_COUNT:-0}
echo "  Python launcher: ${PY_COUNT} process(es)"

hdr "8. 最近的 UKF 日志 (最后3行/节点)"
for node in 5bus 39bus 9bus; do
    LOGF="/tmp/ukf_log_${node}.log"
    if [ -f "$LOGF" ]; then
        echo "  [$node]"
        tail -3 "$LOGF" 2>/dev/null | while read line; do echo "    $line"; done
    fi
done

echo ""
sep
echo "  提示: 持续监控请运行  ./monitor.sh"
echo "        重新测试请运行  ./start_all.sh 10"
sep
