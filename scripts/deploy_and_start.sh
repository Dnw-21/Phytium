#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  飞腾派 LoRa 接收系统 — 一键部署+启动
#
#  运行位置: 飞腾派开发板上 (/home/user/Phytium/ 目录下)
#  用法:
#    # 首次: 先手动把 Phytium/ 目录拷贝到飞腾派
#    #   scp -r Phytium/scripts Phytium/demo user@192.168.88.11:~/Phytium/
#    #   scp pe2204_*.elf user@192.168.88.11:~/fw/
#    #
#    cd ~/Phytium/scripts
#    chmod +x deploy_and_start.sh
#    ./deploy_and_start.sh
#
#  停止:
#    ./deploy_and_start.sh stop
#
#  LoRa RX 控制:
#    ~/Phytium/demo/lora_ctrl start     # 开启
#    ~/Phytium/demo/lora_ctrl stop      # 关闭
# ═══════════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FW_DIR="${HOME}/fw"
FW_NAME="openamp_core0.elf"
FW_SRC="${FW_DIR}/pe2204_aarch64_phytiumpi_openamp_for_linux.elf"
BOARD_PASS="user"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()   { echo -e "${GREEN}[ OK ]${NC}  $*"; }
err()  { echo -e "${RED}[FAIL]${NC}  $*"; }

CMD="${1:-start}"

# ─────────────────────────────────────────────
#  停止
# ─────────────────────────────────────────────
if [ "$CMD" = "stop" ]; then
    log "停止 OpenAMP..."
    killall master_receiver 2>/dev/null && ok "master_receiver 已停止" || true
    killall lora_ctrl 2>/dev/null || true
    echo "$BOARD_PASS" | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state' 2>/dev/null || true
    sleep 1
    STATE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null || echo "N/A")
    ok "从核状态: $STATE"
    exit 0
fi

# ─────────────────────────────────────────────
#  部署固件
# ─────────────────────────────────────────────
log "=== 步骤 1/5: 部署固件 ==="

if [ -f "$FW_SRC" ]; then
    echo "$BOARD_PASS" | sudo -S cp "$FW_SRC" "/lib/firmware/$FW_NAME"
    ok "固件已部署: $FW_NAME ($(du -h "$FW_SRC" | cut -f1))"
elif [ -f "/tmp/$FW_NAME" ]; then
    echo "$BOARD_PASS" | sudo -S cp "/tmp/$FW_NAME" "/lib/firmware/$FW_NAME"
    ok "固件已部署(从/tmp): $FW_NAME"
else
    err "固件文件未找到!"
    echo "  请将 pe2204_aarch64_phytiumpi_openamp_for_linux.elf 放到 ${FW_DIR}/ 下"
    exit 1
fi

# ─────────────────────────────────────────────
#  停止旧实例
# ─────────────────────────────────────────────
log "=== 步骤 2/5: 清理旧实例 ==="
echo "$BOARD_PASS" | sudo -S systemctl stop openamp.service 2>/dev/null || true
echo "$BOARD_PASS" | sudo -S killall dashboard_server 2>/dev/null || true
echo "$BOARD_PASS" | sudo -S killall lifecycle_mgr 2>/dev/null || true
sleep 1
echo "$BOARD_PASS" | sudo -S sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/unbind' 2>/dev/null || true
echo "$BOARD_PASS" | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state' 2>/dev/null || true
sleep 1
echo "$BOARD_PASS" | sudo -S rmmod rpmsg_ctrl 2>/dev/null || true
echo "$BOARD_PASS" | sudo -S rmmod rpmsg_char 2>/dev/null || true
killall master_receiver 2>/dev/null || true
ok "清理完成"

# ─────────────────────────────────────────────
#  加载模块
# ─────────────────────────────────────────────
log "=== 步骤 3/5: 加载 RPMsg 模块 ==="
echo "$BOARD_PASS" | sudo -S modprobe rpmsg_char rpmsg_ctrl 2>/dev/null
lsmod | grep -q rpmsg && ok "模块已加载" || { err "模块加载失败"; exit 1; }

# ─────────────────────────────────────────────
#  启动 FreeRTOS 从核
# ─────────────────────────────────────────────
log "=== 步骤 4/5: 启动 FreeRTOS CPU3 ==="
echo "$BOARD_PASS" | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3
STATE=$(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null)
if [ "$STATE" = "running" ]; then
    ok "从核: running"
else
    err "从核启动失败: $STATE"
    dmesg | tail -20 | grep -i "rproc\|remoteproc"
    exit 1
fi

# ─────────────────────────────────────────────
#  绑定 RPMsg 通道
# ─────────────────────────────────────────────
log "=== 步骤 5/5: 绑定 RPMsg 通道 ==="
sleep 1
CH=$(ls /sys/bus/rpmsg/devices/ 2>/dev/null | grep "openamp-demo" | head -1)
if [ -z "$CH" ]; then
    err "未找到 RPMsg 通道设备!"
    echo "  ls /sys/bus/rpmsg/devices/"
    ls -la /sys/bus/rpmsg/devices/ 2>/dev/null || echo "  (目录不存在)"
    exit 1
fi

echo "$BOARD_PASS" | sudo -S sh -c "echo rpmsg_chrdev > /sys/bus/rpmsg/devices/$CH/driver_override"
echo "$BOARD_PASS" | sudo -S sh -c "echo $CH > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind"
echo "$BOARD_PASS" | sudo -S chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0 2>/dev/null
ok "通道已绑定: $CH"

ls -la /dev/rpmsg0 2>/dev/null && ok "/dev/rpmsg0 就绪" || err "/dev/rpmsg0 未创建"

# ─────────────────────────────────────────────
#  验证: DEVICE_CORE_CHECK ping
# ─────────────────────────────────────────────
log "=== 验证: PING 测试 ==="
sleep 1

# 快速脚本PING
cat > /tmp/ping_test.c << 'CEOF'
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
int main() {
    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) { printf("open failed\n"); return 1; }
    uint8_t buf[] = {0x03,0x00,0x00,0x00, 0x00,0x00};
    write(fd, buf, 6);
    usleep(200000);
    uint8_t rx[128];
    int r = read(fd, rx, sizeof(rx));
    if (r >= 6 && rx[4]==0 && rx[5]==0) { printf("PONG\n"); }
    else { printf("NO_RESPONSE r=%d\n", r); }
    close(fd);
    return 0;
}
CEOF
gcc -O2 /tmp/ping_test.c -o /tmp/ping_test 2>/dev/null
if /tmp/ping_test 2>/dev/null | grep -q PONG; then
    ok "RPMsg 双向通信 OK"
else
    log "PING 未响应（可能正常，受限于时序）"
fi

# ─────────────────────────────────────────────
#  完成
# ─────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║       LoRa 接收系统已启动!                           ║"
echo "╠══════════════════════════════════════════════════════╣"
echo "║                                                      ║"
echo "║  监控数据:                                           ║"
echo "║    ~/Phytium/demo/master_receiver --monitor          ║"
echo "║                                                      ║"
echo "║  LoRa RX 开关:                                       ║"
echo "║    ~/Phytium/demo/lora_ctrl start   ← 开启接收        ║"
echo "║    ~/Phytium/demo/lora_ctrl stop    ← 暂停接收        ║"
echo "║    ~/Phytium/demo/lora_ctrl status  ← 查询状态        ║"
echo "║                                                      ║"
echo "║  停止系统:                                           ║"
echo "║    ~/Phytium/scripts/deploy_and_start.sh stop        ║"
echo "║                                                      ║"
echo "╚══════════════════════════════════════════════════════╝"
