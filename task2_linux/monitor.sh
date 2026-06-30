#!/bin/bash
# ============================================================
# monitor.sh — 三节点 UKF 实时状态监控面板 (v6)
# ============================================================
# 显示内容:
#   1. 三节点 UKF 帧数 / FPS / RMSE / 延迟 / CPU / 状态
#   2. 三核 CPU 占用率柱状图 (Core 0/1/2)
#   3. 内存使用 (used/total + 百分比)
#   4. 进程存活状态 (UKF C 进程 + Python launcher)
#   5. SHM 写入帧数 (直接读 /dev/mem)
#   6. FreeRTOS remoteproc 状态
#
# 用法: ./monitor.sh [间隔秒数, 默认=2]
# ============================================================

INTERVAL=${1:-2}
METRICS_FILE="/tmp/ukf_metrics.json"
declare -A PREV_TOTAL PREV_IDLE

# ── 颜色 ──
RED=$(tput setaf 1 2>/dev/null || echo '')
GRN=$(tput setaf 2 2>/dev/null || echo '')
YLW=$(tput setaf 3 2>/dev/null || echo '')
BLU=$(tput setaf 4 2>/dev/null || echo '')
CYN=$(tput setaf 6 2>/dev/null || echo '')
RST=$(tput sgr0 2>/dev/null || echo '')
BOLD=$(tput bold 2>/dev/null || echo '')

# ── 工具函数 ──
bar() {
    local pct=$1 max=${2:-50} label=${3:-""}
    local n=$((pct * max / 100))
    [ $n -gt $max ] && n=$max
    [ $n -lt 0 ] && n=0
    local filled="" empty=""
    for ((i=0; i<n; i++)); do filled="${filled}#"; done
    for ((i=n; i<max; i++)); do empty="${empty}-"; done
    if [ $pct -gt 80 ]; then
        printf "%s[%3d%%]%s ${RED}%s${RST}${BLU}%s${RST}" "$label" "$pct" "" "$filled" "$empty"
    elif [ $pct -gt 50 ]; then
        printf "%s[%3d%%]%s ${YLW}%s${RST}${BLU}%s${RST}" "$label" "$pct" "" "$filled" "$empty"
    else
        printf "%s[%3d%%]%s ${GRN}%s${RST}${BLU}%s${RST}" "$label" "$pct" "" "$filled" "$empty"
    fi
}

# ── CPU 速率采样 ──
sample_cpu() {
    for core in cpu0 cpu1 cpu2 cpu3; do
        read -r line < <(grep "^$core " /proc/stat 2>/dev/null)
        read -r _ u1 u2 u3 idle iowait irq sirq <<< "$line"
        local total=$((u1 + u2 + u3 + idle + iowait + irq + sirq))
        local idle_sum=$((idle + iowait))
        local pct=0
        if [ -n "${PREV_TOTAL[$core]}" ] && [ "${PREV_TOTAL[$core]}" -gt 0 ]; then
            local dt=$((total - PREV_TOTAL[$core]))
            local di=$((idle_sum - PREV_IDLE[$core]))
            if [ $dt -gt 0 ]; then
                pct=$((100 * (dt - di) / dt))
            fi
        fi
        PREV_TOTAL[$core]=$total
        PREV_IDLE[$core]=$idle_sum
        echo "$core $pct $total $idle_sum"
    done
}

# ── 首次采样 (预热) ──
sample_cpu > /dev/null
sleep 0.5

# ── 主循环 ──
echo "${BOLD}UKF Multi-Node Monitor v6 — refresh every ${INTERVAL}s${RST}"
echo "Core 0: 5bus+9bus Online UKF | Core 1: FreeRTOS RK4 | Core 2: 39bus Online UKF"
echo ""

while true; do
    tput clear 2>/dev/null || printf "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"

    NOW=$(date '+%Y-%m-%d %H:%M:%S')
    echo "${CYN}=== ${NOW} ===${RST}"
    echo ""

    # ════════════════════════════════════════════
    # 1. UKF 节点状态表
    # ════════════════════════════════════════════
    echo "${BOLD}── UKF Pipeline Status ──${RST}"
    printf "+-----------+----------+----------+----------+----------+----------+------------+\n"
    printf "| %-9s | %8s | %8s | %8s | %8s | %7s | %-10s |\n" \
        "Node" "Frames" "FPS" "RMSE" "Lat(us)" "CPU%" "Status"
    printf "+-----------+----------+----------+----------+----------+----------+------------+\n"

    if [ -f "$METRICS_FILE" ]; then
        python3 -c "
import json, sys
try:
    with open('$METRICS_FILE') as f:
        data = json.load(f)
    for node in ['5bus','39bus','9bus']:
        n = data.get(node, {})
        frames = n.get('frames', 0)
        fps = n.get('fps', 0)
        rmse = n.get('rmse', 0)
        lat = int(n.get('latency_us', 0))
        cpu = n.get('cpu_pct', 0)
        status = n.get('status', '?')[:10]
        print(f'{node}|{frames}|{fps}|{rmse}|{lat}|{cpu}|{status}')
except Exception as e:
    print(f'error|0|0|{str(e)[:20]}|0|0|parse_err', file=sys.stderr)
" 2>/dev/null | while IFS='|' read name frames fps rmse lat cpu status; do
            printf "| %-9s | %8s | %8s | %8s | %8s | %7s | %-10s |\n" \
                "$name" "$frames" "$fps" "$rmse" "$lat" "$cpu" "$status"
        done
    else
        for node in 5bus 39bus 9bus; do
            printf "| %-9s | %8s | %8s | %8s | %8s | %7s | %-10s |\n" \
                "$node" "0" "0" "0" "0" "0" "waiting"
        done
    fi
    printf "+-----------+----------+----------+----------+----------+----------+------------+\n"
    echo ""

    # ════════════════════════════════════════════
    # 2. CPU 占用 (三核柱状图)
    # ════════════════════════════════════════════
    echo "${BOLD}── CPU Usage (per-core) ──${RST}"
    sample_cpu | while read -r core pct total idle; do
        core_num="${core#cpu}"
        tag=""
        case $core_num in
            0) tag="5bus+9bus+OS" ;;
            1) tag="FreeRTOS(RK4)" ;;
            2) tag="39bus UKF" ;;
        esac
        if [ "$core_num" -le 2 ]; then
            printf "  ${CYN}Core %s${RST} %-18s " "$core_num" "($tag)"
            bar "$pct" 50 ""
            echo ""
        fi
    done
    echo ""

    # ════════════════════════════════════════════
    # 3. 内存使用
    # ════════════════════════════════════════════
    read -r mem_total mem_avail < <(awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "%d %d",t,a}' /proc/meminfo 2>/dev/null)
    if [ -n "$mem_total" ] && [ "$mem_total" -gt 0 ]; then
        mem_used=$((mem_total - mem_avail))
        mem_pct=$((100 * mem_used / mem_total))
        echo "${BOLD}── Memory ──${RST}"
        printf "  Used: %d/%d MB  " "$((mem_used / 1024))" "$((mem_total / 1024))"
        bar "$mem_pct" 40 ""
        echo ""
    fi
    echo ""

    # ════════════════════════════════════════════
    # 4. 进程存活
    # ════════════════════════════════════════════
    echo "${BOLD}── Process Status ──${RST}"
    UKF_COUNT=$(pgrep -c ukf_pipeline 2>/dev/null || echo 0)
    PY_COUNT=$(pgrep -cf launch_ukf_multi.py 2>/dev/null || echo 0)
    if [ "$UKF_COUNT" -eq 3 ]; then
        printf "  ${GRN}ukf_pipeline (C UKF):  %d/3 running${RST}\n" "$UKF_COUNT"
    elif [ "$UKF_COUNT" -gt 0 ]; then
        printf "  ${YLW}ukf_pipeline (C UKF):  %d/3 running${RST}\n" "$UKF_COUNT"
    else
        printf "  ${RED}ukf_pipeline (C UKF):  %d/3 running${RST}\n" "$UKF_COUNT"
    fi

    if [ "$PY_COUNT" -ge 1 ]; then
        printf "  ${GRN}launch_ukf_multi.py:    %d running${RST}\n" "$PY_COUNT"
    else
        printf "  ${RED}launch_ukf_multi.py:    %d (DEAD!)${RST}\n" "$PY_COUNT"
    fi

    # 显示每个 UKF 进程的 CPU 绑定
    if [ "$UKF_COUNT" -gt 0 ]; then
        for pid in $(pgrep ukf_pipeline 2>/dev/null); do
            AFFINITY=$(taskset -p "$pid" 2>/dev/null | awk '{print $NF}' | sed 's/,/,/g')
            CMD=$(ps -p "$pid" -o comm --no-headers 2>/dev/null | tr -d ' ')
            printf "    PID %-6s %-20s → affinity %s\n" "$pid" "${CMD:-?}" "$AFFINITY"
        done
    fi
    echo ""

    # ════════════════════════════════════════════
    # 5. SHM 数据量 & FreeRTOS 状态
    # ════════════════════════════════════════════
    echo "${BOLD}── SHM Frame Count & FreeRTOS ──${RST}"

    # 5bus SHM: base=0xC8100000, cnt_offset=8
    VAL_5BUS=$(sudo dd if=/dev/mem bs=1 skip=$((0xC8100008)) count=4 2>/dev/null | od -A n -t u4 | tr -d ' ')
    printf "  5bus  SHM @ 0xC8100000: %s frames\n" "${VAL_5BUS:-?}"

    # 39bus SHM: base=0xC8140000, cnt_offset=8
    VAL_39BUS=$(sudo dd if=/dev/mem bs=1 skip=$((0xC8140008)) count=4 2>/dev/null | od -A n -t u4 | tr -d ' ')
    printf "  39bus SHM @ 0xC8140000: %s frames\n" "${VAL_39BUS:-?}"

    # 9bus SHM: base=0xC81C0000, cnt_offset=8
    VAL_9BUS=$(sudo dd if=/dev/mem bs=1 skip=$((0xC81C0008)) count=4 2>/dev/null | od -A n -t u4 | tr -d ' ')
    printf "  9bus  SHM @ 0xC81C0000: %s frames\n" "${VAL_9BUS:-?}"

    # FreeRTOS remoteproc 状态
    REMOTE_STATE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null || echo "unknown")
    if [ "$REMOTE_STATE" = "running" ]; then
        printf "  FreeRTOS remoteproc: ${GRN}%s${RST}\n" "$REMOTE_STATE"
    else
        printf "  FreeRTOS remoteproc: ${RED}%s${RST}\n" "$REMOTE_STATE"
    fi
    echo ""

    # ════════════════════════════════════════════
    # 6. UKF 进程内存占用
    # ════════════════════════════════════════════
    echo "${BOLD}── UKF Process Memory (RSS) ──${RST}"
    if [ "$UKF_COUNT" -gt 0 ]; then
        ps -C ukf_pipeline -o pid,rss,args --no-headers 2>/dev/null | while read -r pid rss args; do
            NODE=$(echo "$args" | grep -oP '(?<=--node )\S+' || echo "?")
            printf "  %-6s PID %-6s  RSS: %d KB\n" "$NODE" "$pid" "$rss"
        done
    fi

    echo ""
    echo "Press Ctrl+C to exit | refresh: ${INTERVAL}s | $(date +%H:%M:%S)"

    sleep "$INTERVAL"
done
