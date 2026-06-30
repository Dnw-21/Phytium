#!/bin/bash
# ============================================================
# start_all.sh — 三节点 UKF 全链路一键启动脚本 (v5)
# ============================================================
# CPU 分配方案:
#   Core 0 (A55): 5bus + 9bus UKF + Python 主脚本 + SSH/网络
#   Core 1 (A76): FreeRTOS 独占 (3 节点 RK4 仿真引擎)
#   Core 2 (A76): 39bus UKF 独占 (VIP 计算特区)
#
# 用法: ./start_all.sh [speed]
#       speed 默认=1 (控制 5bus, 39bus/9bus 全速)
# ============================================================

set -o pipefail
SPEED=${1:-1}
DIR="$(cd "$(dirname "$0")" && pwd)"
USER_PASS="${USER_PASS:-user}"
LOG_DIR="$DIR/logs_v5"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
mkdir -p "$LOG_DIR"

LAUNCH_LOG="$LOG_DIR/launch_${TIMESTAMP}.log"
SIM_LOG="$LOG_DIR/sim_${TIMESTAMP}.log"
DIAG_LOG="$LOG_DIR/diag_${TIMESTAMP}.log"

log()  { echo "[$(date +%H:%M:%S.%3N)] $*" | tee -a "$DIAG_LOG"; }
err()  { echo "[$(date +%H:%M:%S.%3N)] ERROR: $*" | tee -a "$DIAG_LOG"; }
warn() { echo "[$(date +%H:%M:%S.%3N)] WARN:  $*" | tee -a "$DIAG_LOG"; }

echo ""
echo "  ╔═══════════════════════════════════════════════════╗"
echo "  ║   Multi-Node Online UKF Pipeline v6 Startup      ║"
echo "  ║   Core 0: 5bus+9bus | Core 1: FreeRTOS | Core 2: 39bus ║"
echo "  ║   Speed: ${SPEED}                                    ║"
echo "  ╚═══════════════════════════════════════════════════╝"
echo ""
log "========== START_ALL v6 (speed=${SPEED}) =========="

# ── 预检 ──
log "Preflight checks..."
IFACE=$(ip route get 1.1.1.1 2>/dev/null | awk '{print $5; exit}')
log "  Primary interface: ${IFACE:-unknown}"
log "  Kernel: $(uname -r)"
log "  Arch: $(uname -m)"

# 检查 nproc (应为 3, FreeRTOS 占用 1 个核)
NPROC=$(nproc 2>/dev/null || echo "?")
log "  nproc (Linux visible cores): $NPROC"
if [ "$NPROC" != "3" ]; then
    warn "Expected 3 Linux cores (FreeRTOS occupies 1), got $NPROC"
fi

# 检查必需文件
for f in ukf_pipeline_5bus ukf_pipeline_39bus ukf_pipeline_9bus start_sim_nodes reset_shm launch_ukf_multi.py; do
    if [ ! -f "$DIR/$f" ]; then
        err "Missing: $DIR/$f"
        exit 1
    fi
done

# 检查是否为 ARM64 平台 (Phytium) — 防止 x86 二进制部署错误
BIN_ARCH=$(file "$DIR/start_sim_nodes" | grep -oP 'ARM aarch64|x86-64|Intel 80386' | head -1)
MACHINE=$(uname -m)
if [ "$MACHINE" != "aarch64" ]; then
    err "This script must run on ARM64 Phytium board (current: $MACHINE)"
    err "Binary architecture: $BIN_ARCH"
    exit 1
fi
if [ "$BIN_ARCH" != "aarch64" ] && [ "$BIN_ARCH" != "ARM aarch64" ]; then
    err "Binary compiled for $BIN_ARCH, but board is $MACHINE! Recompile with aarch64-linux-gnu-gcc."
    exit 1
fi
log "  Binary arch check OK ($BIN_ARCH)"

# ── Step 1: 清理旧进程 ──
log "Step 1/6: Cleaning old processes..."
for name in ukf_pipeline launch_ukf_multi.py start_sim_nodes; do
    if pgrep -f "$name" > /dev/null 2>&1; then
        log "  Killing $name..."
        sudo pkill -9 -f "$name" 2>/dev/null || true
    fi
done
sleep 1
# 验证清理结果
for name in ukf_pipeline launch_ukf_multi.py start_sim_nodes; do
    if pgrep -f "$name" > /dev/null 2>&1; then
        warn "  $name still running!"
    fi
done
log "  Cleaned OK"

# ── Step 2: 重载 FreeRTOS 固件 ──
log "Step 2/6: Reloading FreeRTOS firmware..."
REMOTEPROC="/sys/class/remoteproc/remoteproc0"
if [ ! -d "$REMOTEPROC" ]; then
    err "remoteproc0 not found! Is FreeRTOS firmware loaded?"
    err "  Check: ls /sys/class/remoteproc/"
    exit 1
fi

OLD_STATE=$(cat "$REMOTEPROC/state" 2>/dev/null || echo "unknown")
log "  Old state: $OLD_STATE"

echo "$USER_PASS" | sudo -S sh -c "echo stop > $REMOTEPROC/state" 2>/dev/null
sleep 2
echo "$USER_PASS" | sudo -S sh -c "echo start > $REMOTEPROC/state"
sleep 3

STATE=$(cat "$REMOTEPROC/state" 2>/dev/null || echo "unknown")
log "  New state: $STATE"

if [ "$STATE" != "running" ]; then
    err "FreeRTOS failed to start! (state=$STATE)"
    err "  Check: dmesg | tail -20"
    exit 1
fi

# ── Step 3: 重置 SHM + 清理旧数据 ──
log "Step 3/6: Resetting SHM and cleaning old data..."
echo "$USER_PASS" | sudo -S "$DIR/reset_shm" 2>&1 | tee -a "$DIAG_LOG"
echo "$USER_PASS" | sudo -S rm -f /tmp/ukf_*.csv /tmp/ukf_*.json /tmp/ukf_*.log /tmp/ukf_metrics.json* /tmp/ukf_*.bin
log "  SHM reset OK"

# ── Step 3.5: 预绑定 RPMsg endpoint (prime) ──
# FreeRTOS 重启后, 39bus/9bus RPMsg endpoint 存在绑定竞态,
# 需要先启动一次 start_sim_nodes 再杀掉, 让 FreeRTOS 端点稳定绑定。
log "Step 3.5/6: Priming RPMsg endpoints..."
echo "$USER_PASS" | sudo -S "$DIR/start_sim_nodes" "$SPEED" > "$LOG_DIR/prime_${TIMESTAMP}.log" 2>&1 &
PRIME_PID=$!
sleep 6
kill -9 "$PRIME_PID" 2>/dev/null || true
wait "$PRIME_PID" 2>/dev/null || true
sleep 1
log "  Prime done"

# ── Step 4: 启动 UKF Pipeline (必须在仿真前) ──
log "Step 4/6: Starting UKF pipelines..."
log "  CPU binding: 5bus→Core0, 9bus→Core0, 39bus→Core2"
nohup python3 "$DIR/launch_ukf_multi.py" --nodes=5bus,39bus,9bus > "$LAUNCH_LOG" 2>&1 &
LAUNCH_PID=$!
echo $LAUNCH_PID > /tmp/ukf_launcher.pid
log "  Launcher PID=$LAUNCH_PID"

# 等待 UKF 进程启动 (每个节点 3 个线程: main+stderr+run=3, 3节点=9, 加上 c ukf_pipeline=3)
sleep 10

# 详细检查每个进程
UKF_COUNT=$(pgrep -c ukf_pipeline 2>/dev/null || echo 0)
PY_COUNT=$(pgrep -cf launch_ukf_multi.py 2>/dev/null || echo 0)
log "  ukf_pipeline C processes: $UKF_COUNT (expect 3)"
log "  launch_ukf_multi.py:       $PY_COUNT (expect 1)"

if [ "$UKF_COUNT" -lt 3 ]; then
    err "Only $UKF_COUNT/3 UKF pipeline processes started!"
    err "Last 30 lines of $LAUNCH_LOG:"
    tail -30 "$LAUNCH_LOG" | tee -a "$DIAG_LOG"
    # 不退出, 继续尝试 (可能有些节点启动慢)
fi

# 检查 CPU 绑定是否正确
log "  CPU binding verification:"
for pid in $(pgrep ukf_pipeline 2>/dev/null); do
    AFFINITY=$(taskset -p "$pid" 2>/dev/null | awk '{print $NF}')
    log "    PID $pid → affinity $AFFINITY"
done

# 检查指标文件写入
if [ -f /tmp/ukf_metrics.json ]; then
    log "  Metrics file created OK"
else
    warn "  Metrics file not yet created"
fi

# ── Step 5: 启动 FreeRTOS 仿真 ──
log "Step 5/6: Starting simulation (speed=${SPEED})..."
echo ""
echo "  ╔═════════════════════════════════════════════════════╗"
echo "  ║  Simulation STARTING                               ║"
echo "  ║  Open another SSH session and run:                 ║"
echo "  ║    cd ~/Phytium/task2_linux && ./monitor.sh        ║"
echo "  ╚═════════════════════════════════════════════════════╝"
echo ""

# 启动时打时间戳
START_TS=$(date +%s)
SIM_START=$(date +%H:%M:%S)
log "  Simulation start time: $SIM_START"

echo "$USER_PASS" | sudo -S "$DIR/start_sim_nodes" "$SPEED" 2>&1 | tee -a "$SIM_LOG"
SIM_EXIT=$?

END_TS=$(date +%s)
ELAPSED=$((END_TS - START_TS))
SIM_END=$(date +%H:%M:%S)

echo ""
log "========== Simulation Finished =========="
log "  Start: $SIM_START  End: $SIM_END  Elapsed: ${ELAPSED}s"
log "  Exit code: $SIM_EXIT"

# ── 后处理: 状态汇总 ──
log "========== Final Status Report =========="

# 1. 三节点 UKF 最终帧数
if [ -f /tmp/ukf_metrics.json ]; then
    log "-- UKF Pipeline Results --"
    python3 -c "
import json
with open('/tmp/ukf_metrics.json') as f:
    data = json.load(f)
for node in ['5bus','39bus','9bus']:
    n = data.get(node, {})
    print(f'  {node}: frames={n.get(\"frames\",0)} fps={n.get(\"fps\",0)} '
          f'rmse={n.get(\"rmse\",0)} cpu={n.get(\"cpu_pct\",0)}% status={n.get(\"status\",\"?\")}')
" 2>/dev/null | tee -a "$DIAG_LOG"
else
    warn "  No metrics file found"
fi

# 2. 进程是否正常退出
log "-- Process Status --"
UKF_ALIVE=$(pgrep -c ukf_pipeline 2>/dev/null || echo 0)
LAUNCHER_ALIVE=$(pgrep -cf launch_ukf_multi.py 2>/dev/null || echo 0)
if [ "$UKF_ALIVE" -gt 0 ]; then
    warn "  $UKF_ALIVE ukf_pipeline processes still alive (expected 0)"
else
    log "  All ukf_pipeline processes exited normally"
fi

# 3. 日志
log "-- Logs --"
log "  Launch log:  $LAUNCH_LOG"
log "  Sim log:     $SIM_LOG"
log "  Diag log:    $DIAG_LOG"
for node in 5bus 39bus 9bus; do
    LOGF="/tmp/ukf_log_${node}.log"
    if [ -f "$LOGF" ]; then
        LAST_LINE=$(tail -1 "$LOGF" 2>/dev/null)
        log "  $node log: $LAST_LINE"
    fi
done

log "========== START_ALL v6 COMPLETE =========="
