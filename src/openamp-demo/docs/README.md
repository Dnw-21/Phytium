# OpenAMP 异构多核通信 Demo

## 📖 项目简介

本项目实现了飞腾派 CEK8903 开发板上 **Linux主核** 与 **RTOS从核（裸机）** 之间的异构多核通信功能。

基于 **OpenAMP (Open Asymmetric Multi-Processing)** 框架，通过 RPMsg 协议实现双方的数据交换。

### 系统架构

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
│            共享内存 (Shared Memory)      │
│            中断机制 (IPI)               │
└─────────────────────────────────────────┘
```

## 🔧 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| 多核框架 | OpenAMP v2021.10 | 异构多处理标准框架 |
| 通信协议 | RPMsg | 基于VirtIO的消息传递 |
| 共享内存 | libmetal | 底层硬件抽象层 |
| Linux端 | C11 + ioctl | 用户空间RPMsg接口 |
| 从核端 | C11 + OpenAMP API | 裸机程序 |
| 编译器 | aarch64-none-linux-gnu-gcc | ARM64交叉编译 |

## 📁 项目结构

```
openamp-demo/
├── linux-master/          # Linux主控程序
│   └── rpmsg_master.c     # 主控端代码
│
├── remote-core/           # 从核固件
│   └── rpmsg_slave.c      # 从核端代码（模板）
│
├── scripts/
│   └── deploy.sh          # 一键部署脚本
│
├── docs/
│   └── README.md          # 本文档
│
├── Makefile               # 构建配置
└── build/                 # 编译输出（构建后生成）
    ├── rpmsg_master       # Linux可执行文件
    └── rpmsg_slave.elf    # 从核固件文件
```

## ⚙️ 环境要求

### 必要条件

1. **操作系统**: PIOSv2.1 (Phytium-Pi-OS v2.1)
   - 内核版本: Linux 6.6.x
   - 必须包含OpenAMP支持
   
2. **硬件**: 飞腾派 CEK8903 开发板
   - 4核处理器 (2×FTC664 + 2×FTC310)
   
3. **交叉编译工具链**
   ```
   /home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/
   ```

4. **内核配置项**（必须启用）
   ```
   CONFIG_REMOTEPROC=y
   CONFIG_RPMSG=y
   CONFIG_RPMSG_CHAR=y
   ```

### 检查当前系统是否支持

在开发板上执行：
```bash
# 检查remoteproc设备
ls /sys/class/remoteproc/

# 检查OpenAMP固件
ls /lib/firmware/openamp_core0.elf

# 检查设备树节点
ls /sys/firmware/devicetree/base/reserved-memory/
```

如果以上检查都失败，说明当前系统**不支持OpenAMP**，需要升级到PIOSv2.1。

## 🚀 快速开始

### 1. 编译项目

```bash
cd /home/alientek/Phytium/src/openamp-demo

# 只编译Linux主控程序
make master

# 或编译所有目标（包括从核）
make all

# 查看帮助信息
make help
```

### 2. 部署到开发板

```bash
# 使用部署脚本（推荐）
chmod +x scripts/deploy.sh
./scripts/deploy.sh

# 或手动部署
make deploy
```

### 3. 在开发板上启动测试

#### 步骤A: 启动远程处理器（从核）

```bash
# SSH登录开发板
ssh user@192.168.88.11

# 启动从核（会加载openamp_core0.elf固件）
echo start > /sys/class/remoteproc/remoteproc0/state

# 查看状态（应该显示 running）
cat /sys/class/remoteproc/remoteproc0/state
```

**预期输出**:
```
I/TC: Secondary CPU 3 initializing...
I/TC: Secondary CPU 3 switching to normal world boot...
openamp lib version: v2021.10 ...
cpu1: SLAVE_00:Starting application...
cpu1: SLAVE_00:Successfully created rpmsg endpoint.
```

#### 步骤B: 绑定RPMsg通道

```bash
# 绑定字符设备驱动
sudo sh -c 'echo rpmsg_chrdev > \
  /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'

# 查看设备文件（应该出现 /dev/rpmsg0）
ls -la /dev/rpmsg*
```

#### 步骤C: 运行Demo程序

```bash
# 进入程序目录
cd ~/openamp-demo

# 方式1: 默认参数运行（发送100条消息，间隔100ms）
./rpmsg_master

# 方式2: 自定义参数运行
./rpmsg_master --count 50 --interval 200

# 方式3: 快速测试（10条消息，500ms间隔）
./rpmsg_master --count 10 --interval 500
```

**预期输出**:
```
╔══════════════════════════════════════════╗
║   OpenAMP Master Demo - 飞腾派 CEK8903    ║
╚══════════════════════════════════════════╝

[CONFIG] 通道名称: rpmsg-openamp-demo-channel
[CONFIG] 目标地址: 0
[CONFIG] 发送次数: 100
[CONFIG] 发送间隔: 100 ms

[MASTER] 打开设备: /dev/rpmsg0
[MASTER] 创建端点: name=rpmsg-openamp-demo-channel, src=ANY, dst=0
[MASTER] ✅ 端点创建成功！

[MASTER] 开始通信测试...

[SEND #001] Hello World! No:1
[RECV      ] [SLAVE ACK] 收到: Hello World! No:1
[SEND #002] Hello World! No:2
[RECV      ] [SLAVE ACK] 收到: Hello World! No:2
...
```

#### 步骤D: 停止测试

```bash
# 方法1: Ctrl+C 停止demo程序

# 方法2: 停止远程处理器（从核）
echo stop > /sys/class/remoteproc/remoteproc0/state

# 验证已停止
cat /sys/class/remoteproc/remoteproc0/state
# 输出: offline
```

## 📝 命令行参数

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `--channel NAME` | RPMsg通道名称 | `rpmsg-openamp-demo-channel` | `--channel my-ch` |
| `--dst ADDR` | 目标地址 | `0` | `--dst 1` |
| `--count N` | 发送消息数量 | `100` | `--count 50` |
| `--interval MS` | 发送间隔(毫秒) | `100` | `--interval 500` |
| `--help`, `-h` | 显示帮助信息 | - | `-h` |

## 🔍 故障排除

### 问题1: 无法打开 /dev/rpmsg0

**错误信息**:
```
[ERROR] 无法打开 /dev/rpmsg0: No such file or directory
```

**解决方案**:
```bash
# 1. 确认远程处理器已启动
cat /sys/class/remoteproc/remoteproc0/state
# 应该显示: running

# 2. 如果是offline，先启动
echo start > /sys/class/remoteproc/remoteproc0/state

# 3. 绑定通道驱动
sudo sh -c 'echo rpmsg_chrdev > \
  /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'

# 4. 再次检查设备
ls -la /dev/rpmsg*
```

### 问题2: 创建端点失败

**错误信息**:
```
[ERROR] 创建端点失败: Invalid argument
```

**原因**: 从核未正确响应或通道名不匹配

**解决方案**:
```bash
# 1. 查看dmesg日志
dmesg | grep -i rpmsg

# 2. 重启远程处理器
echo stop > /sys/class/remoteproc/remoteproc0/state
sleep 2
echo start > /sys/class/remoteproc/remoteproc0/state

# 3. 确认通道名称一致（主控和从核必须相同）
```

### 问题3: 远程处理器状态为crashed

**解决方案**:
```bash
# 1. 停止处理器
echo stop > /sys/class/remoteproc/remoteproc0/state

# 2. 查看崩溃日志
dmesg | tail -50

# 3. 重新启动
echo start > /sys/class/remoteproc/remoteproc0/state
```

### 问题4: 编译错误 - 找不到头文件

**错误信息**:
```
fatal error: sys/ioctl.h: No such file or directory
```

**解决方案**:
```bash
# 确认使用正确的交叉编译器
export PATH=$PATH:/home/alientek/Phytium_syscode/GCC编译器/\
gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin

# 清理并重新编译
make clean && make
```

## 📊 性能参考指标

| 指标 | 典型值 | 说明 |
|------|--------|------|
| 单次消息延迟 | <1ms | 共享内存直接读写 |
| 吞吐量 | >10MB/s | 取决于消息大小 |
| CPU开销 | <1% | 低开销设计 |
| 内存占用 | ~1MB | 共享内存区域 |

## 🎯 扩展方向

1. **数据协议封装**
   - 添加JSON/二进制协议支持
   - 实现数据校验和重传机制

2. **实时性优化**
   - 使用RT-Preempt补丁
   - 优先级继承协议

3. **安全增强**
   - 消息加密传输
   - 访问控制列表

4. **应用场景**
   - 传感器数据采集（从核采集→主核处理）
   - 实时控制（主核决策→从核执行）
   - 任务卸载（计算密集型任务分配给从核）

## 📚 参考资料

- [OpenAMP官方文档](https://github.com/OpenAMP/open-amp/wiki)
- [飞腾派OpenAMP手册](https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp)
- [飞腾派OpenAMP部署教程](https://www.iceasy.com/news/1950487026519457794)
- [RPMsg协议说明](https://docs.kernel.org/driver-api/rpmsg.html)

## 📄 许可证

MIT License

---

**版本**: v1.0  
**最后更新**: 2026-05-03  
**适用平台**: 飞腾派 CEK8903 (PIOSv2.1+)
