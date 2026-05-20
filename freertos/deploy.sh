#!/bin/bash
#
# FreeRTOS OpenAMP 固件部署脚本 (v10 Final, 2026-05-21)
#
#  用法: cd /home/alientek/Phytium/freertos && bash deploy.sh
#
#  功能:
#    1. 编译 FreeRTOS 固件 (调用SDK make)
#    2. 传输到开发板 192.168.88.11
#    3. 安全启动 — 仅在 remoteproc offline 时 start, running 时跳过重启
#       (避免 echo stop 触发 OP-TEE 重新初始化远程核 → RCU stall)
#    4. 运行 trace_reader 验证数据解析
#
#  目录结构:
#    /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/  ← SDK + 编译系统
#    /home/alientek/Phytium/freertos/                               ← 源码 + 文档 + 脚本 (本目录)
#
#  编译依赖: 飞腾 SDK build 系统, aarch64-none-elf-gcc 工具链

set -e

SDK_DIR="/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux"
TOOLCHAIN="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
BOARD_IP="192.168.88.11"
ELF_FILE="pe2204_aarch64_phytiumpi_openamp_for_linux.elf"
FW_TARGET="/lib/firmware/openamp_core0.elf"

SSH="sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@${BOARD_IP}"
SCP="sshpass -p 'user' scp -o StrictHostKeyChecking=no"
SUDO="echo 'user' | sudo -S"

echo "==== 1/6 编译 FreeRTOS 固件 ===="
cd "${SDK_DIR}"
export AARCH64_CROSS_PATH="${TOOLCHAIN}"
make config_pe2204_phytiumpi_aarch64
make clean
make all -j$(nproc)
echo "  ELF: $(ls -lh ${ELF_FILE} | awk '{print $5}')"

echo ""
echo "==== 2/6 传输固件到开发板 ===="
${SCP} "${ELF_FILE}" "user@${BOARD_IP}:/tmp/"
echo "  done"

echo ""
echo "==== 3/6 检查 remoteproc 状态 ===="
STATE=$(${SSH} "${SUDO} cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null" 2>/dev/null || echo "unknown")
echo "  remoteproc state = ${STATE}"

echo ""
echo "==== 4/6 更新固件到 ${FW_TARGET} ===="
${SSH} "${SUDO} cp /tmp/${ELF_FILE} ${FW_TARGET} && sync && echo '  firmware updated'"

echo ""
if [ "${STATE}" = "offline" ] || [ "${STATE}" = "unknown" ]; then
    echo "==== 5a/6 固件离线, 直接启动 (安全) ===="
    ${SSH} "${SUDO} sh -c 'echo openamp_core0.elf > /sys/class/remoteproc/remoteproc0/firmware'"
    ${SSH} "${SUDO} sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'"
    sleep 5
    NEW_STATE=$(${SSH} "${SUDO} cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null" 2>/dev/null || echo "unknown")
    echo "  remoteproc new state = ${NEW_STATE}"
else
    echo "==== 5b/6 固件已在运行, 跳过重启 ===="
    echo "  (避免 echo stop 触发 OP-TEE 重初始化远程核 → RCU stall)"
    echo "  新固件已写入 ${FW_TARGET}, 下次 reboot 生效"
fi

echo ""
echo "==== 6/6 验证数据解析 (trace_reader 45s) ===="
${SSH} "${SUDO} timeout 45 /home/user/trace_reader 2>/dev/null" 2>/dev/null | head -80

echo ""
echo "==== 部署完毕 ===="