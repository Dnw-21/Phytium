# src/linux-app — Linux 侧验证与调试工具

> **更新**: 2026-06-24 | **适用平台**: 飞腾派 CEK8903 (aarch64) / 本地 x86_64 验证

本目录包含直接与 FreeRTOS 从核 RPMsg 通道或 LoRa 硬件交互的 Linux 侧 C 程序，用于 Task 1 的验证、调试和硬件确认。

---

## 目录结构

```
src/linux-app/
├── main.c              # 占位符 IoT 主程序（当前仅打印初始化信息）
├── rpmsg_recv.c        # ★ 当前推荐：RPMsg 通用通道接收器
├── lora_receiver.c     # Linux 直接驱动 ATK-MWCC68D LoRa 模块（历史验证路径）
├── aux_check.c         # GPIO2_10 (AUX) / GPIO3_1 (MD0) 引脚监测工具
├── at_test.c           # LoRa 模块 AT 命令测试
├── hw_verify.c         # 硬件基础验证
├── uart_test.c         # UART2 回环/收发测试
└── Makefile            # 构建 iot-main
```

---

## 工具说明

### rpmsg_recv.c — RPMsg 通用通道接收器（推荐）

**用途**: 连接 FreeRTOS `rpmsg-openamp-demo-channel` 通道，接收心跳、Echo 响应以及预留的 `CMD_LORA_RAW` 消息。

**当前状态**:
- `CMD_LORA_RAW 0x0023` 路径已在 FreeRTOS `main.c` 中准备，但**尚未被业务任务调用**，因此当前主要看到 `CMD_HEARTBEAT` 和 `CMD_ECHO_RESP`。
- 支持通过 `-n <service_name>` 参数指定通道名。

**编译**:
```bash
cd /home/alientek/Phytium/src/linux-app
make
# 输出: build/iot-main
```

**交叉编译到飞腾派**:
```bash
export CROSS_COMPILE=/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
${CROSS_COMPILE}gcc -Wall -O2 -o rpmsg_recv rpmsg_recv.c
sshpass -p 'user' scp rpmsg_recv user@192.168.88.10:~/
```

**运行**:
```bash
ssh user@192.168.88.10
./rpmsg_recv
# 或指定通道
./rpmsg_recv -n rpmsg-openamp-demo-channel
```

---

### lora_receiver.c — Linux 侧 LoRa 直接驱动

**用途**: 绕过 FreeRTOS，直接在 Linux 下通过 UART2 + GPIO 操作 ATK-MWCC68D 模块接收 LoRa 数据。用于早期硬件验证和接线确认。

**硬件连接**:
- J1 Pin8  = UART2_TXD → LoRa RXD
- J1 Pin10 = UART2_RXD → LoRa TXD
- J1 Pin7  = GPIO2_10  → LoRa AUX
- GPIO3_1  = MD0       → LoRa MD0

**编译与运行**:
```bash
cd /home/alientek/Phytium/src/linux-app
gcc -Wall -O2 -o lora_receiver lora_receiver.c
sudo ./lora_receiver
```

> 当前 Task 1 主控链路已迁移到 FreeRTOS，`lora_receiver.c` 保留作为底层硬件调试参考。

---

### aux_check.c — AUX / MD0 引脚监测

**用途**: 监测 GPIO2_10 (AUX) 电平变化，验证 LoRa 模块 AUX 引脚状态和 MD0 模式切换。

**运行**:
```bash
gcc -Wall -O2 -o aux_check aux_check.c
sudo ./aux_check
```

---

### at_test.c — AT 命令测试

**用途**: 直接向 LoRa 模块发送 AT 命令并读取回复，验证模块配置和通信。

**运行**:
```bash
gcc -Wall -O2 -o at_test at_test.c
sudo ./at_test
```

---

### hw_verify.c / uart_test.c

**用途**: 更底层的硬件验证和 UART2 回环/收发测试。

---

### main.c — 占位符主程序

**用途**: 当前仅打印初始化信息，未接入实际业务逻辑。`Makefile` 默认目标 `iot-main` 会编译所有 `.c` 文件并链接为单一二进制。

---

## 与 FreeRTOS 的 RPMsg 通道对应关系

| Linux 程序 | RPMsg 通道 | FreeRTOS 端点 | 说明 |
|------------|------------|---------------|------|
| `rpmsg_recv` | `rpmsg-openamp-demo-channel` | `g_ept` (main.c) | 通用通道，当前主要心跳/Echo |
| `lora_receiver` | 无（直接 UART） | 无 | Linux 直接操作 LoRa 硬件 |

---

## 注意事项

1. **rpmsg_recv 当前看不到 LoRa 业务数据** — FreeRTOS 侧 `rpmsg_send_lora_raw()` 已准备但未接线，业务数据当前走 SHM 调试打印（`trace_reader`）。
2. **运行 GPIO/UART 工具需要 root** — 因为它们操作 `/dev/mem` 和 `/dev/ttyAMA2`。
3. **交叉编译时注意工具链路径** — 飞腾派使用 `aarch64-none-linux-gnu-gcc`。

---

**更新**: 2026-06-24
