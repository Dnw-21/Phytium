# IoT Monitoring System (物联网监测系统)

## 📋 项目概述

基于飞腾派开发板的物联网监测系统，包含传感器数据采集、终端节点管理、LoRa通信、状态估计和UI界面等功能。

### 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                     主控端 (Linux + RTOS)                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ 状态估计  │  │ 数据存储  │  │ UI界面   │  │ 安防监控  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│  ┌─────────────────────────────────────────────────────┐    │
│  │           LoRa通信 + 数据解析 (RTOS)                 │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                              ↕ LoRa
┌─────────────────────────────────────────────────────────────┐
│                   终端节点 (RTOS)                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ 传感器采集│  │ 异常判断  │  │ 数据上报  │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

## 🎯 当前阶段：环境搭建与应用层验证

**目标**: 在飞腾派开发板上完成基础环境搭建，实现LED控制示例，跑通"修改代码→编译→部署→运行→调试"完整流程。

## 📁 项目结构

```
iot-monitoring-system/
├── src/                          # 源代码
│   ├── linux-app/               # Linux应用层（当前重点）
│   │   ├── main.c              # 主程序入口
│   │   ├── led_control.c/h     # LED控制模块
│   │   └── ...
│   ├── common/                  # 公共工具函数
│   └── drivers/                 # 驱动相关代码
├── config/                      # 配置文件
│   └── config.json             # 系统配置（IP、LED、传感器参数）
├── docs/                        # 文档
│   ├── design/                  # 设计文档
│   ├── api/                     # API文档
│   └── debug-logs/              # 调试日志
├── scripts/                     # 自动化脚本
│   └── deploy.sh               # 远程部署脚本
├── tests/                       # 测试代码
├── build/                       # 编译输出目录
├── CMakeLists.txt              # 构建配置
└── README.md                    # 本文档
```

## 🔧 快速开始

### 前置条件

- 飞腾派开发板已连接网络（IP: `192.168.88.11`）
- 开发机安装: `cmake`, `gcc`, `g++`, `rsync`, `ssh`
- SSH免密登录或已知密码（默认: `alientek`）

### 1️⃣ 克隆/进入项目目录

```bash
cd /home/alientek/iot-monitoring-system
```

### 2️⃣ 编译项目

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

或者使用脚本：
```bash
chmod +x scripts/deploy.sh
./scripts/deploy.sh build
```

### 3️⃣ 部署到飞腾派

```bash
./scripts/deploy.sh deploy
```

### 4️⃣ 在板子上运行LED控制示例

```bash
# 方式1: 使用脚本一键运行
./scripts/deploy.sh full

# 方式2: SSH到板子手动执行
./scripts/deploy.sh shell
# 然后在板子上执行:
cd /home/root/iot-monitoring-system
./build/iot-main led-blue --blink --interval 500 --count 5

# 方式3: 远程直接运行命令
./scripts/deploy.sh run "led-blue --on"
./scripts/deploy.sh run "led-red --blink --interval 200 --count 10"
```

### 5️⃣ 调试

```bash
# GDB远程调试
./scripts/deploy.sh debug

# 查看实时日志
./scripts/deploy.sh logs
```

## 💡 LED控制示例说明

本示例演示了如何在飞腾派的Linux应用层控制LED灯：

### 支持的命令

```bash
# 打开LED
./iot-main led-blue --on

# 关闭LED
./iot-main led-blue --off

# LED闪烁（500ms间隔，闪烁5次）
./iot-main led-blue --blink

# 自定义闪烁参数
./iot-main led-red --blink --interval 200 --count 10
```

### 实现原理

- 通过Linux sysfs接口 (`/sys/class/leds/`) 控制GPIO
- 支持信号处理（Ctrl+C优雅退出）
- 包含错误处理和日志输出

## 🔌 可用的LED设备

根据 [config.json](config/config.json) 配置：

| LED名称 | 说明 |
|---------|------|
| `led-blue` | 蓝色LED（默认） |
| `led-red` | 红色LED |
| `led-green` | 绿色LED |

> **注意**: 实际可用LED取决于硬件设计，可通过 `ls /sys/class/leds/` 查看

## 🛠️ 开发工作流

```
修改代码 → 本地编译 → 部署到板 → 运行测试 → 调试问题 → 提交代码
    ↓          ↓         ↓         ↓         ↓         ↓
  编辑器    make/cmake  rsync     SSH      GDB/log   git
```

### 典型调试流程

1. **修改代码**: 编辑 `src/linux-app/led_control.c`
2. **本地编译**: `./scripts/deploy.sh build`
3. **部署**: `./scripts/deploy.sh deploy`
4. **运行测试**: `./scripts/deploy.sh run "led-blue --blink"`
5. **查看日志**: `./scripts/deploy.sh logs`
6. **如需GDB调试**: `./scripts/deploy.sh debug`

## 📊 后续开发计划

### Phase 1: 基础验证 ✅ (当前)
- [x] 环境搭建
- [x] LED控制示例
- [ ] 跑通完整开发流程

### Phase 2: 传感器集成
- [ ] ADC电压/电流采集驱动
- [ ] 数据校准与滤波
- [ ] 传感器异常检测

### Phase 3: 终端节点通信
- [ ] LoRa模块驱动
- [ ] 数据帧协议定义
- [ ] 正常/预警/紧急三模式切换

### Phase 4: 主控功能
- [ ] 状态估计算法
- [ ] 天气API集成
- [ ] Web UI界面
- [ ] 数据存储（本地+云端）

### Phase 5: 系统优化
- [ ] 功耗管理
- [ ] 安全加密
- [ ] 故障自恢复

## 🔧 配置说明

主要配置文件: [config/config.json](config/config.json)

可配置项：
- **board**: 开发板连接信息
- **led**: LED设备列表和路径
- **lora**: LoRa通信参数
- **sensors**: 传感器通道和阈值
- **logging**: 日志级别和存储策略
- **communication**: 通信间隔时间

## 📝 调试日志

所有调试日志保存在 `docs/debug-logs/debug.log`

日志格式:
```
[2026-05-03 10:30:15] [INFO] LED led-blue initialized (fd=3)
[2026-05-03 10:30:15] [DEBUG] LED set to ON
[2026-05-03 10:30:20] [WARN] Sensor voltage exceeds threshold: 225V
```

## 🤝 团队分工

| 成员 | 负责模块 | 平台 |
|------|---------|------|
| 你 | 主控Linux应用层、环境搭建 | 飞腾派 (aarch64) |
| 成员A | 终端节点RTOS | GD32 |
| 成员B | 通信协议、LoRa | - |
| 成员C | UI界面、Web服务 | Linux |

## 📌 常用命令速查

```bash
# 构建
./scripts/deploy.sh build

# 部署
./scripts/deploy.sh deploy

# 运行（带参数）
./scripts/deploy.sh run "led-blue --blink"

# 一键构建+部署+运行
./scripts/deploy.sh full

# SSH登录板子
./scripts/deploy.sh shell

# GDB调试
./scripts/deploy.sh debug

# 查看日志
./scripts/deploy.sh logs

# 清理构建文件
rm -rf build/*
```

## ⚠️ 注意事项

1. **首次使用前**请确认：
   - 飞腾派IP地址正确（`ping 192.168.88.11`）
   - SSH连接正常（`ssh root@192.168.88.11`）
   - 已安装依赖：`sudo apt install cmake gcc g++ rsync`

2. **LED操作需要root权限**，脚本会自动以root身份SSH连接

3. **修改配置后**需重新部署才能生效

4. **建议在虚拟机中开发**，通过rsync同步到开发板

## 📚 相关资源

- [飞腾派官方文档](https://docs.phytium.com.cn/)
- [Linux sysfs GPIO编程](https://www.kernel.org/doc/Documentation/gpio/sysfs.txt)
- [LoRa通信协议规范](https://lora-alliance.org/)

## 📄 许可证

MIT License

---

**最后更新**: 2026-05-03  
**版本**: v1.0.0-alpha  
**维护者**: [你的名字]
