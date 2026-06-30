#!/bin/bash
# ============================================================
# bench_39bus_compare.sh — 39bus UKF FT vs 非FT 性能对比测试
# ============================================================
# 在 Phytium 开发板上运行, 测量 39bus 状态估计的 latency 和 fps。
# 用法: ./bench_39bus_compare.sh [时长秒数, 默认=30]
# ============================================================

DUR=${1:-30}
DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="$DIR/logs_bench_39bus"
mkdir -p "$LOG_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

echo "=============================================="
echo " 39bus UKF Performance Benchmark"
echo " Duration: ${DUR}s per mode"
echo " Timestamp: $TIMESTAMP"
echo "=============================================="

# 清理函数
cleanup() {
    echo "[bench] cleanup..."
    pkill -9 -f ukf_pipeline 2>/dev/null || true
    pkill -9 -f launch_ukf_multi.py 2>/dev/null || true
    pkill -9 -f start_sim_nodes 2>/dev/null || true
    sleep 1
}

# 重置 FreeRTOS + SHM
reset_system() {
    echo "[bench] reset FreeRTOS and SHM..."
    echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
    sleep 2
    echo start > /sys/class/remoteproc/remoteproc0/state
    sleep 3
    "$DIR/reset_shm"
    sleep 1
}

# 解析日志: 从最后一条心跳提取 frames/rmse/lat, 并估算 wall-time fps (frames/DUR)
parse_log() {
    local log=$1
    local mode=$2
    local last_hb=$(grep -E '\[ukf-39bus\] t=' "$log" | tail -1 || true)
    local final=$(grep -E '\[ukf-39bus\] done:' "$log" | tail -1 || true)

    local frames=0 fps=0 lat=0 rmse=0 t=0 sim_fps=0
    if [ -n "$last_hb" ]; then
        frames=$(echo "$last_hb" | grep -oE 'frames=[0-9]+' | cut -d= -f2 || true)
        rmse=$(echo "$last_hb" | grep -oE 'rmse=[0-9.]+' | cut -d= -f2 || true)
        lat=$(echo "$last_hb" | grep -oE 'lat=[0-9]+us' | grep -oE '[0-9]+' || true)
        t=$(echo "$last_hb" | grep -oE 't=[0-9.]+s' | grep -oE '[0-9.]+' || true)
    fi
    if [ -n "$final" ]; then
        fps=$(echo "$final" | grep -oE '\bfps=[0-9.]+' | cut -d= -f2 || true)
        lat=$(echo "$final" | grep -oE 'avg_lat=[0-9.]+us' | grep -oE '[0-9.]+' | cut -d. -f1 || true)
        sim_fps=$(echo "$final" | grep -oE '\bshm_fps=[0-9.]+' | cut -d= -f2 || true)
    else
        # wall-time fps: 实际每秒处理的帧数 (由测试时长 DUR 决定)
        if [ -n "$frames" ] && [ "$DUR" -gt 0 ]; then
            fps=$(awk -v f="$frames" -v d="$DUR" 'BEGIN{printf "%.1f", f/d}')
        fi
        # simulation-time fps (frames / simulation t), 仅供参考
        if [ -n "$last_hb" ] && [ -n "$t" ]; then
            sim_fps=$(awk -v f="$frames" -v t="$t" 'BEGIN{if(t>0) printf "%.1f", f/t; else print 0}')
        fi
    fi

    echo "$mode|${frames:-0}|${fps:-0}|${rmse:-0}|${lat:-0}|${sim_fps:-0}" > "$LOG_DIR/result_${mode}_${TIMESTAMP}.txt"
}

# 运行一次测试
run_mode() {
    local MODE=$1
    local BIN=$2
    local ENV_LD=$3
    local LOG="$LOG_DIR/${MODE}_${TIMESTAMP}.log"

    echo ""
    echo "----------------------------------------------"
    echo " Mode: $MODE"
    echo " Binary: $BIN"
    echo " Log: $LOG"
    echo "----------------------------------------------"

    cleanup
    reset_system

    # 预绑定 (prime): 第一次创建 RPMsg endpoint 后, 39bus/9bus 端点
    # 可能未立即与 FreeRTOS 绑定。杀掉重新创建可稳定建立绑定。
    echo "[bench] priming rpmsg endpoints..."
    "$DIR/start_sim_nodes" 1 > "$LOG_DIR/sim_prime_${MODE}_${TIMESTAMP}.log" 2>&1 &
    local PRIME_PID=$!
    sleep 6
    kill -9 "$PRIME_PID" 2>/dev/null || true
    sleep 1

    # 启动 FreeRTOS 仿真 (所有节点, 后台)
    echo "[bench] starting sim nodes (speed=1)..."
    "$DIR/start_sim_nodes" 1 > "$LOG_DIR/sim_${MODE}_${TIMESTAMP}.log" 2>&1 &
    local SIM_PID=$!
    sleep 5

    # 确认 SHM 已有数据, 最多等 10s
    local cnt=0
    local wait_cnt=0
    while [ "$cnt" -eq 0 ] && [ "$wait_cnt" -lt 20 ]; do
        local raw=$(busybox devmem 0xC8140008 32 2>/dev/null || echo 0)
        cnt=$((raw))
        [ "$cnt" -eq 0 ] && { sleep 0.5; wait_cnt=$((wait_cnt+1)); }
    done
    echo "[bench] 39bus SHM count before UKF: $cnt"

    if [ "$cnt" -eq 0 ]; then
        echo "[bench] ERROR: 39bus SHM still empty, skipping $MODE"
        kill -9 "$SIM_PID" 2>/dev/null || true
        echo "$MODE|0|0|0|0|0" > "$LOG_DIR/result_${MODE}_${TIMESTAMP}.txt"
        return
    fi

    # 启动 39bus UKF, 绑定 CPU2
    echo "[bench] starting $BIN for ${DUR}s..."
    if [ -n "$ENV_LD" ]; then
        env LD_LIBRARY_PATH="$ENV_LD" taskset -c 2 "$DIR/$BIN" > /dev/null 2>>"$LOG" &
    else
        taskset -c 2 "$DIR/$BIN" > /dev/null 2>>"$LOG" &
    fi
    local UKF_PID=$!

    # 等待测试时长
    sleep "$DUR"

    # 停止 UKF
    kill -TERM "$UKF_PID" 2>/dev/null || true
    sleep 1
    kill -9 "$UKF_PID" 2>/dev/null || true

    # 停止仿真
    kill -9 "$SIM_PID" 2>/dev/null || true

    # 解析结果
    parse_log "$LOG" "$MODE"
}

# 主流程
cleanup

# FT 库路径
FT_LD="/home/user/Phytium/fc_lib/BLAS-FT_v1.5.0/lib:/home/user/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib"

# 先跑非FT, 再跑FT
run_mode nonft ukf_pipeline_39bus ""
run_mode ft ukf_pipeline_39bus_ft "$FT_LD"

cleanup

RESULT_NONFT=$(cat "$LOG_DIR/result_nonft_${TIMESTAMP}.txt" 2>/dev/null || echo "nonft|0|0|0|0|0")
RESULT_FT=$(cat "$LOG_DIR/result_ft_${TIMESTAMP}.txt" 2>/dev/null || echo "ft|0|0|0|0|0")

IFS='|' read -r M_NF F_NF FPS_NF RMSE_NF LAT_NF SIMFPS_NF <<< "$RESULT_NONFT"
IFS='|' read -r M_FT F_FT FPS_FT RMSE_FT LAT_FT SIMFPS_FT <<< "$RESULT_FT"

echo ""
echo "=============================================="
echo " Benchmark Results (wall-time fps)"
echo "=============================================="
printf "| %-8s | %-7s | %-8s | %-10s | %-10s | %-10s |\n" "Mode" "Frames" "FPS" "RMSE" "Lat(us)" "SimFPS"
echo "|----------|---------|----------|------------|------------|------------|"
printf "| %-8s | %-7s | %-8s | %-10s | %-10s | %-10s |\n" "$M_NF" "$F_NF" "$FPS_NF" "$RMSE_NF" "$LAT_NF" "$SIMFPS_NF"
printf "| %-8s | %-7s | %-8s | %-10s | %-10s | %-10s |\n" "$M_FT" "$F_FT" "$FPS_FT" "$RMSE_FT" "$LAT_FT" "$SIMFPS_FT"
echo "=============================================="

# 保存 JSON 摘要
cat > "$LOG_DIR/summary_${TIMESTAMP}.json" <<EOF
{
  "timestamp": "$TIMESTAMP",
  "duration_s": $DUR,
  "nonft": {
    "frames": ${F_NF:-0},
    "fps": ${FPS_NF:-0},
    "rmse": ${RMSE_NF:-0},
    "latency_us": ${LAT_NF:-0}
  },
  "ft": {
    "frames": ${F_FT:-0},
    "fps": ${FPS_FT:-0},
    "rmse": ${RMSE_FT:-0},
    "latency_us": ${LAT_FT:-0}
  }
}
EOF

echo "[bench] summary saved to $LOG_DIR/summary_${TIMESTAMP}.json"
