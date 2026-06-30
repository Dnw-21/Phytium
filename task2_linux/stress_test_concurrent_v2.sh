#!/bin/bash
# stress_test_concurrent_v2.sh — 支持 9bus 绑定 CPU2 的变体
# 用法: sudo ./stress_test_concurrent_v2.sh [N5] [N9_CPU0] [N9_CPU2] [N39_CPU2] [DURATION]
#   N5       : CPU0 上额外 5bus 只读实例数
#   N9_CPU0  : CPU0 上额外 9bus 只读实例数
#   N9_CPU2  : CPU2 上额外 9bus 只读实例数 (与 39bus 共享 CPU2)
#   N39_CPU2 : CPU2 上额外 39bus 只读实例数
#   DURATION : 测试时长, 秒

set -e

N5=${1:-0}
N9_CPU0=${2:-0}
N9_CPU2=${3:-0}
N39_CPU2=${4:-0}
DURATION=${5:-20}

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_DIR="$DIR/logs_stress/stress_v2_${N5}_${N9_CPU0}_${N9_CPU2}_${N39_CPU2}_${DURATION}s_${TIMESTAMP}"
mkdir -p "$LOG_DIR"

echo "=== 并发压力测试 v2 配置 ==="
echo "额外 5bus  (CPU0): $N5"
echo "额外 9bus  (CPU0): $N9_CPU0"
echo "额外 9bus  (CPU2): $N9_CPU2"
echo "额外 39bus (CPU2): $N39_CPU2"
echo "测试时长: ${DURATION}s"
echo "日志目录: $LOG_DIR"
echo ""

cleanup() {
    echo "[*] 清理进程..."
    sudo pkill -9 -f ukf_pipeline || true
    sudo pkill -9 -x start_sim_nodes || true
    sleep 1
}
trap cleanup EXIT

cleanup
sudo pkill -9 -f firefox || true
sudo pkill -9 -f Xtigervnc || true
sleep 1

sudo bash -c 'echo stop > /sys/class/remoteproc/remoteproc0/state; sleep 2; echo start > /sys/class/remoteproc/remoteproc0/state; sleep 5'
sudo ./reset_shm

nohup sudo ./start_sim_nodes 1 > "$LOG_DIR/sim_prime.log" 2>&1 &
PRIME_PID=$!
sleep 6
sudo kill -9 $PRIME_PID || true
sleep 1

# 原始节点
nohup sudo taskset -c 0 ./ukf_pipeline_5bus > "$LOG_DIR/ukf_5bus_0.csv" 2> "$LOG_DIR/ukf_5bus_0.log" &
nohup sudo taskset -c 2 ./ukf_pipeline_9bus > "$LOG_DIR/ukf_9bus_0.csv" 2> "$LOG_DIR/ukf_9bus_0.log" &
nohup sudo taskset -c 2 ./ukf_pipeline_39bus > "$LOG_DIR/ukf_39bus_0.csv" 2> "$LOG_DIR/ukf_39bus_0.log" &

# 额外实例
for i in $(seq 1 "$N5"); do
    nohup sudo taskset -c 0 env UKF_READONLY=1 ./ukf_pipeline_5bus > "$LOG_DIR/ukf_5bus_${i}.csv" 2> "$LOG_DIR/ukf_5bus_${i}.log" &
done
for i in $(seq 1 "$N9_CPU0"); do
    nohup sudo taskset -c 0 env UKF_READONLY=1 ./ukf_pipeline_9bus > "$LOG_DIR/ukf_9bus_cpu0_${i}.csv" 2> "$LOG_DIR/ukf_9bus_cpu0_${i}.log" &
done
for i in $(seq 1 "$N9_CPU2"); do
    nohup sudo taskset -c 2 env UKF_READONLY=1 ./ukf_pipeline_9bus > "$LOG_DIR/ukf_9bus_cpu2_${i}.csv" 2> "$LOG_DIR/ukf_9bus_cpu2_${i}.log" &
done
for i in $(seq 1 "$N39_CPU2"); do
    nohup sudo taskset -c 2 env UKF_READONLY=1 ./ukf_pipeline_39bus > "$LOG_DIR/ukf_39bus_${i}.csv" 2> "$LOG_DIR/ukf_39bus_${i}.log" &
done

nohup sudo ./start_sim_nodes 1 > "$LOG_DIR/sim.log" 2>&1 &

echo "[*] 所有进程已启动, 等待 ${DURATION}s..."
sleep "$DURATION"

echo ""
echo "========== 测试结束采样 =========="
ps -eo pid,psr,comm,pcpu,args --sort=-pcpu | grep "ukf_pipeline" | grep -v grep > "$LOG_DIR/final_ps.txt" || true
cat "$LOG_DIR/final_ps.txt"

idle0_b=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_b=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_b=$(awk '/^cpu2 /{print $5}' /proc/stat)
sleep 1
idle0_a=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_a=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_a=$(awk '/^cpu2 /{print $5}' /proc/stat)

awk -v i0b="$idle0_b" -v i0a="$idle0_a" -v i1b="$idle1_b" -v i1a="$idle1_a" -v i2b="$idle2_b" -v i2a="$idle2_a" 'BEGIN{
    printf "CPU0 total: %.1f%%\n", 100.0*(1-(i0a-i0b)/((i0a-i0b)+1000))
    printf "CPU1 total: %.1f%%\n", 100.0*(1-(i1a-i1b)/((i1a-i1b)+1000))
    printf "CPU2 total: %.1f%%\n", 100.0*(1-(i2a-i2b)/((i2a-i2b)+1000))
}' | tee "$LOG_DIR/final_cpu.txt"

echo ""
echo "--- 各实例最后心跳 ---"
for log in "$LOG_DIR"/ukf_*.log; do
    [ -f "$log" ] || continue
    name=$(basename "$log" .log)
    tail -n 2 "$log" | sed "s/^/[$name] /" || true
done

{
echo "timestamp: $TIMESTAMP"
echo "duration_s: $DURATION"
echo "extra_nodes: {5bus_cpu0: $N5, 9bus_cpu0: $N9_CPU0, 9bus_cpu2: $N9_CPU2, 39bus_cpu2: $N39_CPU2}"
echo "cpu_usage:"
cat "$LOG_DIR/final_cpu.txt"
echo "processes:"
cat "$LOG_DIR/final_ps.txt"
} > "$LOG_DIR/summary.txt"

echo ""
echo "结果已保存到: $LOG_DIR"
