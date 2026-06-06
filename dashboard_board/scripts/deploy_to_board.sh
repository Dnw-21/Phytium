#!/bin/bash
set -e
BOARD_IP="192.168.88.10"
BOARD_USER="user"
BOARD_PASS="user"
REMOTE_DIR="/home/user/dashboard_board"
LOCAL_DIR="/home/alientek/Phytium/dashboard_board"
CURL="curl -s --noproxy '*'"

echo "================================================"
echo "  Dashboard Board 一键部署"
echo "  目标: ${BOARD_USER}@${BOARD_IP}"
echo "================================================"

# Step 1: 本地完整性检查
echo "[1/8] 本地完整性检查..."
for f in server/dashboard_server.py data/dashboard_data.json config.json templates/dashboard.html static/vendor/chart.umd.min.js ukf_src/Makefile; do
    if [ ! -f "$LOCAL_DIR/$f" ]; then
        echo "ERROR: 缺少文件: $f"
        exit 1
    fi
done
echo "  文件检查通过"

# Step 2: 创建远程目录 (先清空旧目录)
echo "[2/8] 准备远程目录..."
sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP} \
    "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR}/{server,data,templates,static/vendor,scripts,systemd,logs,ukf_src/{src,include}}"

# Step 3: scp 文件 (逐目录复制，避免展平)
echo "[3/8] 传输文件..."
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/server/"*     ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/server/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/data/"*       ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/data/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/templates/"*  ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/templates/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/static/"*     ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/static/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/scripts/"*    ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/scripts/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/systemd/"*    ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/systemd/
sshpass -p "$BOARD_PASS" scp -r -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/ukf_src/"*    ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/ukf_src/
sshpass -p "$BOARD_PASS" scp -o StrictHostKeyChecking=no \
    "$LOCAL_DIR/config.json"  ${BOARD_USER}@${BOARD_IP}:${REMOTE_DIR}/
echo "  文件传输完成"

# Step 4: 配置免密 sudo（一次性，后续不再需要密码）
echo "[4/8] 配置免密 sudo..."
sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP} "bash -s" << 'REMOTE_SCRIPT'
set -e
# 确保 sudo 免密（一次性配置）
if [ ! -f /etc/sudoers.d/user ]; then
    echo 'user ALL=(ALL) NOPASSWD:ALL' | echo 'user' | sudo -S tee /etc/sudoers.d/user > /dev/null
    echo 'user' | sudo -S chmod 0440 /etc/sudoers.d/user
    echo "  sudoers 免密配置完成"
else
    echo "  sudoers 免密已存在，跳过"
fi
REMOTE_SCRIPT

# Step 5: 安装依赖
echo "[5/8] 安装依赖..."
sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP} "bash -s" << 'REMOTE_SCRIPT'
set -e
# 设置脚本权限
chmod +x /home/user/dashboard_board/scripts/*.sh

# 检查 Flask 是否已安装
if python3 -c "import flask" 2>/dev/null; then
    echo "  Flask 已安装"
else
    echo "  Flask 未安装，尝试安装..."
    # 尝试从 wheel 安装（离线包）
    if ls /tmp/flask_wheels/*.whl >/dev/null 2>&1; then
        mkdir -p ~/.local/lib/python3.11/site-packages
        for f in /tmp/flask_wheels/*.whl; do
            python3 -c "
import zipfile, os
target = os.path.expanduser('~/.local/lib/python3.11/site-packages')
with zipfile.ZipFile('$f') as z:
    for name in z.namelist():
        if name.endswith('/'): continue
        parts = name.split('/')
        if parts[0].endswith('.dist-info') or parts[0].endswith('.data'):
            dest = os.path.join(target, name)
        else:
            dest = os.path.join(target, '/'.join(parts))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if not os.path.exists(dest):
            with z.open(name) as src, open(dest, 'wb') as dst:
                dst.write(src.read())
"
        done
        python3 -c "import flask; print('  Flask 安装成功')" || echo "  Flask 安装失败"
    else
        echo "  ERROR: 无 wheel 包，请先在 VM 上运行: pip3 download -d /tmp/flask_wheels flask"
        echo "  然后 scp 到板子: scp -r /tmp/flask_wheels user@192.168.88.10:/tmp/"
        exit 1
    fi
fi
REMOTE_SCRIPT

# Step 6: 编译 C UKF
echo "[6/8] 编译 C UKF..."
sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP} \
    "cd ${REMOTE_DIR}/ukf_src && ln -sf ../data data && make clean 2>/dev/null; make 2>&1 | tail -3 && echo '  C UKF 编译完成'"

# Step 7: 注册 systemd
echo "[7/8] 注册 systemd 服务..."
sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP} "bash -s" << 'REMOTE_SCRIPT'
set -e
echo 'user' | sudo -S cp /home/user/dashboard_board/systemd/dashboard-board.service /etc/systemd/system/
echo 'user' | sudo -S cp /home/user/dashboard_board/systemd/dashboard-vnc.service /etc/systemd/system/
echo 'user' | sudo -S systemctl daemon-reload
echo 'user' | sudo -S systemctl enable dashboard-board.service
echo 'user' | sudo -S systemctl enable dashboard-vnc.service
echo 'user' | sudo -S systemctl restart dashboard-board.service
sleep 3
echo 'user' | sudo -S systemctl restart dashboard-vnc.service
echo "  服务注册 + 启动完成"
REMOTE_SCRIPT

# Step 8: 验证
echo "[8/8] 验证..."
sleep 5
HTTP_OK=$($CURL -o /dev/null -w "%{http_code}" http://${BOARD_IP}:5000/api/status 2>/dev/null || echo "000")
if [ "$HTTP_OK" = "200" ]; then
    echo "  HTTP :5000  ✓"
else
    echo "  HTTP :5000  ✗ (code: $HTTP_OK)"
fi

echo ""
echo "================================================"
echo "  部署完成！"
echo ""
echo "  VNC 连接:"
echo "    vncviewer ${BOARD_IP}:5901"
echo ""
echo "  浏览器访问:"
echo "    http://${BOARD_IP}:5000"
echo "================================================"