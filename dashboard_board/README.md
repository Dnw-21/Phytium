# Dashboard Board — 微电网 UKF 状态估计大屏

飞腾派嵌入式大屏监控系统，基于 UKF 多机状态估计，实时展示 IEEE 9-Bus 微电网的转子角度/转速曲线、故障检测、天气风险预警和灾害仿真。

## 项目结构

```
dashboard_board/
├── README.md
├── 需求开发文档.md              # 需求清单 & 接口契约
├── DEPLOY.md                    # 部署指南（详细版）
├── config.json                  # 系统配置（3节点全部启用、WiFi、飞书通知）
├── data/
│   ├── dashboard_data.json      # 预计算数据（VM 端 build_data_1000hz.py 生成）
│   ├── true_states.csv          # 真实状态（1Hz，供参考）
│   ├── measurements.txt         # 测量数据（node_1 遗留）
│   ├── system_params.txt        # 系统参数（node_1 遗留）
│   ├── weather_cache.json       # 天气缓存
│   ├── node_1/                  # Z1 数据：Bus 8, Line 8-9 故障
│   ├── node_2/                  # Z2 数据：Bus 4, Line 4-5 故障
│   ├── node_3/                  # Z3 数据：Bus 3, Line 3-9 故障
│   ├── estimates_node_*.csv     # 三节点 UKF 预计算估值
│   └── measurements_node_*.txt  # 三节点测量数据
├── prep/
│   ├── build_data_1000hz.py     # 三节点数据构建脚本（从 DSE 目录取材）
│   └── generate_3node_data.py   # 三节点 UKF 估值生成脚本
├── scripts/
│   ├── deploy_to_board.sh       # 一键部署脚本（VM → 飞腾派）
│   ├── start_vnc.sh             # VNC 桌面启动脚本
│   ├── xstartup_vnc             # VNC xstartup 配置
│   ├── start.sh / stop.sh / status.sh / tail_log.sh
│   └── connect_wifi.sh
├── server/
│   ├── dashboard_server.py      # Flask 主服务（三节点、飞书合并推送、UKF 仿真）
│   ├── heartbeat_source.py      # 心跳源（模拟节点在线状态）
│   ├── feishu_notifier.py       # 飞书自定义机器人推送
│   └── wechat_notifier.py       # 微信 Server酱推送
├── systemd/
│   ├── dashboard-board.service  # HTTP 服务 systemd 单元
│   └── dashboard-vnc.service    # VNC 桌面 systemd 单元
├── templates/
│   └── dashboard.html           # 前端大屏（Chart.js 18 图表 + 三节点历史/日志 + 灾害仿真）
├── tools/
│   ├── controller_online        # C UKF 二进制 (x86_64)
│   └── controller_online.arm    # C UKF 二进制 (aarch64, 飞腾派)
└── static/
    └── vendor/
        └── chart.umd.min.js     # Chart.js 4.x
```

## 新开发板完整搭建流程

### 前置条件

- 飞腾派 (Phytium Pi) 已烧录 PIOS 镜像
- 开发板已连接 WiFi（`bash scripts/connect_wifi.sh`）
- 开发板 IP：`192.168.88.10`
- 开发板用户：`user`，密码：`user`（已配置免密 sudo）

### Step 1：安装系统依赖

```bash
# SSH 到开发板
ssh user@192.168.88.10

# 更新包列表
echo "user" | sudo -S apt-get update

# 安装 Python 环境
echo "user" | sudo -S apt-get install -y python3 python3-pip python3-flask python3-requests dbus-x11

# 安装 VNC 桌面环境
echo "user" | sudo -S apt-get install -y tigervnc-standalone-server xfce4 xfce4-terminal firefox-esr

# 配置免密 sudo（避免后续脚本卡住）
echo "user ALL=(ALL) NOPASSWD:ALL" | echo "user" | sudo -S tee /etc/sudoers.d/user
echo "user" | sudo -S chmod 440 /etc/sudoers.d/user
```

### Step 2：在 VM 生成预计算数据

```bash
# 在虚拟机上执行（需要 numpy）
cd /home/alientek/Phytium/dashboard_board
python3 prep/prepare_data.py
```

### Step 3：一键部署到开发板

```bash
cd /home/alientek/Phytium/dashboard_board
bash scripts/deploy_to_board.sh
```

部署脚本会自动：
1. 将全部代码 SCP 到开发板 `/home/user/dashboard_board/`
2. 安装 systemd 服务
3. 设置 VNC 密码
4. 配置免密 sudo
5. 重启 HTTP 和 VNC 服务

### Step 4：验证部署

```bash
# SSH 到开发板检查
ssh user@192.168.88.10

# 检查 HTTP 服务
curl -s http://localhost:5000/api/status | python3 -m json.tool

# 检查 VNC 服务
vncserver -list
# 应显示：:2 (port 5902)

# 检查进程
ps aux | grep -E "dashboard_server|firefox|xfdesktop|xfce4-panel" | grep -v grep
```

### Step 5：连接 VNC 查看大屏

在你的电脑上使用 VNC Viewer 连接：
- 地址：`192.168.88.10:5902`
- 密码：`user123`

浏览器内访问：`http://192.168.88.10:5000`

## 快速命令

```bash
# 开发板端
echo "user" | sudo -S systemctl restart dashboard-board.service   # 重启 HTTP
echo "user" | sudo -S systemctl restart dashboard-vnc.service     # 重启 VNC
echo "user" | sudo -S journalctl -u dashboard-board.service -f    # 查看日志

# VM 端
bash scripts/deploy_to_board.sh    # 一键部署
```

## 云端服务

- 飞书通知：自定义机器人 Webhook（`config.json` 中配置）
- 微信通知：Server酱（`config.json` 中配置 `wechat.sendkey`）
- 天气 API：心知天气（`config.json` 中配置 `weather.key`，无 key 时使用仿真数据）

## 仿真参数

| 参数 | 值 |
|------|-----|
| 系统模型 | IEEE 9-Bus |
| 发电机数 | 3 台 |
| 母线数 | 9 条 |
| 采样率 | 1000 Hz |
| 故障1 | t=5.0s，母线 8-9 三相短路 |
| 故障2 | t=15.0s，母线 8-9 三相短路 |
| 实时刷新间隔 | 1s（1Hz 降采样） |
| 故障回放分辨率 | 1ms（1000Hz 全分辨率） |

## TF 卡保护策略

- 所有日志写入 print 到 stdout，不写磁盘文件
- 历史数据存内存，不持久化
- 代码修改在 VM 完成，SCP 一次性部署到板
- 禁止在板子上高频擦写文件

## 更新记录

| 日期 | 更新内容 |
|------|----------|
| 2026-06-05 | 初始版本：12 个需求，全部 VM 端完成 |
| 2026-06-05 | 修复 history 初始化不一致；C UKF 编译通过并产出 CSV |
| 2026-06-06 | 板上调试：Flask wheel 离线安装、systemd 双服务启动正常、VNC :2 运行中 |
| 2026-06-06 | 修复：VNC Firefox 从 --kiosk 改为 --new-window；增加 xstartup 启动 xfce4 桌面 |
| 2026-06-06 | 修复：sudo 免密配置；故障回放按故障顺序揭示；曲线/数据分页；10s 滑动窗口 |
| 2026-06-09 | v2 发布：三节点真实 DSE 数据驱动；飞书合并推送；移除系统信息面板/RMSE；X 轴北京时间+15s 窗口；数据接收速率+电量；历史/日志三节点标签页 |
| 2026-06-09 | v3 发布：三节点独立时序故障（step 10/15/20）；独立图表高亮；故障检测 Bento 聚合；飞书独立推送（bypass_rate_limit）；三节点独立故障回放；系统配置栏精简；时间尺度分离（FAULT_TUNING + FAULT_STEPS）；故障结束后自动恢复 |
| 2026-06-14 | v4 发布：三节点轮播显示（0-2s/3-5s/6-8s 切换 node_1/2/3）；6 图表 3 曲线对比；每节点滑动窗口 20 点；X 轴北京时间精确到毫秒；chunk 循环（0-19/20-39/40-59/60-79）；非活跃节点曲线静止；VNC Openbox 1024x600 轻量部署 |
| 2026-06-09 | VNC 方案变更：Xfce 1920x1080 → Openbox 1024x600（飞腾派内存不足，FHD 反复触发 kernel panic）；DEPLOY.md 新增 VNC 轻量部署章节 |
| 2026-06-14 | v4 发布：三节点轮播显示（0-3s node_1 / 4-6s node_2 / 7-9s node_3）；6 图表 × 3 曲线布局；每 3s 窗口显示 20ms（20 点）数据；chunk 切换自动清空图表；X 轴改为毫秒刻度；DEPLOY.md 新增本地浏览器直连说明；config.json VNC geometry 改为 1024x600 |

## GitHub 仓库

本项目是 `https://github.com/xxluestc/Phytium.git` 仓库的子目录：

```
Phytium/
├── dashboard_board/     # 本目录
├── state_estimation/    # UKF 状态估计算法
├── lyq/                 # 异构环境搭建文档
└── ...                  # 其他子项目
```

```bash
cd /home/alientek/Phytium
git add dashboard_board/
git commit -m "dashboard_board: 微电网 UKF 状态估计大屏 v1.0"
git push origin main
```