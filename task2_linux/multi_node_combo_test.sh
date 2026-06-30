#!/bin/bash
# multi_node_combo_test.sh — 多节点 UKF 组合压力测试
# 用法: sudo ./multi_node_combo_test.sh [E5] [E9] [E39] [DURATION]
#   E5   : 额外 5bus 实例数量 (默认 0)
#   E9   : 额外 9bus 实例数量 (默认 0)
#   E39  : 额外 39bus 实例数量 (默认 0)
#   DURATION : 测试时长, 秒 (默认 25)
#
# 说明:
#   - 始终保持 1×5bus + 1×9bus + 1×39bus 作为基础数据源节点
#   - 额外实例以 UKF_READONLY=1 只读模式运行, 模拟同类型多节点并发
#   - 5bus/9bus 绑定 CPU0, 39bus 绑定 CPU2
#   - 结果写入 /tmp/combo_${E5}_${E9}_${E39}_${DURATION}.log

set -e
DIR=/home/user/Phytium/task2_linux
cd "$DIR"

E5=${1:-0}
E9=${2:-0}
E39=${3:-0}
DURATION=${4:-25}

TAG="${E5}_${E9}_${E39}_${DURATION}"
LOG="/tmp/combo_${TAG}.log"
SUMMARY="${SUMMARY_FILE:-/tmp/combo_summary.txt}"

echo "=== 多节点组合测试: 基础(1+1+1) + 额外(${E5}×5bus + ${E9}×9bus + ${E39}×39bus), ${DURATION}s ===" | tee "$LOG"
echo "    总节点数: $((1+E5))×5bus + $((1+E9))×9bus + $((1+E39))×39bus" | tee -a "$LOG"

# 清理
sudo pkill -9 -f ukf_pipeline || true
sudo pkill -9 -x start_sim_nodes || true
sleep 1

# 热重载 FreeRTOS 固件，避免连续测试导致 SHM 写入停滞
echo "[*] 热重载 FreeRTOS 固件，清理状态..." | tee -a "$LOG"
sudo ./reload_firmware.sh /lib/firmware/openamp_core0.elf > /tmp/reload_${TAG}.log 2>&1 || {
    echo "[ERROR] 固件热重载失败，详情见 /tmp/reload_${TAG}.log" | tee -a "$LOG"
    exit 1
}
sleep 2

sudo ./reset_shm

# 预热 RPMsg
nohup sudo ./start_sim_nodes 1 > /tmp/sim_prime_combo.log 2>&1 &
PRIME_PID=$!
sleep 6
sudo kill -9 $PRIME_PID || true
sleep 1

# 基础 5bus/9bus/39bus (正常模式, 更新 SHM ri)
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_5bus  > /tmp/combo_5bus_${TAG}_base.csv  2> /tmp/combo_5bus_${TAG}_base.log  &
nohup sudo taskset -c 0 env UKF_DELTT=0.002 ./ukf_pipeline_9bus  > /tmp/combo_9bus_${TAG}_base.csv  2> /tmp/combo_9bus_${TAG}_base.log  &
nohup sudo taskset -c 2 env UKF_DELTT=0.004 ./ukf_pipeline_39bus > /tmp/combo_39bus_${TAG}_base.csv 2> /tmp/combo_39bus_${TAG}_base.log &

# 额外 5bus 只读实例
for i in $(seq 1 "$E5"); do
    nohup sudo taskset -c 0 env UKF_DELTT=0.002 UKF_READONLY=1 ./ukf_pipeline_5bus > /tmp/combo_5bus_${TAG}_${i}.csv 2> /tmp/combo_5bus_${TAG}_${i}.log &
done
# 额外 9bus 只读实例
for i in $(seq 1 "$E9"); do
    nohup sudo taskset -c 0 env UKF_DELTT=0.002 UKF_READONLY=1 ./ukf_pipeline_9bus > /tmp/combo_9bus_${TAG}_${i}.csv 2> /tmp/combo_9bus_${TAG}_${i}.log &
done
# 额外 39bus 只读实例
for i in $(seq 1 "$E39"); do
    nohup sudo taskset -c 2 env UKF_DELTT=0.004 UKF_READONLY=1 ./ukf_pipeline_39bus > /tmp/combo_39bus_${TAG}_${i}.csv 2> /tmp/combo_39bus_${TAG}_${i}.log &
done

# 启动 FreeRTOS 数据生成
nohup sudo ./start_sim_nodes 1 > /tmp/sim_combo_${TAG}.log 2>&1 &

echo "[*] 运行 ${DURATION}s..." | tee -a "$LOG"
sleep "$DURATION"

# 采样 CPU
echo "" | tee -a "$LOG"
echo "=== CPU 占用 ===" | tee -a "$LOG"
idle0_b=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_b=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_b=$(awk '/^cpu2 /{print $5}' /proc/stat)
sleep 1
idle0_a=$(awk '/^cpu0 /{print $5}' /proc/stat)
idle1_a=$(awk '/^cpu1 /{print $5}' /proc/stat)
idle2_a=$(awk '/^cpu2 /{print $5}' /proc/stat)

CPU0=$(awk -v i0b="$idle0_b" -v i0a="$idle0_a" 'BEGIN{printf "%.1f", 100.0*(1-(i0a-i0b)/((i0a-i0b)+1000))}')
CPU1=$(awk -v i1b="$idle1_b" -v i1a="$idle1_a" 'BEGIN{printf "%.1f", 100.0*(1-(i1a-i1b)/((i1a-i1b)+1000))}')
CPU2=$(awk -v i2b="$idle2_b" -v i2a="$idle2_a" 'BEGIN{printf "%.1f", 100.0*(1-(i2a-i2b)/((i2a-i2b)+1000))}')
echo "CPU0 total: ${CPU0}%" | tee -a "$LOG"
echo "CPU1 total: ${CPU1}%" | tee -a "$LOG"
echo "CPU2 total: ${CPU2}%" | tee -a "$LOG"

echo "" | tee -a "$LOG"
echo "=== 进程 CPU 占用 ===" | tee -a "$LOG"
ps -eo pid,psr,comm,pcpu,args --sort=-pcpu | grep "ukf_pipeline" | grep -v grep | tee -a "$LOG"

# 提取 RMSE / 帧数 / 延迟
echo "" | tee -a "$LOG"
echo "=== 各实例指标 ===" | tee -a "$LOG"

extract_last() {
    local log=$1
    [ -f "$log" ] || return
    tail -1 "$log" 2>/dev/null
}

extract_rmse() {
    local csv=$1
    [ -f "$csv" ] || return
    local lines=$(tail -n +2 "$csv" 2>/dev/null | wc -l)
    [ "$lines" -gt 0 ] || return
    local last=$(tail -1 "$csv" 2>/dev/null | awk -F',' '{print $NF}')
    local avg=$(tail -n +2 "$csv" 2>/dev/null | awk -F',' '{sum+=$NF; n++} END{if(n>0) printf "%.6f", sum/n}')
    echo "frames=$lines avg_rmse=$avg final_rmse=$last"
}

# 基础实例
for t in 5bus 9bus 39bus; do
    echo "--- ${t} base ---" | tee -a "$LOG"
    extract_rmse "/tmp/combo_${t}_${TAG}_base.csv" | tee -a "$LOG"
    extract_last "/tmp/combo_${t}_${TAG}_base.log" | tee -a "$LOG"
done
# 额外实例
for i in $(seq 1 "$E5"); do
    echo "--- 5bus extra #$i ---" | tee -a "$LOG"
    extract_rmse "/tmp/combo_5bus_${TAG}_${i}.csv" | tee -a "$LOG"
    extract_last "/tmp/combo_5bus_${TAG}_${i}.log" | tee -a "$LOG"
done
for i in $(seq 1 "$E9"); do
    echo "--- 9bus extra #$i ---" | tee -a "$LOG"
    extract_rmse "/tmp/combo_9bus_${TAG}_${i}.csv" | tee -a "$LOG"
    extract_last "/tmp/combo_9bus_${TAG}_${i}.log" | tee -a "$LOG"
done
for i in $(seq 1 "$E39"); do
    echo "--- 39bus extra #$i ---" | tee -a "$LOG"
    extract_rmse "/tmp/combo_39bus_${TAG}_${i}.csv" | tee -a "$LOG"
    extract_last "/tmp/combo_39bus_${TAG}_${i}.log" | tee -a "$LOG"
done

# 汇总到 summary
{
echo "${E5} ${E9} ${E39} ${DURATION} ${CPU0} ${CPU1} ${CPU2}"
} >> "$SUMMARY"

# 清理
sudo pkill -9 -f ukf_pipeline || true
sudo pkill -9 -x start_sim_nodes || true

echo "" | tee -a "$LOG"
echo "=== 测试完成: ${TAG} ===" | tee -a "$LOG"
