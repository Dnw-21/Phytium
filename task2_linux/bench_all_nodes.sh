#!/bin/bash
# ============================================================
# bench_all_nodes.sh — 同时运行 5bus/9bus/39bus, 对比 FT vs 非 FT
# ============================================================
# 在 Phytium 开发板上运行, 三个 UKF 同时消费, 观察整体吞吐、延迟、RMSE 及 CPU/内存占用。
# 用法: ./bench_all_nodes.sh [时长秒数, 默认=30]
# ============================================================

DUR=${1:-30}
DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="$DIR/logs_bench_all_nodes"
mkdir -p "$LOG_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

NODES=(5bus 9bus 39bus)
SHM_CNT_ADDR=(0xC8100008 0xC81C0008 0xC8140008)
CPU_AFFINITY=(0 0 2)   # 5bus/9bus 在 CPU0, 39bus 在 CPU2; FreeRTOS 在 CPU1

echo "=============================================="
echo " All-Node Multi-UKF Benchmark"
echo " Duration: ${DUR}s per mode"
echo " Timestamp: $TIMESTAMP"
echo "=============================================="

# 读取 /proc/stat 单个 CPU 的 total/idle
cpu_snapshot() {
    local core=$1
    awk -v c="cpu$core" '$1==c{for(i=2;i<=NF;i++)t+=$i; print t, $5}' /proc/stat
}

# 计算 CPU 使用率 (%)
cpu_usage() {
    read t0 i0 <<< "$1"
    read t1 i1 <<< "$2"
    if [ "$t1" -gt "$t0" ]; then
        awk -v t0="$t0" -v i0="$i0" -v t1="$t1" -v i1="$i1" 'BEGIN{printf "%.1f", 100*( (t1-t0)-(i1-i0) )/(t1-t0)}'
    else
        echo 0.0
    fi
}

parse_log() {
    local log=$1
    local last_hb=$(grep -E "\[ukf-${2}\] t=" "$log" | tail -1 || true)
    local final=$(grep -E "\[ukf-${2}\] done:" "$log" | tail -1 || true)
    local frames=0 fps=0 lat=0 rmse=0 sim_fps=0
    if [ -n "$final" ]; then
        frames=$(echo "$final" | grep -oE '\bframes=[0-9]+' | cut -d= -f2 || true)
        fps=$(echo "$final" | grep -oE '\bfps=[0-9.]+' | cut -d= -f2 || true)
        lat=$(echo "$final" | grep -oE 'avg_lat=[0-9.]+us' | grep -oE '[0-9.]+' | cut -d. -f1 || true)
        sim_fps=$(echo "$final" | grep -oE '\bshm_fps=[0-9.]+' | cut -d= -f2 || true)
    fi
    # 若进程被 SIGKILL 没有 done 行, 从最后心跳提取 frames/fps/lat/sim_fps
    if [ -z "$frames" ] || [ "$frames" -eq 0 ]; then
        if [ -n "$last_hb" ]; then
            frames=$(echo "$last_hb" | grep -oE '\bframes=[0-9]+' | cut -d= -f2 || true)
            lat=$(echo "$last_hb" | grep -oE 'lat=[0-9]+us' | grep -oE '[0-9]+' || true)
        fi
    fi
    if [ -z "$rmse" ] || [ "$rmse" = "0" ]; then
        if [ -n "$last_hb" ]; then
            rmse=$(echo "$last_hb" | grep -oE 'rmse=[0-9.]+' | cut -d= -f2 || true)
        fi
    fi
    # 若仍无 fps, 用 frames / duration 估算 (duration 来自脚本全局 DUR)
    if [ -z "$fps" ] || [ "$fps" = "0" ]; then
        if [ -n "$frames" ] && [ "$frames" -gt 0 ]; then
            fps=$(awk -v f="$frames" -v d="$DUR" 'BEGIN{printf "%.1f", f/d}')
        fi
    fi
    echo "${frames:-0}|${fps:-0}|${sim_fps:-0}|${lat:-0}|${rmse:-0}"
}

cleanup() {
    echo "[bench] cleanup..."
    pkill -9 -f "ukf_pipeline_(5bus|9bus|39bus)" 2>/dev/null || true
    pkill -9 -x start_sim_nodes 2>/dev/null || true
    sleep 1
}

reset_system() {
    echo "[bench] reset FreeRTOS and SHM..."
    echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
    sleep 2
    echo start > /sys/class/remoteproc/remoteproc0/state
    sleep 3
    "$DIR/reset_shm"
    sleep 1
}

run_mode() {
    local MODE=$1
    local SUFFIX=$2
    local ENV_LD=$3
    local MODE_LOG_DIR="$LOG_DIR/${MODE}_${TIMESTAMP}"
    mkdir -p "$MODE_LOG_DIR"

    echo ""
    echo "----------------------------------------------"
    echo " Mode: $MODE"
    echo " Suffix: $SUFFIX"
    echo "----------------------------------------------"

    cleanup
    reset_system

    # prime
    echo "[bench] priming rpmsg endpoints..."
    "$DIR/start_sim_nodes" 1 > "$MODE_LOG_DIR/prime.log" 2>&1 &
    local PRIME_PID=$!
    sleep 6
    kill -9 "$PRIME_PID" 2>/dev/null || true
    sleep 1

    # start all sim nodes (speed=1)
    echo "[bench] starting all sim nodes (speed=1)..."
    "$DIR/start_sim_nodes" 1 > "$MODE_LOG_DIR/sim.log" 2>&1 &
    local SIM_PID=$!
    sleep 5

    # wait for all SHM ready
    local all_ready=0
    local wait_cnt=0
    while [ "$all_ready" -eq 0 ] && [ "$wait_cnt" -lt 20 ]; do
        all_ready=1
        for addr in "${SHM_CNT_ADDR[@]}"; do
            local raw=$(busybox devmem "$addr" 32 2>/dev/null || echo 0)
            local cnt=$((raw))
            if [ "$cnt" -eq 0 ]; then all_ready=0; break; fi
        done
        if [ "$all_ready" -eq 0 ]; then sleep 0.5; wait_cnt=$((wait_cnt+1)); fi
    done

    echo -n "[bench] SHM counts: "
    for i in 0 1 2; do
        local raw=$(busybox devmem "${SHM_CNT_ADDR[$i]}" 32 2>/dev/null || echo 0)
        echo -n "${NODES[$i]}=$((raw)) "
    done
    echo ""

    if [ "$all_ready" -eq 0 ]; then
        echo "[bench] ERROR: some SHM still empty, skipping $MODE"
        kill -9 "$SIM_PID" 2>/dev/null || true
        return
    fi

    # snapshot before
    local cpu0_before=$(cpu_snapshot 0)
    local cpu2_before=$(cpu_snapshot 2)
    local mem_before=$(free -m | awk 'NR==2{print $3}')
    local shm_cnt_before=()
    for i in 0 1 2; do
        local raw=$(busybox devmem "${SHM_CNT_ADDR[$i]}" 32 2>/dev/null || echo 0)
        shm_cnt_before[$i]=$((raw))
    done

    # start three UKF pipelines
    local UKF_PIDS=()
    for i in 0 1 2; do
        local node=${NODES[$i]}
        local cpu=${CPU_AFFINITY[$i]}
        local bin="ukf_pipeline_${node}${SUFFIX}"
        local log="$MODE_LOG_DIR/${node}.log"
        rm -f "$log"
        echo "[bench] starting $bin on CPU$cpu"
        if [ -n "$ENV_LD" ]; then
            env LD_LIBRARY_PATH="$ENV_LD" \
                taskset -c "$cpu" "$DIR/$bin" > /dev/null 2>>"$log" </dev/null &
        else
            taskset -c "$cpu" "$DIR/$bin" > /dev/null 2>>"$log" </dev/null &
        fi
        UKF_PIDS+=($!)
    done

    echo "[bench] running ${DUR}s..."
    sleep "$DUR"

    # snapshot after
    local cpu0_after=$(cpu_snapshot 0)
    local cpu2_after=$(cpu_snapshot 2)
    local mem_after=$(free -m | awk 'NR==2{print $3}')
    local shm_cnt_after=()
    for i in 0 1 2; do
        local raw=$(busybox devmem "${SHM_CNT_ADDR[$i]}" 32 2>/dev/null || echo 0)
        shm_cnt_after[$i]=$((raw))
    done

    # stop UKFs
    for pid in "${UKF_PIDS[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    sleep 1
    for pid in "${UKF_PIDS[@]}"; do kill -9 "$pid" 2>/dev/null || true; done
    kill -9 "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true

    # process RSS
    echo "[bench] process memory (RSS KB):"
    ps -o comm,rss -C "ukf_pipeline_5bus${SUFFIX}" -C "ukf_pipeline_9bus${SUFFIX}" -C "ukf_pipeline_39bus${SUFFIX}" 2>/dev/null || true

    # parse results
    local CPU0_PCT=$(cpu_usage "$cpu0_before" "$cpu0_after")
    local CPU2_PCT=$(cpu_usage "$cpu2_before" "$cpu2_after")

    echo ""
    echo "[$MODE] Results:"
    printf "| %-6s | %-7s | %-8s | %-10s | %-8s | %-8s | %-6s |\n" Node Frames FPS GenFPS "Lat(us)" RMSE CPU
    echo "|--------|---------|----------|------------|----------|----------|--------|"
    declare -a node_frames node_fps node_genfps node_lat node_rmse node_cpu
    for i in 0 1 2; do
        local node=${NODES[$i]}
        local cpu=${CPU_AFFINITY[$i]}
        local res=$(parse_log "$MODE_LOG_DIR/${node}.log" "$node")
        IFS='|' read -r frames fps sim_fps lat rmse <<< "$res"
        # 若 UKF 没输出 shm_fps, 用 SHM count 差值 / 时长估算生成频率
        if [ -z "$sim_fps" ] || [ "$sim_fps" = "0" ]; then
            local produced=$((shm_cnt_after[$i] - shm_cnt_before[$i]))
            sim_fps=$(awk -v p="$produced" -v d="$DUR" 'BEGIN{printf "%.1f", p/d}')
        fi
        local cpu_pct=$CPU0_PCT
        [ "$cpu" -eq 2 ] && cpu_pct=$CPU2_PCT
        node_frames[$i]=${frames:-0}
        node_fps[$i]=${fps:-0}
        node_genfps[$i]=${sim_fps:-0}
        node_lat[$i]=${lat:-0}
        node_rmse[$i]=${rmse:-0}
        node_cpu[$i]=${cpu_pct:-0}
        printf "| %-6s | %-7s | %-8s | %-10s | %-8s | %-8s | %-6s |\n" \
            "$node" "${frames}" "${fps}" "${sim_fps}" "${lat}" "${rmse}" "${cpu_pct}%"
    done
    echo "|--------|---------|----------|------------|----------|----------|--------|"
    echo "[$MODE] CPU0 usage: ${CPU0_PCT}%, CPU2 usage: ${CPU2_PCT}%, Mem used: ${mem_before}M -> ${mem_after}M"

    # save summary JSON
    cat > "$MODE_LOG_DIR/summary.json" <<EOF
{
  "timestamp": "$TIMESTAMP",
  "duration_s": $DUR,
  "mode": "$MODE",
  "cpu0_pct": $CPU0_PCT,
  "cpu2_pct": $CPU2_PCT,
  "mem_used_before_mb": ${mem_before:-0},
  "mem_used_after_mb": ${mem_after:-0},
  "nodes": {
    "5bus": {"frames": ${node_frames[0]:-0}, "fps": ${node_fps[0]:-0}, "gen_fps": ${node_genfps[0]:-0}, "lat_us": ${node_lat[0]:-0}, "rmse": ${node_rmse[0]:-0}, "cpu": ${node_cpu[0]:-0}},
    "9bus": {"frames": ${node_frames[1]:-0}, "fps": ${node_fps[1]:-0}, "gen_fps": ${node_genfps[1]:-0}, "lat_us": ${node_lat[1]:-0}, "rmse": ${node_rmse[1]:-0}, "cpu": ${node_cpu[1]:-0}},
    "39bus": {"frames": ${node_frames[2]:-0}, "fps": ${node_fps[2]:-0}, "gen_fps": ${node_genfps[2]:-0}, "lat_us": ${node_lat[2]:-0}, "rmse": ${node_rmse[2]:-0}, "cpu": ${node_cpu[2]:-0}}
  }
}
EOF
}

cleanup

FT_LD="/home/user/Phytium/fc_lib/BLAS-FT_v1.5.0/lib:/home/user/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib"

run_mode nonft "" ""
run_mode ft "_ft" "$FT_LD"

cleanup

echo ""
echo "[bench] all done. logs: $LOG_DIR"
