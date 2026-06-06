#!/bin/bash
# WiFi 连接工具
CONFIG="/home/user/dashboard_board/config.json"
SSID=$(python3 -c "import json; print(json.load(open('$CONFIG'))['wifi']['ssid'])" 2>/dev/null)
PWD=$(python3 -c "import json; print(json.load(open('$CONFIG'))['wifi']['password'])" 2>/dev/null)
if [ -z "$SSID" ] || [ "$SSID" = "" ]; then
    echo "ERROR: 请先在 config.json 中配置 wifi.ssid 和 wifi.password"
    exit 1
fi
echo "Connecting to $SSID..."
sudo nmcli device wifi connect "$SSID" password "$PWD" 2>&1
sleep 2
echo "Testing internet..."
if ping -c 1 -W 3 api.seniverse.com >/dev/null 2>&1; then
    echo "WiFi connected, internet OK"
else
    echo "WiFi connected but no internet access"
fi