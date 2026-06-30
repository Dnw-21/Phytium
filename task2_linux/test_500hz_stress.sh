#!/bin/bash
# test_500hz_stress.sh — 验证 5bus/9bus 降到 500Hz 后的精度与 CPU 并发能力
# 运行环境: 开发板 Linux 侧 (/home/user/Phytium/task2_linux)
set -e
DIR=/home/user/Phytium/task2_linux
cd "$DIR"

DURATION=${1:-30}
EXTRA_5BUS=${2:-2}
EXTRA_9BUS=${3:-2}

echo "=== 500Hz 降频压力测试 ==="
echo "测试时长: ${DURATION}s"
echo "额外 5bus 只读实例: $EXTRA_5BUS"
echo "额外 9bus 只读实例: $EXTRA_9BUS"
echo ""

# 清理
sudo pkill -9 -f ukf_pipeline || true
sudo pkill -9 -x start_sim_nodes || true
sleep 1
sudo ./reset_shm

# 预热 RPMsg
nohup sudo ./start_sim_nodes 1 > /tmp/sim_prime_500hz.log 2>&1 &
PRIME_PID=$!
sleep 6
sudo kill -9 $PRIME_PID || true
sleep 1

# 原始三节点 (5/9bus 500Hz, 39bus 保持 2000Hz)
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_5bus  > /tmp/ukf5_500hz.csv  2> /tmp/ukf5_500hz.log  &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_9bus  > /tmp/ukf9_500hz.csv  2> /tmp/ukf9_500hz.log  &
nohup sudo taskset -c 2 env UKF_DELTT=0.0005 ./ukf_pipeline_39bus > /tmp/ukf39_500hz.csv 2> /tmp/ukf39_500hz.log &

# 额外只读实例 (500Hz)
for i in $(seq 1 "$EXTRA_5BUS"); do
    nohup sudo taskset -c 0 env UKF_DELTT=0.002 UKF_READONLY=1 ./ukf_pipeline_5bus > /tmp/ukf5_500hz_${i}.csv 2> /tmp/ukf5_500hz_${i}.log &
done
for i in $(seq 1 "$EXTRA_9BUS"); do
    nohup sudo taskset -c 0 env UKF_DELTT=0.002 UKF_READONLY=1 ./ukf_pipeline_9bus > /tmp/ukf9_500hz_${i}.csv 2> /tmp/ukf9_500hz_${i}.log &
done

# 启动 FreeRTOS 数据生成
nohup sudo ./start_sim_nodes 1 > /tmp/sim_500hz.log 2>&1 &

echo "[*] 运行 ${DURATION}s..."
sleep "$DURATION"

echo "=== CPU 占用 ==="
idle0_b=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_b=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_b=$(awk '/^cpu2 /{print $5}' /proc/stat)
sleep 1
idle0_a=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_a=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_a=$(awk '/^cpu2 /{print $5}' /proc/stat)

awk -v i0b="$idle0_b" -v i0a="$idle0_a" \
    -v i1b="$idle1_b" -v i1a="$idle1_a" \
    -v i2b="$idle2_b" -v i2a="$idle2_a" 'BEGIN{
    printf "CPU0 total: %.1f%%\n", 100.0*(1-(i0a-i0b)/((i0a-i0b)+1000))
    printf "CPU1 total: %.1f%%\n", 100.0*(1-(i1a-i1b)/((i1a-i1b)+1000))
    printf "CPU2 total: %.1f%%\n", 100.0*(1-(i2a-i2b)/((i2a-i2b)+1000))
}'

echo ""
echo "=== 进程 CPU 占用 ==="
ps -eo pid,psr,comm,pcpu,args --sort=-pcpu | grep "ukf_pipeline" | grep -v grep

echo ""
echo "=== 最后心跳 ==="
for f in /tmp/ukf5_500hz.log /tmp/ukf9_500hz.log /tmp/ukf39_500hz.log; do
    echo "--- $f ---"
    tail -2 "$f" || true
done

echo ""
echo "=== RMSE (CSV 最后一行) ==="
for f in /tmp/ukf5_500hz.csv /tmp/ukf9_500hz.csv /tmp/ukf39_500hz.csv; do
    echo "--- $f ---"
    tail -1 "$f" | awk -F',' '{print "cols="NF", last="$NF}' || true
done

# 清理
sudo pkill -9 -f ukf_pipeline || true
sudo pkill -9 -x start_sim_nodes || true
