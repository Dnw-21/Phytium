#!/bin/bash
# test_9bus_backpressure.sh — 验证 9bus 背压/消费配合
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

DUR=${1:-12}
BIN=${2:-./ukf_pipeline_9bus}

cleanup() {
    pkill -9 ukf_pipeline_9bus 2>/dev/null || true
    pkill -9 start_sim_nodes 2>/dev/null || true
}
cleanup

# FreeRTOS 已经在运行, 只需要重置 SHM 并重新绑定
echo "[test] reset SHM..."
./reset_shm

echo "[test] prime rpmsg..."
./start_sim_nodes 1 > /tmp/sim9_bp.log 2>&1 &
PRIME_PID=$!
sleep 6
kill -9 "$PRIME_PID" 2>/dev/null || true
wait "$PRIME_PID" 2>/dev/null || true
sleep 1

echo "[test] start UKF first, then sim nodes..."
rm -f /tmp/ukf9_bp.log
LOG="/tmp/ukf9_bp.log"
taskset -c 2 "$BIN" > /dev/null 2>>"$LOG" &
UKF_PID=$!
sleep 1

./start_sim_nodes 1 >> /tmp/sim9_bp.log 2>&1 &
SIM_PID=$!

sleep "$DUR"

echo "[test] stop UKF..."
kill -TERM "$UKF_PID" 2>/dev/null || true
sleep 1
kill -9 "$UKF_PID" 2>/dev/null || true
kill -9 "$SIM_PID" 2>/dev/null || true
wait "$SIM_PID" 2>/dev/null || true

echo "[test] UKF log tail:"
tail -30 "$LOG"

cleanup
echo "[test] done"
