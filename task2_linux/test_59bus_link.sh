#!/bin/bash
# test_59bus_link.sh — 快速验证 5bus/9bus 链路是否通
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$DIR"
pkill -9 -f start_sim_nodes 2>/dev/null || true
pkill -9 -f ukf_pipeline 2>/dev/null || true

echo "[test] reloading FreeRTOS..."
echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null || true
sleep 2
echo start > /sys/class/remoteproc/remoteproc0/state
sleep 5

echo "[test] reset SHM..."
./reset_shm

echo "[test] prime rpmsg..."
./start_sim_nodes 1 > /tmp/prime59.log 2>&1 &
PRIME_PID=$!
sleep 6
kill -9 "$PRIME_PID" 2>/dev/null || true
wait "$PRIME_PID" 2>/dev/null || true
sleep 1

echo "[test] start sim nodes..."
./start_sim_nodes 1 > /tmp/sim59.log 2>&1 &
SIM_PID=$!
sleep 8

echo "[test] SHM counts:"
echo -n "  5bus  = "; busybox devmem 0xC8100008 32
echo -n "  39bus = "; busybox devmem 0xC8140008 32
echo -n "  9bus  = "; busybox devmem 0xC81C0008 32

kill -9 "$SIM_PID" 2>/dev/null || true
wait "$SIM_PID" 2>/dev/null || true

echo "[test] done"
