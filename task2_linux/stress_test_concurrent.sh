#!/bin/bash
# stress_test_concurrent.sh — 多节点 UKF 并发压力测试
# 用法: sudo ./stress_test_concurrent.sh [N5] [N9] [N39] [DURATION]
#   N5    : CPU0 上额外启动的 5bus 只读实例数 (默认 0)
#   N9    : CPU0 上额外启动的 9bus 只读实例数 (默认 0)
#   N39   : CPU2 上额外启动的 39bus 只读实例数 (默认 0)
#   DURATION : 测试时长, 秒 (默认 20)
#
# 说明:
#   - 原始 5bus/9bus/39bus 以正常模式运行 (更新 SHM ri)
#   - 额外实例以 UKF_READONLY=1 模式运行, 不更新 SHM ri,
#     仅用于对 CPU 施加 UKF 计算压力, 模拟更多节点并发
#   - 测试结果记录到 logs_stress/<timestamp>/

set -e

N5=${1:-0}
N9=${2:-0}
N39=${3:-0}
DURATION=${4:-20}

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_DIR="$DIR/logs_stress/stress_${N5}_${N9}_${N39}_${DURATION}s_${TIMESTAMP}"
mkdir -p "$LOG_DIR"

echo "=== 并发压力测试配置 ==="
echo "额外 5bus  实例数: $N5 (CPU0)"
echo "额外 9bus  实例数: $N9 (CPU0)"
echo "额外 39bus 实例数: $N39 (CPU2)"
echo "测试时长: ${DURATION}s"
echo "日志目录: $LOG_DIR"
echo ""

# 清理函数
cleanup() {
    echo "[*] 清理进程..."
    sudo pkill -9 -f ukf_pipeline || true
    sudo pkill -9 -x start_sim_nodes || true
    sleep 1
}
trap cleanup EXIT

# 1. 停止旧进程并清理桌面
cleanup
sudo pkill -9 -f firefox || true
sudo pkill -9 -f Xtigervnc || true
sudo pkill -9 -f lightdm || true
sudo pkill -9 -f Xorg || true
sleep 1

# 2. 重启 FreeRTOS
sudo bash -c 'echo stop > /sys/class/remoteproc/remoteproc0/state; sleep 2; echo start > /sys/class/remoteproc/remoteproc0/state; sleep 5'

# 3. 重置 SHM
sudo ./reset_shm

# 4. prime RPMsg
nohup sudo ./start_sim_nodes 1 > "$LOG_DIR/sim_prime.log" 2>&1 &
PRIME_PID=$!
sleep 6
sudo kill -9 $PRIME_PID || true
sleep 1

# 5. 启动原始三个 UKF (正常模式)
mkdir -p /tmp/ukf_logs
nohup sudo taskset -c 0 ./ukf_pipeline_5bus > "$LOG_DIR/ukf_5bus_0.csv" 2> "$LOG_DIR/ukf_5bus_0.log" &
nohup sudo taskset -c 0 ./ukf_pipeline_9bus > "$LOG_DIR/ukf_9bus_0.csv" 2> "$LOG_DIR/ukf_9bus_0.log" &
nohup sudo taskset -c 2 ./ukf_pipeline_39bus > "$LOG_DIR/ukf_39bus_0.csv" 2> "$LOG_DIR/ukf_39bus_0.log" &

# 6. 启动额外只读实例
EXTRA_PIDS=""
for i in $(seq 1 "$N5"); do
    nohup sudo taskset -c 0 env UKF_READONLY=1 ./ukf_pipeline_5bus > "$LOG_DIR/ukf_5bus_${i}.csv" 2> "$LOG_DIR/ukf_5bus_${i}.log" &
    EXTRA_PIDS="$EXTRA_PIDS $!"
done
for i in $(seq 1 "$N9"); do
    nohup sudo taskset -c 0 env UKF_READONLY=1 ./ukf_pipeline_9bus > "$LOG_DIR/ukf_9bus_${i}.csv" 2> "$LOG_DIR/ukf_9bus_${i}.log" &
    EXTRA_PIDS="$EXTRA_PIDS $!"
done
for i in $(seq 1 "$N39"); do
    nohup sudo taskset -c 2 env UKF_READONLY=1 ./ukf_pipeline_39bus > "$LOG_DIR/ukf_39bus_${i}.csv" 2> "$LOG_DIR/ukf_39bus_${i}.log" &
    EXTRA_PIDS="$EXTRA_PIDS $!"
done

# 7. 启动 FreeRTOS 数据生成
nohup sudo ./start_sim_nodes 1 > "$LOG_DIR/sim.log" 2>&1 &
START_SIM_PID=$!

echo "[*] 所有进程已启动, 等待 ${DURATION}s..."
sleep "$DURATION"

# 8. 采样最终状态
echo ""
echo "========== 测试结束采样 =========="

# 各进程 CPU 占用
ps -eo pid,psr,comm,pcpu,args --sort=-pcpu | grep "ukf_pipeline" | grep -v grep > "$LOG_DIR/final_ps.txt" || true

echo "--- 各 UKF 进程 CPU 占用 ---"
cat "$LOG_DIR/final_ps.txt"

# CPU 总占用
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

# 从日志提取关键指标
echo ""
echo "--- 各实例最后心跳 ---"
for log in "$LOG_DIR"/ukf_*.log; do
    [ -f "$log" ] || continue
    name=$(basename "$log" .log)
    tail -n 2 "$log" | sed "s/^/[$name] /" || true
done

# 生成汇总
echo ""
echo "========== 汇总 =========="
{
echo "timestamp: $TIMESTAMP"
echo "duration_s: $DURATION"
echo "extra_nodes: {5bus: $N5, 9bus: $N9, 39bus: $N39}"
echo "cpu_usage:"
cat "$LOG_DIR/final_cpu.txt"
echo "processes:"
cat "$LOG_DIR/final_ps.txt"
} > "$LOG_DIR/summary.txt"

echo "结果已保存到: $LOG_DIR"
