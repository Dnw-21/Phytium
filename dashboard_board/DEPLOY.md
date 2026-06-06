# Dashboard Board 部署指南

## 前置条件

- 飞腾派 IP: 192.168.88.10
- SSH 用户: user / 密码: user
- sudo 密码: user
- VM 端已安装 sshpass

## 首次部署

### 步骤 1: 生成预计算数据

```bash
cd /home/alientek/Phytium/dashboard_board
python3 prep/prepare_data.py
```

如果 ukf_cache.npz 不存在，请先从 state_estimation/ 复制。

### 步骤 2: 一键部署

```bash
bash scripts/deploy_to_board.sh
```

脚本会自动完成：
- scp 文件到飞腾派
- 安装 Flask（pip3 install flask）
- 注册 systemd 服务
- 启动 HTTP + VNC

### 步骤 3: 验证

```bash
# VM 端验证 HTTP
curl http://192.168.88.10:5000/api/status

# 浏览器访问
http://192.168.88.10:5000

# VNC 访问
vncviewer 192.168.88.10:5901
```

## 手动部署（如果脚本失败）

```bash
# 1. scp 文件
sshpass -p 'user' scp -r server/ data/ templates/ static/ scripts/ systemd/ config.json user@192.168.88.10:/home/user/dashboard_board/

# 2. SSH 板子
ssh user@192.168.88.10

# 3. 安装 Flask
pip3 install --break-system-packages flask

# 4. 设置权限
chmod +x /home/user/dashboard_board/scripts/*.sh

# 5. 注册 systemd
sudo cp /home/user/dashboard_board/systemd/dashboard-board.service /etc/systemd/system/
sudo cp /home/user/dashboard_board/systemd/dashboard-vnc.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable dashboard-board dashboard-vnc
sudo systemctl start dashboard-board

# 6. 等待 5 秒，验证
curl http://localhost:5000/api/status
```

## Flask 安装失败

如果板子无外网，用 VM 预下载 wheel：

```bash
# VM 端
pip3 download flask -d /tmp/flask_wheels/
sshpass -p 'user' scp -r /tmp/flask_wheels/ user@192.168.88.10:/tmp/
ssh user@192.168.88.10 "pip3 install --break-system-packages --no-index --find-links /tmp/flask_wheels/ flask"
```

## WiFi 配置

编辑飞腾派上的 `/home/user/dashboard_board/config.json`：
```json
"wifi": { "ssid": "YourWiFi", "password": "YourPassword" }
```
然后运行：
```bash
bash /home/user/dashboard_board/scripts/connect_wifi.sh
```

## 故障排查

| 问题 | 检查 |
|------|------|
| 部署失败 | VM 能否 ping 192.168.88.10 |
| HTTP 500 | `tail -f /home/user/dashboard_board/logs/server.log` |
| VNC 连不上 | `sudo systemctl status dashboard-vnc` |
| 前端白屏 | 浏览器 F12 → Console 看 JS 错误 |
| chart.js 不加载 | 确认 static/vendor/chart.umd.min.js 存在 |