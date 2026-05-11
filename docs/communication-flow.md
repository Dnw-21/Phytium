# OpenAMP 异构多核通信流程详解

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                    Phytium PE2204 SoC                            │
│                                                                  │
│  ┌─────────────────────────┐    ┌─────────────────────────────┐ │
│  │   Linux 主核 (Core 0-1)  │    │   裸机从核 (Core 3)         │ │
│  │   FTC664 @ 1.8GHz       │    │   FTC310 @ 1.5GHz           │ │
│  │                         │    │                             │ │
│  │  rpmsg-demo-single      │    │  openamp_core0.elf          │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  /dev/rpmsg0 (ioctl)    │    │  RPMsg endpoint             │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  rpmsg_char.ko          │    │  OpenAMP lib                │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  virtio_rpmsg_bus       │    │  virtio (vring)             │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  rproc-virtio           │    │  libmetal                   │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  homo_remoteproc        │    │  PSCI CPU_ON                │ │
│  └──────────┬──────────────┘    └──────────────┬──────────────┘ │
│             │                                  │                │
│             │  ┌──────────────────────────┐    │                │
│             └──│   共享内存 (0xB0100000)    │────┘                │
│                │   - vring0 (TX)           │                     │
│                │   - vring1 (RX)           │                     │
│                │   - RPMsg buffers         │                     │
│                │   - 固件代码+数据          │                     │
│                └──────────────────────────┘                     │
│                                                                  │
│             ┌──────────────────────────┐                        │
│             │   IPI 中断 (GICv3 SGI 9) │                        │
│             │   Linux ←→ 从核 通知     │                        │
│             └──────────────────────────┘                        │
└─────────────────────────────────────────────────────────────────┘
```

## 2. 通信流程 (Linux → 从核 → Linux)

### 2.1 启动阶段

```
Step 1: Linux 启动
  └── 内核解析设备树 → 发现 homo_rproc 节点
      └── homo_remoteproc 驱动 probe
          ├── 解析 reserved-memory (0xB0100000, 409MB)
          ├── 映射共享内存为可执行 (PAGE_KERNEL_EXEC)
          ├── 注册 SGI 9 中断处理
          ├── 注册 CPU hotplug 回调
          └── rproc_add() → /sys/class/remoteproc/remoteproc0

Step 2: 用户启动从核
  $ echo start > /sys/class/remoteproc/remoteproc0/state
  └── homo_rproc_start()
      ├── remove_cpu(3)            # 下电 CPU3
      ├── 加载 openamp_core0.elf 到 0xB0100000
      ├── 刷新 I/D-Cache
      └── PSCI CPU_ON → CPU3 从 0xB0100000 开始执行

Step 3: 从核初始化
  └── openamp_core0.elf 启动
      ├── 初始化 libmetal (硬件抽象层)
      ├── 初始化 OpenAMP (virtio, RPMsg)
      ├── 创建 RPMsg 端点 "rpmsg-openamp-demo-channel"
      └── 等待主核消息

Step 4: RPMsg 通道建立
  └── virtio_rpmsg_bus 检测到从核端点
      └── 创建设备: /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0

Step 5: 用户绑定驱动
  $ echo rpmsg_chrdev > .../driver_override
  $ echo virtio0... > .../rpmsg_chrdev/bind
  └── /dev/rpmsg0 创建
```

### 2.2 数据发送 (Linux → 从核)

```
Linux 用户程序                     内核                        从核 (Core 3)
─────────────                     ────                        ────────────
write(fd, "Hello", 6)
  └── rpmsg_char.ko
      └── virtio_rpmsg_bus
          └── 将数据写入 vring0
               (共享内存中的
                virtqueue)
              └── homo_rproc_kick()
                  └── GICv3 SGI 9 → 从核
                                        ──→ GIC 中断
                                             └── IRQ handler
                                                 └── rpmsg_recv()
                                                     └── 处理 "Hello"
                                                     └── rpmsg_send("Hello")
                                                         └── 写入 vring1
                                                         └── IPI → Linux
  ←── GIC 中断
  ←── rproc_vq_interrupt()
  ←── virtio_rpmsg_bus
  ←── rpmsg_char.ko
read(fd, buf, 512) ← "Hello"
```

### 2.3 关键数据路径

```
发送路径: 用户程序 → write() → /dev/rpmsg0 → rpmsg_char → virtio_rpmsg_bus
          → vring (共享内存) → SGI 9 → 从核 → rpmsg_recv()

接收路径: 从核 → rpmsg_send() → vring (共享内存) → IPI → Linux
          → rproc_vq_interrupt() → virtio_rpmsg_bus → rpmsg_char
          → /dev/rpmsg0 → read() → 用户程序
```

## 3. 代码修改方法

### 3.1 从核 (裸机) 代码

**位置**: `phytium-standalone-sdk-master/example/system/amp/openamp_for_linux/`

```
openamp_for_linux/
├── main.c              ← 从核主入口
├── src/
│   └── slaver_00_example.c  ← ★ 从核通信逻辑 (主要修改对象)
├── common/             ← 共享头文件
├── configs/            ← 平台配置文件
└── makefile
```

**修改从核逻辑** → 在 `slaver_00_example.c` 中修改消息处理回调

### 3.2 主核 (Linux) 代码

**位置**: 项目 `demo/` 目录

```
demo/
└── rpmsg-demo-single.c  ← Linux 主控程序
```

**修改 Linux 逻辑** → 参考 `rpmsg-demo-single.c` 编写新的数据处理程序

## 4. 编译方法

### 4.1 从核固件编译

```bash
# 1. 设置工具链
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

# 2. 进入目录
cd /home/alientek/Phytium_syscode/phytium-standalone-sdk-master/phytium-standalone-sdk-master/example/system/amp/openamp_for_linux

# 3. 配置 (首次或修改配置后需要)
make config_pe2204_phytiumpi_aarch64

# 4. 编译
make clean && make all

# 5. 输出
# → pe2204_aarch64_phytiumpi_openamp_core0.elf
```

### 4.2 Linux 程序编译

```bash
# 使用 Linux 交叉编译器
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"

${CROSS_COMPILE}gcc -Wall -O2 -o my_app my_app.c
```

## 5. 部署和烧写

### 5.1 从核固件部署

```bash
# 复制到开发板
scp pe2204_aarch64_phytiumpi_openamp_core0.elf user@192.168.88.11:/tmp/openamp_core0.elf
ssh user@192.168.88.11 "sudo cp /tmp/openamp_core0.elf /lib/firmware/"

# 重启从核 (无需重启系统)
ssh user@192.168.88.11 "
  echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
  sleep 1
  echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
"
```

### 5.2 Linux 程序部署

```bash
scp my_app user@192.168.88.11:~/
ssh user@192.168.88.11 "chmod +x ~/my_app"
```

### 5.3 设备树修改 (需要时)

设备树只有修改硬件资源时（如共享内存大小、中断号）才需要更新，修改上位机和从核程序代码**不需要更新设备树**。

如果确实需要修改设备树的流程见 `docs/setup-guide.md`。

## 6. 验证方法

### 6.1 检查从核状态

```bash
ssh user@192.168.88.11 "cat /sys/class/remoteproc/remoteproc0/state"
# running = 正常, offline = 未启动, crashed = 崩溃
```

### 6.2 检查 RPMsg 通道

```bash
ssh user@192.168.88.11 "ls /sys/bus/rpmsg/devices/"
# 应看到: virtio0.rpmsg-openamp-demo-channel.-1.0
```

### 6.3 查看内核日志

```bash
ssh user@192.168.88.11 "dmesg | grep -iE 'rproc|rpmsg|virtio' | tail -20"
```

### 6.4 运行测试程序

```bash
ssh user@192.168.88.11 "
  sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
  ./my_app
"
```

### 6.5 快速验证脚本

```bash
# 一键验证 OpenAMP 通信状态
ssh user@192.168.88.11 '
echo "=== remoteproc state: $(cat /sys/class/remoteproc/remoteproc0/state) ==="
echo "=== RPMsg channels ===" && ls /sys/bus/rpmsg/devices/ 2>/dev/null
echo "=== /dev/rpmsg ===" && ls -la /dev/rpmsg* 2>/dev/null
echo "=== dmesg last 5 ===" && dmesg | grep -iE "rproc|rpmsg" | tail -5
'
```

## 7. 当前通信通道配置

| 参数 | 值 | 说明 |
|------|-----|------|
| 通道名 | `rpmsg-openamp-demo-channel` | 主核和从核必须一致 |
| 从核地址 | `0` | RPMsg 目的地址 |
| 主核地址 | `0xFFFFFFFF` (RPMSG_ADDR_ANY) | 自动分配 |
| 共享内存基址 | `0xB0100000` | 物理地址 |
| 共享内存大小 | `0x19900000` (409MB) | 含固件代码+vring+数据缓冲 |
| IPI 中断 | SGI 9 | GICv3 软件生成中断 |
| 从核 CPU | CPU 3 (FTC310) | MPIDR 0x201 |

## 8. 故障排查

| 现象 | 检查点 | 解决 |
|------|--------|------|
| state = offline | 固件文件不存在 | `ls /lib/firmware/openamp_core0.elf` |
| state = crashed | 固件与内核不匹配 | 重新编译固件 |
| 无 RPMsg 通道 | 从核未创建端点 | 检查从核代码通道名 |
| /dev/rpmsg0 不存在 | 未绑定驱动 | 执行 bind 操作 |
| write/read 失败 | 权限问题 | `chmod 666 /dev/rpmsg*` |
