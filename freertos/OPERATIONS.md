# FreeRTOS LoRa 接收系统 — 操作指导

> 最后更新: 2026-06-02  |  版本: 移植完成版
>
> 本文档涵盖日常操作的全部命令：编译、部署、FreeRTOS 启停、状态验证。

---

## 速查卡片

```bash
# 一键部署 (推荐)
cd /home/alientek/Phytium/freertos && bash deploy.sh

# 仅编译
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make config_pe2204_phytiumpi_aarch64
make all -j$(nproc)

# 查看 FreeRTOS 状态
ssh user@192.168.88.11 "cat /sys/class/remoteproc/remoteproc0/state"

# 查看共享内存输出
ssh user@192.168.88.11 "echo 'user' | sudo -S /home/user/trace_reader"

# 保存日志到文件
ssh user@192.168.88.11 "echo 'user' | sudo -S timeout 60 /home/user/trace_reader" > /tmp/lora_log.txt
```

---

## 一、为什么在 SDK 目录编译？编译的是我的代码吗？

**是的，编译的就是你写的代码。** 关键在 SDK 的 makefile：

```makefile
USER_CSRC := main.c
USER_CSRC += $(wildcard src/*.c)
```

`$(wildcard src/*.c)` 自动搜集 `src/` 目录下所有 `.c` 文件加入编译。

`deploy.sh` 第一步把你写的源码同步到 SDK 目录：
```bash
cp freertos/main.c → SDK/main.c
cp freertos/src/*.c → SDK/src/
cp freertos/inc/*.h → SDK/inc/
```

SDK 提供编译框架（FreeRTOS 内核、ARM 驱动、OpenAMP、链接脚本、工具链），你的代码作为应用层嵌入其中。

---

## 二、为什么 reboot 才能看到新固件？

### 根本原因：安全启动机制

当 FreeRTOS 已在 **running** 状态时，`echo stop` 会触发 OP-TEE 重新初始化远程 CPU 核心，导致 **RCU stall**（系统卡死），只能拔电重启。

### 安全策略

| 当前状态 | deploy.sh 动作 | 风险 |
|----------|---------------|------|
| **offline** | 直接 `echo start` 启动 | 无风险 ✅ |
| **running** | 只更新固件文件，不重启 | 无风险 ✅ → reboot 后生效 |

### 何时可见新固件？

- **方法一（推荐）**：手动 `sudo reboot` 开发板，remoteproc 自动加载新固件
- **方法二**：如果之前从未启动过 FreeRTOS（remoteproc=offline），deploy.sh 会直接启动

---

## 三、FreeRTOS 生命周期命令

所有命令在**开发板上**执行 (`ssh user@192.168.88.11`)。

### 3.1 查看状态

```bash
cat /sys/class/remoteproc/remoteproc0/state
```

- `offline` — FreeRTOS 未运行
- `running` — FreeRTOS 正常运行
- 文件不存在 — remoteproc 驱动未加载

### 3.2 启动 FreeRTOS

**前置条件**：状态必须为 `offline`

```bash
echo 'user' | sudo -S sh -c 'echo openamp_core0.elf > /sys/class/remoteproc/remoteproc0/firmware'
echo 'user' | sudo -S sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
```

### 3.3 停止 FreeRTOS

> ⚠️ **危险操作！** 停止后系统可能 RCU stall，需拔电重启。

```bash
# 推荐：直接 reboot
sudo reboot
```

### 3.4 更换固件 (不重启 FreeRTOS)

```bash
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.11:/tmp/
echo 'user' | sudo -S cp /tmp/pe2204_aarch64_phytiumpi_openamp_for_linux.elf \
  /lib/firmware/openamp_core0.elf
# 下次 reboot 生效
```

---

## 四、完整部署流程 (deploy.sh)

### 4.1 一键执行

```bash
cd /home/alientek/Phytium/freertos
bash deploy.sh
```

### 4.2 脚本流程

```
Step 0/7: 同步源码到 SDK 目录
  └─ cp main.c src/*.c inc/*.h → SDK

Step 1/7: 编译
  └─ make all -j$(nproc)
  └─ 输出: pe2204_aarch64_phytiumpi_openamp_for_linux.elf (约707K)

Step 2/7: 传输
  └─ scp .elf → 192.168.88.11:/tmp/

Step 3/7: 检查状态
  └─ cat /sys/class/remoteproc/remoteproc0/state

Step 4/7: 更新固件
  └─ cp .elf → /lib/firmware/openamp_core0.elf

Step 5/7: 安全启动
  ├─ offline → echo start (安全)
  └─ running → 跳过重启 (安全)

Step 6/7: 验证
  └─ trace_reader 45s → 查看 FreeRTOS 输出
```

---

## 五、手动编译

```bash
# 1. 同步源码到 SDK 目录
cp /home/alientek/Phytium/freertos/main.c \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/
cp /home/alientek/Phytium/freertos/inc/*.h \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/inc/
cp /home/alientek/Phytium/freertos/src/*.c \
  /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/

# 2. 编译
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
make config_pe2204_phytiumpi_aarch64
make clean
make all -j$(nproc)
```

---

## 六、验证 FreeRTOS 是否存活

### 6.1 共享内存检查

```bash
ssh user@192.168.88.11 "echo 'user' | sudo -S busybox devmem 0xC8000000 32"
```

- **非零值** → FreeRTOS 存活
- **零值** → 可能已崩溃或未启动

### 6.2 查看实时输出

```bash
ssh user@192.168.88.11 "echo 'user' | sudo -S /home/user/trace_reader"
```

期望输出（移植后）：
```
D1
D2
...
D6-IRQ
AT init start (pre-scheduler)
AT+ADDR=00,0A: ...
AT init done
MD0=LOW AUX=1 (ready)
RX IRQ enabled
RPMsg endpoint created
Recv task started
Process task started
Judge task started
Poll task started

[RECV] sync=XXXXXXXXXXXX type=0x01 len=XX
[DEC] sync=XXXXXXXXXXXX type=0x01 len=XX: XX XX ...
[RECV] sync=XXXXXXXXXXXX type=0x04 len=XX
[DEC] sync=XXXXXXXXXXXX type=0x04 len=XX: XX XX ...
Node0: status saved (20 pts)
[ENC][CMD] node0 code=0x14 payload: 14 XX XX XX XX
Poll: node0 status ok sev=0
```

### 6.3 保存日志到文件

```bash
# 方法1: 本地保存
ssh user@192.168.88.11 "echo 'user' | sudo -S timeout 60 /home/user/trace_reader" > /tmp/lora_log.txt

# 方法2: 开发板保存
echo 'user' | sudo -S timeout 60 /home/user/trace_reader > /tmp/lora_log.txt
```

---

## 七、常见故障处理

### 7.1 remoteproc 启动失败

```bash
ssh user@192.168.88.11 "dmesg | grep remoteproc"
```

- `-6` (ENXIO) → 拔电重启
- `-16` (EBUSY) → 已运行

### 7.2 trace_reader 无输出 (WI=0)

1. 共享内存未映射 → 检查 main.c 中 `FMmuMap(0xC8000000, ...)` 是否存在
2. FreeRTOS 崩溃 → `cat /sys/class/remoteproc/remoteproc0/state`
3. Cache 一致性 → 确认 MMU 属性为 `MT_DEVICE_NGNRNE`

### 7.3 RCU stall (系统卡死)

唯一恢复方式：**拔电重启**

### 7.4 LoRa 模块不响应

1. 检查 MD0 是否为 LOW（进入透传模式）
2. 检查 AUX 是否为 HIGH（模块空闲）
3. 检查 AT 命令是否返回 OK
4. 检查 WLRATE=23,5 / PACKSIZE=3 / NETID=0 是否与终端节点一致

### 7.5 混沌解密失败

- 确认终端节点和主控的 `chaos_encrypt.c` 初始状态一致
- 确认 sync_code 为 8 字节 uint64_t
- 确认 enc_start 偏移量正确（frame[17]，即跳过 AA 55 len2B ts4B type sync8B）

### 7.6 队列满丢帧

看到 `[RECV] queue full` 时需要增大 `RECV_QUEUE_LENGTH`（当前 16），或优化 process_task 处理速度。

### 7.7 Tier2 不触发故障轮询

- 确认终端节点 `NodeUploadHeader_t.fault_pending` 为 1
- 确认 master_process_task 的 `process_node_header` 正确设置了 `node->fault_pending`