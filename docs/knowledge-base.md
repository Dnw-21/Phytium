# OpenAMP 异构多核通信知识库

> **更新**: 2026-05-18 | **项目进度**: 设备树配置完成 → Bare-metal/FreeRTOS 双版本 → A1-A4 性能优化 → C2 边缘检测 → C3 Web面板 → **GD32主控移植完成** → [GD32移植记录](transplant-gd32-to-phytium.md)

## 1. 硬件架构

### SoC: Phytium PE2204

| CPU | 核心类型 | MPIDR | 逻辑CPU | 用途 |
|-----|---------|-------|---------|------|
| cpu@0 | FTC310 (LITTLE) | 0x200 | cpu0 | Linux SMP |
| cpu@1 | FTC310 (LITTLE) | 0x201 | cpu1 | Linux SMP |
| cpu@2 | FTC664 (big) | 0x000 | cpu2 | Linux SMP |
| cpu@3 | FTC664 (big) | 0x100 | cpu3 | **FreeRTOS 从核 (OpenAMP 独占)** |

### 内存布局

| 地址范围 | 大小 | 用途 |
|----------|------|------|
| 0x80000000 - 0x80010000 | 64KB | 启动保留 (/memreserve/) |
| 0x80010000 - 0xB0100000 | ~768MB | Linux 可用 |
| **0xB0100000 - 0xC9A00000** | **409MB** | **OpenAMP 共享内存 (reserved-memory, no-map)** |
| 0xC9A00000+ | ~3GB | Linux 可用 |

## 2. 设备树配置 (Kernel 6.6)

### 嵌套结构（正确）

```dts
/memreserve/ 0x80000000 0x10000;

/ {
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        rproc: rproc@b0100000 {
            no-map;
            reg = <0x0 0xb0100000 0x0 0x19900000>;
        };
    };

    homo_rproc: homo_rproc@0 {
        compatible = "homo,rproc";
        status = "okay";
        homo_core0: homo_core0@b0100000 {
            compatible = "homo,rproc-core";
            remote-processor = <3>;         // CPU 3 (FTC664)
            inter-processor-interrupt = <9>; // SGI 9 (8-15可用, 0-7内核保留)
            memory-region = <&rproc>;
            firmware-name = "openamp_core0.elf";
        };
    };
};
```

## 3. 关键 DT 属性

| 属性 | 值 | 说明 |
|------|-----|------|
| `remote-processor` | `<3>` | Linux 逻辑 CPU 号 |
| `inter-processor-interrupt` | `<9>` | GICv3 SGI 中断号 (8-15范围) |
| `memory-region` | `<&rproc>` | phandle 指向 reserved-memory 节点 |
| `firmware-name` | `"openamp_core0.elf"` | 必须在 /lib/firmware/ 下 |

## 4. 内核配置

```makefile
CONFIG_REMOTEPROC=y
CONFIG_REMOTEPROC_CDEV=y
CONFIG_HOMO_REMOTEPROC=y       # Phytium 定制驱动 (内建)
CONFIG_RPMSG=y
CONFIG_RPMSG_CHAR=m            # /dev/rpmsg0 字符设备 (模块)
CONFIG_RPMSG_CTRL=m            # /dev/rpmsg_ctrl0 控制设备 (模块)
CONFIG_RPMSG_VIRTIO=y          # VirtIO RPMsg 传输
CONFIG_PHYTIUM_MBOX=y          # Phytium 邮箱驱动
```

## 5. OpenAMP 驱动启动流程

```
homo_rproc_probe()
  ├── rproc_of_parse_firmware()    → 获取 firmware-name
  ├── rproc_alloc()               → 分配 remoteproc 设备
  ├── of_property_read_u32()       → 解析 remote-processor
  ├── of_property_read_u32()       → 解析 inter-processor-interrupt
  ├── of_parse_phandle()           → 解析 memory-region
  ├── ioremap(PAGE_KERNEL_EXEC)    → 映射共享内存为可执行
  ├── request_percpu_irq()         → 注册 SGI IPI 中断
  ├── cpuhp_setup_state()          → CPU hotplug 回调
  ├── homo_core_of_init()          → 解析子节点 "homo,rproc-core"
  └── rproc_add()                  → 注册 remoteproc 设备

homo_rproc_start() (echo start > state)
  ├── remove_cpu(3)                → 下电 CPU3
  ├── 加载 openamp_core0.elf 到 0xB0100000
  ├── arm_smccc_smc(CPU_ON, ...)   → PSCI 启动 CPU3
  └── rproc_virtio → virtio_rpmsg_bus → 创建 RPMsg 通道
```

## 6. FreeRTOS 从核编译

### 6.1 工具链

- **必须**: `aarch64-none-elf-gcc` (ARM bare-metal, 非 Linux 版本)
- **路径**: `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/`

### 6.2 FreeRTOS SDK: phytium-free-rtos-sdk

**前置**: standalone SDK 已复制到 `freertos-sdk/standalone/` 目录下。

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

make config_pe2204_phytiumpi_aarch64
make clean && make all
# 输出: pe2204_aarch64_phytiumpi_openamp_for_linux.elf
```

### 6.3 FreeRTOS 项目代码结构 (本项目的移植代码)

```
phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/
├── main.c              ← FreeRTOS 入口 (chaos_init → master_init → task_create → scheduler)
├── src/
│   ├── rpmsg-echo_os.c ← ★ RPMsg通信核心 + 边缘检测
│   ├── master_recv.c   ← LoRa帧接收/解析
│   ├── master_judge.c  ← 故障判决
│   ├── master_cmd.c    ← 命令生成/加密/发送
│   ├── master_sys.c    ← 节点管理/Flash模拟
│   ├── chaos_encrypt.c ← 混沌加解密
│   └── log.c           ← 日志
├── inc/
│   ├── master.h        ← 主控数据结构/宏定义
│   ├── data_frame.h    ← LoRa帧数据结构
│   ├── chaos_encrypt.h ← 混沌加密接口
│   └── log.h           ← 日志接口
├── configs/            ← 平台配置文件
└── makefile            ← 构建
```

### 6.4 Bare-metal SDK (备用)

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

cd /home/alientek/Phytium_syscode/phytium-standalone-sdk-master/example/system/amp/openamp_for_linux

make config_pe2204_phytiumpi_aarch64
make clean && make all
# 输出: pe2204_aarch64_phytiumpi_openamp_core0.elf
```

## 7. RPMsg 通信操作

### 7.1 启动序列

```bash
modprobe rpmsg_char rpmsg_ctrl                          # 加载模块
echo start > /sys/class/remoteproc/remoteproc0/state    # 启动从核
echo rpmsg_chrdev > .../driver_override                  # 绑定驱动
echo virtio0.rpmsg-openamp-demo-channel.-1.0 > .../bind # 创建设备
sudo chmod 666 /dev/rpmsg*                              # 设置权限
```

### 7.2 验证命令

```bash
cat /sys/class/remoteproc/remoteproc0/state  # running/offline
ls /sys/bus/rpmsg/devices/                   # 应看到通道设备
ls /dev/rpmsg*                               # rpmsg0, rpmsg_ctrl0
dmesg | grep -i rproc                        # 启动日志
```

### 7.3 停止序列

```bash
echo stop > /sys/class/remoteproc/remoteproc0/state
modprobe -r rpmsg_char rpmsg_ctrl
```

## 8. 固件配置要点

- 加载地址: `0xB0100000` (CONFIG_IMAGE_LOAD_ADDRESS)
- 中断角色: slave (CONFIG_INTERRUPT_ROLE_SLAVE=y)
- 使用 IPI 中断: CONFIG_USE_OPENAMP_IPI=y
- 启用 Cache 一致性: CONFIG_USE_CACHE_COHERENCY=y

## 9. 题排查表

| 现象 | 原因 | 解决 |
|------|------|------|
| `/sys/class/remoteproc/` 为空 | 设备树无 OpenAMP 节点 | 确认 dtb 包含嵌套结构 |
| `OF: reserved mem:` 未打印 | 未用启动 dtb 修改 | 直接修改 dtb，不用 overlay |
| probe 成功但无设备 | 用了平铺结构 | 改用嵌套 `homo,rproc-core` |
| `Permission denied` | 设备权限 | `chmod 666 /dev/rpmsg*` |
| 固件崩溃 | 编译架构不匹配 | 确认 `aarch64-none-elf` 编译 |
| FreeRTOS sensor 只发1包 | remoteproc priv 丢失 | 使用全局 `g_remoteproc_priv` |

## 10. 关键路径速查

| 文件 | 位置 |
|------|------|
| FreeRTOS项目代码 | `/home/alientek/Phytium/freertos/` |
| FreeRTOS SDK | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` |
| Bare-metal SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 内核源码 (5.10) | `/home/alientek/Phytium_syscode/内核源码/` |
| 飞腾参考文档 | `/home/alientek/phytium-embedded-docs-master/` |
| Linux 应用 | `/home/alientek/Phytium/src/openamp-demo/` |
| GD32 原始工程 | `/home/alientek/Phytium/GD32L233C_Prj_Master/` |

## 11. 自动化测试框架

### 11.1 架构

```
┌─────────────────────────────────────────────────────────┐
│                    测试主机 (x86_64)                      │
│                                                          │
│  test_runner.sh ──总控脚本────                           │
│  ├── TC01: RPMsg Link PING (链路测试)                    │
│  ├── TC02: Fault Injection (故障注入全覆盖)              │
│  ├── TC03: Command TX (命令传输监听)                     │
│  ├── TC04: Chaos Encrypt (混沌加密验证, 本地计算)        │
│  └── TC05: Stress Test (压力测试, 高频率故障注入)        │
│       │                                                  │
│       │ SSH (sshpass)                                    │
│       ▼                                                  │
│  ┌─────────────────────────────────────────────────┐    │
│  │  Phytium Pi 开发板 (192.168.88.11)               │    │
│  │                                                   │    │
│  │  /home/user/demo/tests/                           │    │
│  │  ├── test_rpmsg_link    →  /dev/rpmsg0 (RPMsg)   │    │
│  │  ├── test_fault_inject  →  /dev/rpmsg0           │    │
│  │  ├── test_command       →  /dev/rpmsg0           │    │
│  │  ├── test_encrypt       →  本地计算 (无RPMsg)     │    │
│  │  └── test_stress        →  /dev/rpmsg0           │    │
│  │                                                   │    │
│  │  RPMsg ──────────────────► FreeRTOS (CPU3)        │    │
│  │    DEVICE_MASTER_TEST (0x0030)                    │    │
│  │    ◄──────────────────── DEVICE_MASTER_CMD (0x0021)│   │
│  │                                                   │    │
│  │  测试报告: /home/alientek/Phytium/docs/           │    │
│  │    test_report_YYYYMMDD_HHMMSS.md                 │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

### 11.2 协议定义

**Linux → FreeRTOS**: `DEVICE_MASTER_TEST (0x0030)`

数据载荷为 `TestCtrlPacket_t`:
| 字段 | 字节 | 说明 |
|------|------|------|
| subcmd | 1 | 测试子命令 (PING=0x01, SINGLE_FAULT=0x02, CONTINUOUS=0x03, STOP=0x04, FLASH_CHECK=0x05, CHAOS_ENCRYPT=0x06, STATUS=0x07) |
| node_id | 1 | 目标节点 ID (0-2) |
| fault_type | 1 | 故障类型 (1=OVER_VOLTAGE, 2=UNDER_VOLTAGE, 3=VOLTAGE_SAG, 4=VOLTAGE_SWELL, 5=TRANSIENT) |
| severity | 1 | 严重等级 (1=NORMAL, 2=WARNING, 3=DANGER) |
| sample_count | 2 | 采样点数 |
| reserved | 2 | 保留 |

**FreeRTOS → Linux**: `DEVICE_MASTER_CMD (0x0021)`

数据载荷为 `TestRespPacket_t`:
| 字段 | 字节 | 说明 |
|------|------|------|
| resp_code | 1 | 响应码 (PONG=0x01, FAULT_SENT=0x02, RUNNING=0x03, STOPPED=0x04, FLASH_OK=0x05, ENCRYPT_OK=0x06, ERROR=0xFF) |
| subcmd_echo | 1 | 回显子命令 |
| node_id | 1 | 节点 ID |
| fault_type | 1 | 故障类型 |
| processed_count | 4 | 已处理包数 |
| timestamp_ms | 4 | 时间戳 (ms) |

### 11.3 FreeRTOS 侧测试控制模块

```
/home/alientek/Phytium/freertos/
├── inc/test_control.h    — 测试协议定义 (宏 + 数据结构 + 接口)
└── src/test_control.c    — 测试控制逻辑实现
```

**关键函数**:
- `test_control_init()` — 初始化测试状态
- `test_control_handle(ctrl, resp)` — 处理测试控制包，返回响应包
  - `TEST_PING`: 返回 PONG + 时间戳
  - `TEST_SINGLE_FAULT`: 构造仿真 LoRa 帧注入 master_recv_inject_data
  - `TEST_CONTINUOUS`: 设置连续运行标志
  - `TEST_STOP`: 停止连续模式
  - `TEST_CHAOS_ENCRYPT`: 执行 chaos_encrypt_block → chaos_decrypt_block 往返验证
- `test_control_get_status()` — 获取测试统计

**集成点**: [rpmsg-echo_os.c](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/rpmsg-echo_os.c#L422) — `DEVICE_MASTER_TEST` case → `test_control_handle()` → RPMsg 响应

### 11.4 运行测试

```bash
cd /home/alientek/Phytium/tests

# 编译
make

# 部署到开发板
make deploy

# 运行全部测试
bash test_runner.sh all

# 运行单个测试
bash test_runner.sh tc01      # 链路 PING 测试
bash test_runner.sh tc02      # 故障注入测试
bash test_runner.sh tc03      # 命令传输测试
bash test_runner.sh tc04      # 混沌加密测试 (本地)
bash test_runner.sh tc05      # 压力测试

# 报告位置
ls /home/alientek/Phytium/docs/test_report_*.md
```

### 11.5 测试清单

| ID | 测试项 | 类型 | 验证内容 | 通过标准 |
|----|--------|------|----------|----------|
| TC01 | RPMsg Link PING | RPMsg | CPU3 链路连通性 | PING 成功率 100%, RTT < 100ms |
| TC02 | Fault Injection | RPMsg | 所有节点×故障类型×严重等级 | 45/45 组合全部 OK |
| TC03 | Command TX | RPMsg | FreeRTOS→Linux 命令传输 | 至少收到 1 条 command |
| TC04 | Chaos Encrypt | 本地 | encrypt→decrypt 往返一致性 | 6 种数据长度全部一致 |
| TC05 | Stress Test | RPMsg | 高频率故障注入 (5秒) | ACK 率 ≥ 70%, 速率 > 10 faults/s |

### 11.6 添加新测试

1. 创建 `test_modules/test_new.c` (参考已有模块)
2. 在 `Makefile` 添加编译规则
3. 在 `test_runner.sh` 添加 `test_tcXX()` 函数
4. 若需 FreeRTOS 侧新子命令，在 `test_control.h` 添加宏，在 `test_control.c` switch 添加 case

## 12. 参考链接

- 飞腾嵌入式文档: https://gitee.com/phytium_embedded/phytium-embedded-docs
- OpenAMP 手册: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
- OpenAMP 官方: https://www.openampproject.org/