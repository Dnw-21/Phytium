# 操作手册 — LoRa 波形数据接收与绘图

> **更新**: 2026-05-23 | **目标读者**: AI 助手与开发人员
>
> 本文档是自包含的操作指南。从零开始，任何 AI 或开发者都能按照步骤完成编译、部署、接收和绘图全流程。

## 一、系统概述

### 1.1 硬件架构

```
GD32终端节点 ──LoRa无线──→ ATK-MWCC68D模块 ──UART2──→ 飞腾派PE2204
                                                         │
                                              ┌──────────┴──────────┐
                                              │ FreeRTOS 主控侧      │
                                              │ 实际 CPU1           │
                                              │ 设备树仍写 CPU3      │
                                              │  UART RX → 帧解析    │
                                              │  → 共享内存输出      │
                                              └─────────────────────┘
                                                         │
                                              /dev/mem (trace_reader)
                                                         │
                                              开发板串口/Linux终端
                                                         │
                                              SSH/scp → 虚拟机
                                                         │
                                              plot_wave.py → PNG图片
```

### 1.2 关键地址和参数

| 项目 | 值 |
|------|-----|
| 开发板 IP | 192.168.88.10 |
| 开发板用户/密码 | user / user |
| root 密码 | user |
| 共享内存地址 | 0xC8000000 (trace_reader 通过 /dev/mem 读取) |
| FreeRTOS 固件路径 | `/lib/firmware/openamp_core0.elf` |
| trace_reader 路径 | `/home/user/trace_reader`，这是开发板 Linux 侧读取共享内存 trace 输出的可执行程序路径，不是虚拟机本机路径 |
| LoRa 波特率 | 115200 (UART2, FreeRTOS 主控侧独占) |

### 1.3 数据格式

**LoRa 帧格式**: `[AA][55][LEN_H][LEN_L][DATA][CRC8][55][AA]`

**DATA 字段**: `[timestamp 4B][data_type 1B][sync_code 4B][payload mB]`

| data_type | 名称 | 说明 |
|-----------|------|------|
| 0x01 | FAULT | 故障数据 |
| 0x02 | WAVE | 波形头 (包含采样率、点数等) |
| 0x04 | STATUS | 状态数据 |
| 0x05 | FLASH_WAVE | 波形采样数据 (int16, Big-Endian) |
| 0x06 | FAULT_LIST | 故障列表 |

**FLASH_WAVE 帧**: payload = 64 个 int16 点 (128 字节)，Big-Endian 字节序。

**FreeRTOS 输出标记**:
```
[FW_BEG] wave#N              ← 波形会话开始
[FW_DAT p=N ts=T len=L]      ← 每帧标记行
HEXHEX...                     ← 完整 hex 数据 (紧凑格式)
[FW_END] wave#N pkts=...     ← 波形会话结束+汇总
```

---

## 二、虚拟机端操作 (所有命令在虚拟机执行)

### 2.1 前提条件

```bash
# 确认开发板在线
ping -c 1 192.168.88.10

# 确认 SSH 可用
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 "echo ok"
```

### 2.2 编译和部署固件 (一键)

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

deploy.sh 做的事情:
1. 同步源码到 SDK 编译目录
2. `make clean && make all` 编译固件
3. scp 传输到开发板
4. 检查 remoteproc 状态
5. 更新 `/lib/firmware/openamp_core0.elf`
6. 如果从核离线则启动，运行中则跳过重启 (需要 reboot 才加载新固件)
7. 运行 45 秒 trace_reader 验证

**如果固件已在运行中**，deploy.sh 会提示 "跳过重启"。要让新固件生效：
```bash
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 \
  "echo 'user' | sudo -S reboot"
# 等待 40 秒后重新连接
```

如果开发板重启后 remoteproc 离线，手动启动：
```bash
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 \
  "echo 'user' | sudo -S sh -c 'echo openamp_core0.elf > /sys/class/remoteproc/remoteproc0/firmware && echo start > /sys/class/remoteproc/remoteproc0/state'"
```

### 2.3 接收波形数据并绘图 (一键)

```bash
cd /home/alientek/Phytium/freertos
sshpass -p 'user' ssh -o StrictHostKeyChecking=no user@192.168.88.10 \
  "echo user | sudo -S timeout 60 /home/user/trace_reader 2>/dev/null" > trace_wave.txt
python3 plot_wave.py trace_wave.txt
```

输出文件:
- `trace_wave.txt` — 原始 trace 数据
- `waveform.png` — 波形图 (FLASH_WAVE int16)
- `node_raw.png` — NODE_RAW 数据图 (如果有)

### 2.4 确认数据是否正确接收

```bash
# 检查是否有波形数据
grep -c 'FW_DAT' /home/alientek/Phytium/freertos/trace_wave.txt

# 查看汇总信息
grep 'FW_END' /home/alientek/Phytium/freertos/trace_wave.txt

# 查看数据量
python3 -c "
import re
raw = open('/home/alientek/Phytium/freertos/trace_wave.txt').read()
matches = re.findall(r'\[FW_DAT p=(\d+) ts=(\d+) len=(\d+)\]\r?\n([0-9A-Fa-f]+)', raw)
if matches:
    total_bytes = sum(len(m[3])//2 for m in matches)
    n = total_bytes
    print(f'Packets: {len(matches)}, Samples: {n}, TS: {matches[0][1]}..{matches[-1][1]} ms')
else:
    print('No FW_DAT found — 检查终端是否在发送 type=05 数据')
"
```

### 2.5 解图数据含义

FLASH_WAVE 波形数据是 **Big-Endian int16**，原始采样值。可能代表:
- ADC 原始采样值 (如 12-bit ADC 映射到 int16)
- 或已缩放的物理量 (电流/电压的整型表示)

绘图时 Python 脚本直接绘制原始 int16 值，不做单位转换。如需物理单位，需要在 GD32 侧确认换算关系后修改 plot_wave.py 添加缩放因子。

---

## 三、开发板端操作 (通过串口终端)

### 3.1 清除历史数据并启动监听

```bash
# 步骤1: 重启 FreeRTOS 清空共享内存
echo user | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 1
echo user | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
sleep 3

# 步骤2: 启动监听 (屏幕实时显示 + 同时保存到文件)
sudo /home/user/trace_reader 2>/dev/null | tee /home/user/trace_wave.txt
```

实时输出示例:
```
[FW_BEG] wave#1 (from WAVE header)
=== FRAME #199: type=05 ts=26082 sync=A5A5A5A5 raw=128B ===
[FW_DAT p=0 ts=26082 len=128]
07BA07E0...
[WAVE-STAT] wave#1 pkt#0 pts=64 total=64 lost=0 ts=26082 val=[2312..32248]
...
[FW_END] wave#1 pkts=85 bytes=10880 pts=5440 lost=0 ...
```

### 3.2 确认数据已保存

```bash
grep -c 'FW_DAT' /home/user/trace_wave.txt    # 应 > 0
ls -lh /home/user/trace_wave.txt               # 确认文件大小
```

### 3.3 传输到虚拟机

在**虚拟机**上执行:
```bash
cd /home/alientek/Phytium/freertos
scp user@192.168.88.10:/home/user/trace_wave.txt .
python3 plot_wave.py trace_wave.txt
```

如果网络不通，检查:
```bash
# 开发板上
ping 192.168.88.1
ip addr | grep 192.168
```

---

## 四、目录结构

```
Phytium/freertos/                     ← 核心工作目录
├── main.c                            ★ FreeRTOS 入口, FLASH_WAVE 逐帧输出
├── src/
│   ├── rpmsg-echo_os.c               ★ RPMsg 通信核心
│   ├── master_recv.c                 LoRa 帧接收管线
│   ├── lora_uart.c                   UART2/LoRa 串口驱动移植参考与当前待统一实现点
│   ├── master_judge.c                故障判决
│   └── master_cmd.c                  命令生成
├── inc/
│   ├── data_frame.h                  数据类型定义 (DATA_TYPE_FLASH_WAVE=0x05)
│   ├── master.h                      主控头文件
│   └── rpmsg_proto.h                 RPMsg 协议头
├── deploy.sh                         ★ 一键编译+部署脚本
├── plot_wave.py                      ★ 波形解析+绘图脚本
├── trace_wave.txt                    运行中产生的 trace 数据
└── waveform.png                      生成的波形图

Phytium/docs/                          ← 文档目录
├── architecture.md                   ★ 架构全景图
├── operations-guide.md               ★ 本文件 — 操作手册
├── debug-log.md                      调试日志 (问题+解决方案)
├── communication-flow.md             通信流程详解
├── freertos-task-flow.md             FreeRTOS 任务流程
├── setup-guide.md                    部署指南
├── knowledge-base.md                 知识库
├── lora-real-hardware-接入指南.md    LoRa 硬件接线指南
└── optimization-record.md            性能优化记录
```

---

## 五、常见问题速查

| 问题 | 原因 | 解决 |
|------|------|------|
| trace_reader 无输出 | remoteproc 离线 | `echo start > /sys/class/remoteproc/remoteproc0/state` |
| grep -c FW_DAT 返回 0 | 终端没发 type=05 数据 | 确认 GD32 在发 FLASH_WAVE，检查 trace 中是否有 `type=05` |
| 图中前几个周期毛刺多 | GD32 端缓存/启动瞬态 | 是 GD32 数据本身的问题，非 LoRa 丢包 |
| RPMsg TX FAIL (-2003) | Linux 侧无 RPMsg 接收端点 | 正常现象，trace_reader 通过 /dev/mem 读共享内存，不受影响 |
| scp 拉回的文件是空的 | 网络不通导致 scp 静默失败 | ping 检查网络, 或用 sshpass 一行命令法 |
| 历史数据太多看不清新数据 | 共享内存未清空 | 重启 FreeRTOS 从核清空缓冲区 |
| WAVE-STAT 数值离谱 | FreeRTOS 端序 bug | 不影响 Python 绘图，可忽略 |
| 新固件编译了但不生效 | 从核 running 状态跳过了重启 | 执行 `sudo reboot` 或手动 stop/start |

---

## 六、完整验证清单

部署和测试完成后，确认以下每一项:

- [ ] `bash deploy.sh` 编译成功, ELF 传输成功
- [ ] 开发板 remoteproc 状态为 `running`
- [ ] `trace_reader` 能看到 `[FW_BEG]` 和 `[FW_DAT]` 输出
- [ ] 至少收到 10 帧以上 FLASH_WAVE 数据 (p 序号连续)
- [ ] `grep -c FW_DAT trace_wave.txt` > 10
- [ ] `python3 plot_wave.py trace_wave.txt` 无报错
- [ ] `waveform.png` 文件存在且 > 50KB
- [ ] 图中波形无明显异常 (短暂毛刺除外，见问题 27)
- [ ] `[FW_END]` 汇总信息中 `lost=0`

---

## 七、UKF 状态估计 Dashboard (Web 可视化)

> **推荐版本**: `Phytium/dashboard_board/`  
> **旧版保留**: `Phytium/state_estimation/`  
> **功能**: 基于 UKF (无迹卡尔曼滤波) 的微电网状态估计实时可视化大屏

### 7.1 推荐版本 — dashboard_board

`dashboard_board/` 是当前推荐使用的监控大屏，支持 IEEE 9-Bus 三节点轮播、故障回放、飞书/微信推送、VNC 桌面展示。

#### 7.1.1 功能特性

- **三节点轮播显示**: node_1 / node_2 / node_3 自动切换，每节点 2~3s 活跃窗口
- **实时状态估计**: 展示 3 台发电机的转子角度和转速估计值
- **真实值对比**: 估计曲线与真实值曲线同步显示
- **故障时段标记**: 三节点独立故障（step 9-11/12-14/15-17），面板曲线高亮变红
- **灾害仿真与天气风险**: 自然灾害模拟 + 心知天气 API 风险预警
- **通知推送**: 飞书自定义机器人 + 微信 Server酱
- **VNC 桌面**: 飞腾派直连大屏，1024x600 轻量部署

#### 7.1.2 快速启动

VM 端生成数据并部署：

```bash
cd /home/alientek/Phytium/dashboard_board
python3 prep/prepare_data.py
bash scripts/deploy_to_board.sh
```

浏览器访问：

```
http://192.168.88.10:5000
```

VNC 桌面：

```
192.168.88.10:5902  密码: user123
```

> 详见 [dashboard_board/README.md](../dashboard_board/README.md) 和 [dashboard_board/DEPLOY.md](../dashboard_board/DEPLOY.md)。

#### 7.1.3 文件结构

```
Phytium/dashboard_board/
├── README.md / DEPLOY.md / 需求开发文档.md
├── config.json                  # 系统配置（节点开关、Webhook、天气 key）
├── data/                        # 预计算数据、节点数据
├── prep/
│   ├── prepare_data.py          # 一键生成 dashboard_data.json
│   └── build_data_1000hz.py     # 三节点 1000Hz 数据构建
├── server/
│   ├── dashboard_server.py      # Flask 主服务
│   ├── heartbeat_source.py      # 心跳源
│   ├── feishu_notifier.py       # 飞书推送
│   └── wechat_notifier.py       # 微信推送
├── templates/dashboard.html     # Chart.js 大屏前端
├── scripts/
│   ├── deploy_to_board.sh       # VM → 飞腾派一键部署
│   ├── start.sh / stop.sh / status.sh
│   └── start_vnc.sh
├── systemd/                     # systemd 双服务配置
└── tools/                       # C UKF 二进制（x86_64 + aarch64）
```

#### 7.1.4 注意事项

- Dashboard 当前使用 `dashboard_board/data/` 的**预计算数据**驱动。
- 实际部署时需要接入 LoRa→FreeRTOS→Linux 的真实开发板数据（Task 1 与 Task 2 尚未打通）。
- 所有日志写入 stdout，历史数据存内存，避免 TF 卡高频擦写。

---

### 7.2 旧版保留 — state_estimation

`state_estimation/` 是早期 UKF Dashboard 版本，依赖 `state_new/` 的模拟数据，当前仅作保留参考，不再推荐用于生产演示。

```bash
cd /home/alientek/Phytium/state_estimation
python dashboard_server.py
```

服务启动后访问: **http://localhost:5000**

#### 7.2.1 文件结构

```
Phytium/state_estimation/
├── dashboard_server.py          # Flask 服务端 + UKF 引擎
├── templates/dashboard.html     # Web 前端 (Chart.js)
├── ukf_estimation.py 等算法文件
└── README_使用说明.txt

Phytium/state_new/
├── initialize_system.py         # 系统参数与故障时间配置
├── terminal_node.py             # 生成节点测量数据和 true_states.csv
├── node1/2/3_measurements.txt   # 节点测量数据
├── true_states.csv              # 真实状态参考数据
└── system_params.mat            # 系统参数
```

#### 7.2.2 注意事项

- 使用 `state_new/` 的模拟数据进行演示。
- 当前非推荐版本，新功能开发请在 `dashboard_board/` 进行。