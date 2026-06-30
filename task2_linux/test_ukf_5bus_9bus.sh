#!/bin/bash
# test_ukf_5bus_9bus.sh — 验证 5bus/9bus Linux 状态估计链路
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

NODE=$1
DUR=${2:-10}
BIN=$3

if [ -z "$NODE" ] || [ -z "$BIN" ]; then
    echo "Usage: $0 <5bus|9bus> <duration> <binary>"
    echo "Example: $0 5bus 10 ./ukf_pipeline_5bus"
    exit 1
fi

SHM_CNT=0xC8100008
[ "$NODE" = "9bus" ] && SHM_CNT=0xC81C0008

cleanup() {
    pkill -9 -f "ukf_pipeline_${NODE}" 2>/dev/null || true
    pkill -9 -f start_sim_nodes 2>/dev/null || true
}
cleanup

echo "[test] reloading FreeRTOS..."
echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
sleep 2
echo start > /sys/class/remoteproc/remoteproc0/state
sleep 5

echo "[test] reset SHM..."
./reset_shm

echo "[test] prime rpmsg..."
./start_sim_nodes 1 > /tmp/prime_u.log 2>&1 &
PRIME_PID=$!
sleep 6
kill -9 "$PRIME_PID" 2>/dev/null || true
wait "$PRIME_PID" 2>/dev/null || true
sleep 1

echo "[test] start sim nodes..."
./start_sim_nodes 1 > /tmp/sim_u.log 2>&1 &
SIM_PID=$!
sleep 8

echo -n "[test] $NODE SHM count = "; busybox devmem "$SHM_CNT" 32

echo "[test] run $BIN for ${DUR}s..."
LOG="/tmp/${NODE}_ukf_test.log"
rm -f "$LOG"
taskset -c 2 "$BIN" > /dev/null 2>>"$LOG" &
UKF_PID=$!
sleep "$DUR"
kill -TERM "$UKF_PID" 2>/dev/null || true
sleep 1
kill -9 "$UKF_PID" 2>/dev/null || true

echo "[test] $NODE UKF log tail:"
tail -20 "$LOG"

cleanup
echo "[test] done"
