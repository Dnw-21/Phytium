#!/bin/bash

set -e

echo "==== 1. 开始编译 FreeRTOS ===="

cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make config_pe2204_phytiumpi_aarch64
make clean
make all -j$(nproc)

echo "==== 2. 传输新固件 ===="
sshpass -p 'user' scp -o StrictHostKeyChecking=no \
  pe2204_aarch64_phytiumpi_openamp_for_linux.elf \
  user@192.168.88.11:/tmp/

echo "==== 3. 隔离CPU1 (FreeRTOS实际运行核心) + 停止+替换+启动 ===="
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo 'user' | sudo -S systemctl stop openamp.service 2>/dev/null; \
   echo 'user' | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state 2>/dev/null'; \
   sleep 2; \
   echo 'user' | sudo -S sh -c 'echo 0 > /sys/devices/system/cpu/cpu1/online 2>/dev/null'; \
   echo 'CPU1 offline ok'; \
   echo 'user' | sudo -S cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf /lib/firmware/openamp_core0.elf; \
   sync; \
   echo 'user' | sudo -S systemctl start openamp.service; \
   sleep 2; \
   S=\$(echo 'user' | sudo -S sh -c 'cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null'); \
   echo \"state=\$S\"; \
   if [ \"\$S\" != \"running\" ]; then \
     echo 'user' | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state 2>/dev/null'; \
     sleep 1; \
   fi; \
   echo 'user' | sudo -S sh -c 'cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null'"

echo "==== 4. 验证固件 AT 命令 ===="
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "strings /lib/firmware/openamp_core0.elf | grep -E 'AT\+WLRATE=23|AT\+TPOWER|AT\+UART=7'"

echo "==== 5. 运行 trace_reader 验证第一行 ===="
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo 'user' | sudo -S timeout 5 /home/user/trace_reader 2>/dev/null" | head -50

echo "==== 部署完毕！ ===="
