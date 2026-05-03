# Phytium CEK8903 项目信息汇总

## 一、项目基本信息

**项目名称**: Phytium  
**开发板型号**: 飞腾派 CEK8903 (Phytium Pi)  
**项目路径**: `/home/alientek/Phytium`  
**创建日期**: 2026-05-03  

---

## 二、开发板硬件信息

### 2.1 基本规格
- **芯片**: FT-2004 四核处理器
- **架构**: ARM64 (aarch64)
- **系统**: Ubuntu 22.04 LTS (定制版)
- **内存**: 4GB DDR4
- **存储**: eMMC + SD卡支持

### 2.2 网络配置
- **IP地址**: `192.168.88.11`
- **连接方式**: SSH远程连接
- **网段**: 192.168.88.x

### 2.3 用户账户信息
| 角色 | 用户名 | 密码 | 用途 |
|------|--------|------|------|
| 开发板用户 | user | user | SSH登录、程序执行 |
| 虚拟机用户 | alientek | 123456 | 本地开发环境 |

---

## 三、系统环境验证结果

### 3.1 已安装工具
```bash
✓ gcc/g++ 编译器 (已安装)
✓ make 构建工具 (已安装)
✓ sshpass 自动密码输入 (已安装)
✓ rsync 文件同步 (已安装)
```

### 3.2 硬件接口状态

#### LED系统
**可访问的LED节点:**
```
/sys/class/leds/sysled
  - GPIO编号: 485
  - 类型: 内部状态指示灯（不可见）
  - 控制方式: sysfs (/sys/class/leds/sysled/brightness)
  - 触发器: [none] heartbeat cpu0 cpu1 default-on
```

**可见LED（原理图标识）:**
- `LED_WPAN#` - WPAN状态灯
- `LED_WLAN#` - WLAN状态灯  
- `LED_WWAN#` - WWAN状态灯
- **注意**: 这三个LED不在Linux LED子系统中，无法通过sysfs控制，可能由底层驱动或固件直接管理

**调试结论:**
- ❌ 无法通过软件控制可见LED
- ✅ `sysled`可控制但为内部LED，物理位置不可见
- 💡 板载两颗常亮绿色LED可能为电源指示灯（硬件层面）

#### 温度传感器
**可访问的hwmon节点:**
```
/sys/class/hwmon/hwmon0/
├── name: "fts_soc_thermal"
├── temp1_input → TS0 (SoC温度)
├── temp1_label: "TS0"
├── temp2_input → TS1 (PCB温度)  
├── temp2_label: "TS1"
```

**测试数据:**
- TS0 (SoC): ~27.00°C
- TS1 (PCB): ~27.00°C
- 数据格式: 毫摄氏度 (27000 = 27.00°C)

#### GPIO系统
- **sysfs GPIO**: 未启用（/sys/class/gpio目录不存在）
- **直接寄存器访问**: 可通过devmem2访问物理地址
- **GPIO控制器基地址**: 0x28018000 (需查阅数据手册确认)

---

## 四、开发资料文档

### 4.1 官方文档（已下载）
| 文档名称 | 文件路径 | 内容说明 |
|---------|---------|---------|
| OS用户使用开发手册 v1.3 | `/docs/飞腾派OS用户使用开发手册+v1.3.pdf` | 操作系统使用指南 |
| 软件开发手册 V1.01 | `/docs/萤火工场·CEK8903飞腾派软件开发手册-V1.01.pdf` | 软件开发API和示例 |
| 硬件规格书 V3.0 | `/docs/萤火工场·CEK8903飞腾派V3.0硬件规格书.pdf` | 硬件参数、引脚定义 |
| 原理图 | `/docs/原理图CEK8903_v3.0_sch.pdf` | 电路设计、LED/GPIO连接 |
| SDK使用说明 v1.0 | `/home/alientek/Phytium_syscode/phytium-pi+sdk使用说明v1.0.pdf` | Phytium Pi SDK详细使用指南 |

### 4.2 在线资源
**官方论坛:**
- 论坛必读帖子: https://edu.phytium.com.cn/group/12/thread/170
- 芯查查-飞腾派论坛: https://www.xcc.com/planet/sectors/48
- 电子发烧友-飞腾派论坛: https://bbs.elecfans.com/group_1708

**OpenAMP异构多核通信（重要）:**
- 📘 **OpenAMP手册和例程**: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
  - 内容：OpenAMP协议说明、API文档、示例代码、多核通信机制
  - 用途：Linux与RTOS核间通信、数据共享、资源管理
  
- 🚀 **OpenAMP部署教程**: https://www.iceasy.com/news/1950487026519457794
  - 内容：环境配置、编译步骤、部署流程、调试方法
  - 适用场景：需要利用飞腾派多核架构进行异构计算时参考

---

## 四点五、系统源码与编译工具链

### 4.5.1 源码目录结构
```
/home/alientek/Phytium_syscode/
├── 内核源码/
│   └── kernel-5.10.209-phytium-embedded-v2.2.tar.gz  (188MB, 已压缩)
│       └── 说明：飞腾定制版Linux内核源码（版本5.10.209）
│          用途：内核驱动开发、内核裁剪、功能定制
│
├── 设备树/
│   └── 5.10.209/
│       ├── 5.10.209-phytium-embedded-v2.2.tar.gz    (125MB, 源码压缩包)
│       ├── Image                                      (22MB, 编译好的内核镜像)
│       ├── phytium-pi-board-v2.dtb                   (31KB, V2版设备树)
│       └── phytium-pi-board-v3.dtb                   (31KB, V3版设备树)
│           └── 说明：硬件描述文件，定义GPIO/LED/外设等资源映射
│              用途：查看开发板硬件资源、修改设备配置
│
├── GCC编译器/
│   └── gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/
│       └── bin/
│           └── aarch64-none-linux-gnu-gcc            ★ 交叉编译器
│           └── 说明：ARM64交叉编译工具链（GCC 10.2版本）
│              用途：在x86主机上编译飞腾派可执行程序
│
└── phytium-pi+sdk使用说明v1.0.pdf                     (363KB, SDK文档)
    └── 说明：Phytium Pi完整SDK使用指南
       用途：了解SDK结构、API接口、编译流程
```

### 4.5.2 交叉编译器详细信息
**编译器路径**: 
```
/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/
```

**主要工具**:
```bash
# C编译器
aarch64-none-linux-gnu-gcc

# C++编译器
aarch64-none-linux-gnu-g++

# 链接器
aarch64-none-linux-gnu-ld

# 调试器（GDB）
aarch64-none-linux-gnu-gdb

# 其他工具
aarch64-none-linux-gnu-objdump      # 反汇编
aarch64-none-linux-gnu-readelf      # ELF文件分析
aarch64-none-linux-gnu-strip        # 符号表剥离
aarch64-none-linux-gnu-size         # 段大小统计
```

**使用方法**:
```bash
# 方法1：直接使用绝对路径
export CROSS_COMPILE=/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
${CROSS_COMPILE}gcc -o output main.c

# 方法2：添加到PATH（推荐）
export PATH=$PATH:/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin
aarch64-none-linux-gnu-gcc -o output main.c

# 方法3：写入~/.bashrc（永久生效）
echo 'export PATH=$PATH:/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin' >> ~/.bashrc
source ~/.bashrc
```

**Makefile中使用交叉编译**:
```makefile
CROSS_COMPILE = /home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -march=armv8-a

all:
	$(CC) $(CFLAGS) -o app main.c
```

### 4.5.3 设备树文件说明
**当前可用设备树**:
| 文件名 | 大小 | 版本 | 说明 |
|--------|------|------|------|
| `phytium-pi-board-v2.dtb` | 31KB | V2 | 早期版本硬件描述 |
| `phytium-pi-board-v3.dtb` | 31KB | V3 | 当前版本硬件描述（推荐使用） |

**设备树用途**:
- ✅ 查看开发板所有硬件资源的寄存器地址和中断号
- ✅ 了解LED、GPIO、UART、SPI、I2C等外设的配置
- ✅ 修改硬件资源配置（需重新编译dtb）
- ✅ 调试驱动问题时对照硬件定义

**查看设备树内容**（需要安装dtc工具）:
```bash
# 反编译设备树为可读格式
sudo apt-get install device-tree-compiler
dtc -I dtb -O dts -o phytium-pi-board-v3.dts phytium-pi-board-v3.dtb

# 查看特定节点（如LED）
dtc -I dtb -O dts phytium-pi-board-v3.dtb | grep -A 20 "led"
```

### 4.5.4 内核源码说明
**内核版本**: Linux 5.10.209 (Phytium Embedded v2.2)  
**源码大小**: ~188MB（压缩后）  
**解压后大小**: ~1.2GB  

**解压命令**:
```bash
cd /home/alientek/Phytium_syscode/内核源码
tar -xzf kernel-5.10.209-phytium-embedded-v2.2.tar.gz
```

**内核目录结构（解压后）**:
```
kernel-5.10.209-phytium-embedded-v2.2/
├── arch/arm64/                    # ARM64架构相关代码
│   └── boot/dts/                  # 设备树源文件（.dts）
├── drivers/                       # 驱动程序
│   ├── gpio/                      # GPIO驱动
│   ├── led/                       # LED驱动
│   ├── thermal/                   # 温度传感器驱动
│   └── ...                        # 其他驱动
├── include/                       # 头文件
├── sound/                         # 音频驱动
└── ...                            # 其他内核子系统
```

**何时需要使用内核源码**:
- 🔧 开发或修改内核驱动模块
- 🔧 添加新的硬件支持
- 🔧 调试内核级问题（如LED控制失败的根本原因）
- 🔧 性能优化和内核裁剪
- 🔧 学习Linux内核实现细节

---

## 五、远程部署流程

### 5.1 连接命令
```bash
# SSH连接
ssh user@192.168.88.11

# 使用sshpass免交互（用于脚本）
sshpass -p 'user' ssh user@192.168.88.11 '<command>'
```

### 5.2 文件传输
```bash
# 同步整个项目到开发板
rsync -avz --progress /home/alientek/Phytium/src/linux-app/ user@192.168.88.11:~/Phytium/

# 执行sudo命令（自动输入密码）
echo 'user' | sudo -S <command>
```

### 5.3 部署脚本
**文件**: `scripts/deploy.sh`  
**功能**: 
- 本地编译 (make clean && make)
- 远程同步 (rsync)
- 远程执行 (ssh)
- 支持参数传递

**使用方法:**
```bash
./scripts/deploy.sh              # 默认编译部署运行
./scripts/deploy.sh --once       # 单次执行模式
./scripts/deploy.sh --monitor    # 监控模式
```

---

## 六、调试经验总结

### 6.1 已解决的问题

#### 问题1: SSH sudo密码提示
**现象**: 远程执行sudo命令时反复提示输入密码  
**原因**: ssh执行命令时无法交互式输入密码  
**解决方案**:
```bash
# 方法1: 使用sshpass
echo 'user' | sudo -S <command>

# 方法2: 配置NOPASSWD（推荐生产环境）
visudo → user ALL=(ALL) NOPASSWD: ALL
```

#### 问题2: LED控制无响应
**现象**: 写入brightness值但物理LED无变化  
**排查过程**:
1. ✅ 确认sysled节点存在且可读写
2. ✅ 确认brightness值变化 (0↔255)
3. ❌ 发现sysled是内部状态LED（GPIO 485）
4. ❌ 可见LED(WPAN/WLAN/WWAN)不在sysfs中
5. ❌ 尝试devmem直接操作寄存器未果

**最终结论**: 
- Linux LED子系统仅暴露内部状态灯
- 可见LED由底层驱动/BSP管理，应用层无法直接控制
- 建议：如需LED反馈，考虑外接GPIO控制的LED模块

### 6.2 硬件限制

| 功能 | 状态 | 说明 |
|------|------|------|
| sysfs LED控制 | ⚠️ 受限 | 仅可控制内部sysled |
| 温度传感器读取 | ✅ 可用 | hwmon接口稳定可靠 |
| GPIO sysfs | ❌ 不可用 | 内核未启用CONFIG_GPIO_SYSFS |
| 直接寄存器访问 | ⚠️ 需确认 | 需要详细数据手册 |

### 6.3 性能基准
- **编译速度**: 本地<1秒，交叉编译待测试
- **网络延迟**: 局域网<1ms
- **文件传输速率**: rsync约10MB/s

---

## 七、团队整体方案概览

### 7.1 系统架构
```
[传感器层] → [RTOS终端(GD32)] ←LoRa→ [Linux主控(飞腾派)]
     ↓              ↓                      ↓
 电压/电流      数据采集+异常处理      状态估计/UI/存储
 相角/功率      低功耗(<5mA)          天气API集成
               预警/紧急模式         历史数据分析
```

### 7.2 终端(RTOS)功能模块
1. **数据采集**: 多传感器轮询 + 模拟量采集
2. **异常检测**: 
   - 正常模式: 5-10min定时上传
   - 预警模式: 1s周期连续采集+平均
   - 紧急模式: 100ms高速采集+自适应重传
3. **指令接收**: LoRa通信+任务调度
4. **功耗管理**: 运行期<5mA

### 7.3 主控(Linux)功能模块
1. **状态估计算法**: 输入电压/电流/相角/功率 → 输出系统状态
2. **寿命预测**: 基于电压的终端剩余使用时长
3. **天气集成**: 心知天气API → 自然灾害风险判断
4. **UI界面**: Web/本地GUI
5. **传感器健康监测**: 零点漂移检测+失效预测
6. **数据存储**: 本地DB + 云端备份

### 7.4 通信协议
- **LoRa**: 长距离、低功耗、强穿透
- **对比**: WiFi(高速短距) / Bluetooth(短距配对)

---

## 八、当前项目结构

```
Phytium/
├── src/
│   ├── linux-app/
│   │   ├── main.c              # 主程序入口（已清理）
│   │   └── Makefile            # 构建配置
│   │
│   └── openamp-demo/           # ★ OpenAMP异构多核通信Demo (新增)
│       ├── linux-master/       # Linux主控程序
│       │   └── rpmsg_master.c  # RPMsg主控端代码
│       ├── remote-core/        # 从核固件（裸机程序）
│       │   └── rpmsg_slave.c   # RPMsg从核端代码模板
│       ├── scripts/
│       │   └── deploy.sh       # 一键部署脚本
│       ├── docs/
│       │   └── README.md       # OpenAMP使用文档
│       ├── Makefile            # 构建配置（支持交叉编译）
│       └── build/              # 编译输出
│           └── rpmsg_master    # ARM64可执行文件 (23KB)
│
├── config/
│   └── config.json             # 系统配置（IP/用户等）
├── scripts/
│   └── deploy.sh               # 部署脚本（linux-app用）
├── docs/
│   ├── 飞腾派OS用户使用开发手册+v1.3.pdf
│   ├── 萤火工场·CEK8903飞腾派软件开发手册-V1.01.pdf
│   ├── 萤火工场·CEK8903飞腾派V3.0硬件规格书.pdf
│   └── 原理图CEK8903_v3.0_sch.pdf
├── logs/                       # 调试日志目录
├── README.md                   # 项目说明
├── .gitignore                  # Git忽略规则
└── PROJECT_INFO.md             # 本文件
```

---

## 八点五、OpenAMP 异构多核通信项目

### 8.5.1 项目概述
**目标**: 实现飞腾派 Linux主核 与 裸机从核 之间的数据通信  
**技术**: OpenAMP框架 + RPMsg协议  
**状态**: ✅ 代码已完成，待升级系统后测试

### 8.5.2 系统架构
```
┌─────────────────────────────────────────┐
│            飞腾派 SoC                    │
│                                         │
│  ┌──────────────┐   ┌──────────────┐   │
│  │  Linux 主核   │◄─►│  裸机 从核   │   │
│  │  (Core 0-1)  │   │  (Core 3)    │   │
│  │              │   │              │   │
│  │ rpmsg_master │   │ rpmsg_slave  │   │
│  └──────────────┘   └──────────────┘   │
│         ▲                  ▲           │
│         └──────┬───────────┘           │
│         共享内存 + IPI中断               │
└─────────────────────────────────────────┘
```

### 8.5.3 项目文件清单

| 文件 | 路径 | 说明 | 状态 |
|------|------|------|------|
| 主控程序 | `src/openamp-demo/linux-master/rpmsg_master.c` | Linux端RPMsg客户端 | ✅ 已编译 |
| 从核固件 | `src/openamp-demo/remote-core/rpmsg_slave.c` | 从核端代码模板 | ⏳ 待编译 |
| 构建配置 | `src/openamp-demo/Makefile` | 交叉编译Makefile | ✅ 可用 |
| 部署脚本 | `src/openamp-demo/scripts/deploy.sh` | 一键部署到开发板 | ✅ 可用 |
| 使用文档 | `src/openamp-demo/docs/README.md` | 详细教程和故障排除 | ✅ 完整 |
| 编译输出 | `src/openamp-demo/build/rpmsg_master` | ARM64可执行文件(23KB) | ✅ 已生成 |

### 8.5.4 快速使用

**编译**:
```bash
cd /home/alientek/Phytium/src/openamp-demo
make master          # 只编译Linux主控程序
# 或
make all            # 编译所有目标
```

**部署到开发板**:
```bash
./scripts/deploy.sh  # 一键部署
# 或
make deploy          # 使用Makefile部署
```

**在开发板上运行** (需要PIOSv2.1+):
```bash
# 1. 启动从核
echo start > /sys/class/remoteproc/remoteproc0/state

# 2. 绑定驱动
sudo sh -c 'echo rpmsg_chrdev > \
  /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'

# 3. 运行demo
cd ~/openamp-demo && ./rpmsg_master --count 10 --interval 500

# 4. 停止
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### 8.5.5 当前环境限制

| 检查项 | 当前状态 | 要求 | 解决方案 |
|--------|---------|------|---------|
| 内核版本 | 5.10.153-v2.0 | 6.6.x (PIOSv2.1) | 升级系统镜像 |
| remoteproc设备 | 空目录 | 包含remoteproc0 | 需要新内核 |
| OpenAMP固件 | 不存在 | /lib/firmware/openamp_core0.elf | 编译或获取 |
| 设备树节点 | 缺失 | reserved-memory + homo_rproc | 使用新DTB |

**⚠️ 重要提示**: 
当前开发板运行的系统版本**不支持OpenAMP**。需要：
1. 下载并编译 PIOSv2.1 源码（约10小时编译时间）
2. 烧录新的SD卡镜像
3. 或者等待官方提供预编译的OpenAMP支持镜像

详细步骤请参考：[OpenAMP部署教程](https://www.iceasy.com/news/1950487026519457794)

### 8.5.6 功能特性

**已实现功能**:
- ✅ RPMsg端点创建和管理
- ✅ 消息发送和接收（异步模式）
- ✅ 命令行参数配置（通道名、目标地址、消息数量、间隔）
- ✅ 信号处理（Ctrl+C优雅退出）
- ✅ 错误处理和日志输出
- ✅ 交叉编译支持（ARM64）
- ✅ 一键部署脚本

**程序参数**:
```bash
./rpmsg_master [选项]

选项:
  --channel NAME   通道名称 (默认: rpmsg-openamp-demo-channel)
  --dst ADDR       目标地址 (默认: 0)
  --count N        发送次数 (默认: 100)
  --interval MS    发送间隔毫秒 (默认: 100)
  --help,-h        显示帮助信息
```

**预期输出示例**:
```
╔══════════════════════════════════════════╗
║   OpenAMP Master Demo - 飞腾派 CEK8903    ║
╚══════════════════════════════════════════╝

[MASTER] 打开设备: /dev/rpmsg0
[MASTER] ✅ 端点创建成功！

[SEND #001] Hello World! No:1
[RECV      ] [SLAVE ACK] 收到: Hello World! No:1
[SEND #002] Hello World! No:2
[RECV      ] [SLAVE ACK] 收到: Hello World! No:2
...
[MASTER] ✅ 测试完成！共发送/接收 100 条消息
```

---

## 九、后续开发建议

### 9.1 推荐的开发顺序
1. **基础框架搭建** - 完成项目骨架和构建系统
2. **LoRa通信模块** - 实现与RTOS终端的数据交换
3. **数据解析层** - 定义协议格式+解包逻辑
4. **状态估计算法** - 核心业务逻辑实现
5. **UI界面开发** - Web前端或本地GUI
6. **云服务集成** - 数据存储+API对接

### 9.2 技术选型建议
| 模块 | 推荐技术 | 备注 |
|------|---------|------|
| 构建系统 | CMake | 跨平台支持好 |
| 日志库 | spdlog/zlog | 高性能C++日志 |
| JSON解析 | cJSON/nlohmann | 轻量级 |
| HTTP客户端 | libcurl | 天气API调用 |
| 数据库 | SQLite | 嵌入式场景 |
| UI框架 | Qt/Web (Flask) | 根据需求选择 |

### 9.3 注意事项
- ⚠️ GPIO操作需要root权限或特定组权限
- ⚠️ 生产环境应配置SSH密钥认证替代密码
- ⚠️ 建议使用Git进行版本控制并定期提交
- ⚠️ 交叉编译工具链：`aarch64-linux-gnu-gcc`
- ⚠️ 调试时建议开启详细日志(-DDEBUG宏)

---

## 十、常用命令速查

### 10.1 开发环境
```bash
# 编译项目
cd /home/alientek/Phytium/src/linux-app
make clean && make

# 运行本地测试
./build/iot-main

# 远程部署
cd /home/alientek/Phytium
./scripts/deploy.sh
```

### 10.2 开发板调试
```bash
# 查看系统信息
ssh user@192.168.88.11 'uname -a && cat /etc/os-release'

# 查看LED状态
ssh user@192.168.88.11 'cat /sys/class/leds/*/brightness'

# 读取温度
ssh user@192.168.88.11 'cat /sys/class/hwmon/hwmon0/temp*_input'

# 查看GPIO（如果可用）
ssh user@192.168.88.11 'ls -la /sys/class/gpio/'

# 查看内核日志
ssh user@192.168.88.11 'dmesg | grep -i led'
```

### 10.3 故障排除
```bash
# 测试SSH连接
sshpass -p 'user' ssh -o ConnectTimeout=5 user@192.168.88.11 'echo OK'

# 检查端口连通性
ping 192.168.88.11 -c 3

# 查看磁盘空间
ssh user@192.168.88.11 'df -h'

# 查看进程
ssh user@192.168.88.11 'ps aux | grep iot'
```

---

## 十一、版本历史

| 版本 | 日期 | 修改内容 |
|------|------|---------|
| v1.0 | 2026-05-03 | 初始创建，完成环境搭建和基础调试 |
| v1.1 | 2026-05-03 | 清理LED/温湿度代码，重命名为Phytium，添加本文档 |
| v1.2 | 2026-05-03 | 添加系统源码路径、GCC交叉编译器、设备树、OpenAMP资源链接 |
| **v1.3** | **2026-05-03** | **新增OpenAMP异构多核通信Demo（完整代码+文档）** |

---

**文档维护者**: AI Assistant  
**最后更新**: 2026-05-03  
**适用阶段**: OpenAMP开发阶段  
**权限设置**: 已启用 requires_approval=false (无需确认执行命令)
