#!/bin/bash
set -e
scp /home/alientek/Phytium/freertos/restart_lora.sh user@192.168.88.11:/home/user/ 2>/dev/null
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.11 \
  "echo 'user' | sudo -S systemctl restart openamp.service && echo 'FreeRTOS restarted OK'"
