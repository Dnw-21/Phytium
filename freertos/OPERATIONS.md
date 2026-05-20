# FreeRTOS LoRa 接收系统 — 操作指导

> 最后更新: 2026-05-21  |  版本: v10
>
> 本文档涵盖日常操作的全部命令：编译、部署、FreeRTOS 启停、状态验证。

---

## 速查卡片

```bash
# 一键部署 (推荐)
cd /home/alientek/Phytium/freertos && bash deploy.sh

# 仅编译
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
make all -j$(nproc)

# 查看 FreeRTOS 状态
ssh user@192.168.88.11 "cat /sys/class/remoteproc/remoteproc0/state"

# 查看共享内存输出
ssh user@192.168.88.11 "sudo /home/user/trace_reader"
```

---

## 一、为什么 reboot 才能看到新固件?

### 根本原因：安全启动机制

FreeRTOS 固件通过 Linux 的 `remoteproc` 框架管理。当 FreeRTOS 已在 **running** 状态时：

```
echo stop > /sys/class/remoteproc/remoteproc0/state
```

这会触发 **OP-TEE 重新初始化远程 CPU 核心**，导致系统出现 **RCU stall**（CPU 长时间无响应），最终系统卡死，只能拔电重启。

### 安全策略

`deploy.sh` 采用**条件启动**策略：

| 当前状态 | deploy.sh 动作 | 风险 |
|----------|---------------|------|
| **offline** | 直接 `echo start` 启动 | 无风险 ✅ |
| **running** | 只更新 `/lib/firmware/openamp_core0.elf` 文件，不重启 | 无风险 ✅ → reboot 后生效 |

### 何时可见新固件?

- **方法一（推荐）**：手动 `sudo reboot` 开发板，remoteproc 自动加载新固件
- **方法二**：如果之前从未启动过 FreeRTOS（remoteproc=offline），deploy.sh 会直接启动

---

## 二、FreeRTOS 生命周期命令

所有命令在**开发板上**执行 (`ssh user@192.168.88.11`)。

### 2.1 查看状态

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

输出含义：
- `offline` — FreeRTOS 未运行
- `running` — FreeRTOS 正常运行
- 文件不存在 — remoteproc 驱动未加载 (需 reboot)

### 2.2 启动 FreeRTOS

**前置条件**：状态必须为 `offline`

```bash
echo 'user' | sudo -S sh -c 'echo openamp_core0.elf > /sys/class/remoteproc/remoteproc0/firmware'
echo 'user' | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
```

等待 5 秒后验证：
```bash
cat /sys/class/remoteproc/remoteproc0/state   # 应该显示 running
sudo /home/user/trace_reader                    # 应该看到启动日志
```

### 2.3 停止 FreeRTOS

> ⚠️ **危险操作！** 仅在明确需要时执行。停止后系统可能 RCU stall，需拔电重启。

```bash
echo 'user' | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
```

**替代方案（推荐）**：`sudo reboot` 重启开发板，FreeRTOS 自动 offline。

### 2.4 更换固件 (不重启 FreeRTOS)

```bash
# scp 传输新固件
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.11:/tmp/

# 替换固件文件
echo 'user' | sudo -S cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf \
  /lib/firmware/openamp_core0.elf

# 下次 reboot 生效
```

---

## 三、完整部署流程 (deploy.sh)

### 3.1 一键执行

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

### 3.2 脚本流程 (6步)

```
Step 1/6: 编译
  └─ make all -j$(nproc)
  └─ 输出: pe2204_aarch64_phytiumpi_openamp_for_linux.elf (约695K)

Step 2/6: 传输
  └─ scp .elf → 192.168.88.11:/tmp/

Step 3/6: 检查状态
  └─ cat /sys/class/remoteproc/remoteproc0/state

Step 4/6: 更新固件
  └─ cp .elf → /lib/firmware/openamp_core0.elf

Step 5/6: 安全启动
  ├─ offline → echo start (安全)
  └─ running → 跳过重启 (安全)

Step 6/6: 验证
  └─ trace_reader 45s → 查看 LoRa 帧解析输出
```

### 3.3 环境变量 (deploy.sh 已内置)

```bash
SDK_DIR="/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux"
TOOLCHAIN="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
BOARD_IP="192.168.88.11"
BOARD_USER="user"
```

---

## 四、手动编译

如果需要单独编译（不使用 deploy.sh）：

```bash
# 1. 同步源码到 SDK 目录 (如果修改了代码)
cp /home/alientek/Phytium/freertos/main.c \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/
cp /home/alientek/Phytium/freertos/inc/*.h \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/
cp /home/alientek/Phytium/freertos/src/*.c \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/

# 2. 编译
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make all -j$(nproc)
```

---

## 五、验证 FreeRTOS 是否存活

### 5.1 共享内存检查

```bash
ssh user@192.168.88.11 "echo 'user' | sudo -S busybox devmem 0xC8000000 32"
```

- **非零值** → FreeRTOS 存活（write_index > 0，有打印输出）
- **零值** → 可能已崩溃或未启动

### 5.2 查看实时输出

```bash
ssh user@192.168.88.11 "echo 'user' | sudo -S /home/user/trace_reader"
```

期望输出（v10）：
```
=== FreRTOS LoRa v10 Final (2026-05-21) ===
MPIDR=0x80000100 GetCpuId=1 dts=rproc=3
LoRa: AT init (verified format)
LoRa: AT config done
LoRa: MD0=LOW AUX=0 (ready)
LoRa: RX ready (IRQ)
[RX-hb] loop=1500 AUX=0 tx=0 isr=0 dat=0
...
=== FRAME #1: type=01 ts=... sync=... enc=16B dec=16B ===
  STATUS: node=216 sev=211 health=...
=== FRAME #2: type=04 ts=... sync=... enc=128B dec=128B ===
  NODE_RAW: 8 samples
```

### 5.3 内核日志

```bash
ssh user@192.168.88.11 "dmesg | tail -20"
```

关键消息：
- `remoteproc remoteproc0: powering up homo_core0` — 启动中
- `remoteproc remoteproc0: remote processor homo_core0 is now up` — 启动成功
- `rpmsg host is online` — RPMsg 通道就绪

---

## 六、常见故障处理

### 6.1 remoteproc 启动失败

```bash
# 查看失败原因
ssh user@192.168.88.11 "dmesg | grep remoteproc"
```

常见错误码：
- `-6` (ENXIO) — 设备不存在或底层固件异常 → 拔电重启
- `-16` (EBUSY) — 已运行 → 检查 state 文件

### 6.2 trace_reader 无输出 (WI=0)

三种可能：
1. **共享内存未映射** → 检查 main.c 中 `FMmuMap(0xC8000000, ...)` 是否存在
2. **FreeRTOS 数据异常崩溃** → `cat /sys/class/remoteproc/remoteproc0/state`
3. **Cache 一致性** → 确认 MMU 属性为 `MT_DEVICE_NGNRNE`，不是 `MT_NORMAL`

### 6.3 RCU stall (系统卡死)

唯一恢复方式：**拔电重启**

原因：`echo stop` 触发 OP-TEE 重新初始化远程核心。

### 6.4 帧解析失败

```bash
# 查看详细帧调试信息 (trace_reader 中)
# FRAME #N: type=... × enc=... dec=...   → CRC/格式错误
# CRC fail calc=... recv=...              → 帧尾偏移不对
# 吐核/丢弃                                → enc_start 指针错误 (应为 &data[9])
```

参考 [DEBUG_GUIDE.md](./DEBUG_GUIDE.md) 第五节踩坑记录。

---

## 七、源码修改工作流

```
1. 编辑源码
   vim /home/alientek/Phytium/freertos/main.c

2. 一键部署
   bash deploy.sh

3. 如果 FreeRTOS 已在运行 (deploy 会跳过重启)
   → 手动 sudo reboot 开发板
   → 等 90-120 秒

4. 验证
   ssh user@192.168.88.11 "sudo /home/user/trace_reader"
```

### 修改后记得双向同步

```bash
# Phytium/freertos → SDK (deploy.sh 自动完成)
# SDK → Phytium/freertos (手动)
cp SDK目录/main.c /home/alientek/Phytium/freertos/
cp SDK目录/inc/*.h /home/alientek/Phytium/freertos/inc/
cp SDK目录/src/*.c /home/alientek/Phytium/freertos/src/
```

---

## 八、快速重启 LoRa 模块

如果 LoRa 模块需要软重启（不重启整个 FreeRTOS）：

```bash
cd /home/alientek/Phytium/freertos
bash restart_lora.sh
```

脚本通过 GPIO 控制 MD0 重新初始化 LoRa 模块配置。

---

## 九、相关文档

| 文档 | 路径 | 内容 |
|------|------|------|
| 交接文档 | [HANDOVER.md](./HANDOVER.md) | 项目架构、技术决策、接手规则 |
| 调试指南 | [DEBUG_GUIDE.md](./DEBUG_GUIDE.md) | 完整调试过程、踩坑记录、版本历史 |
| Git 仓库 | /home/alientek/Phytium/ | 项目源码 |
| GD32 参考 | ../GD32L233C_Prj_Master_v3/ | 帧格式、加密算法、状态机参考 |
| Linux 侧工具 | ../src/linux-app/ | trace_reader、lora_receiver |