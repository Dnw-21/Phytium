# src/openamp-demo — OpenAMP/RPMsg 示例与生产程序

> **更新**: 2026-06-24 | **适用平台**: 飞腾派 CEK8903 (aarch64)

本目录包含 OpenAMP 异构多核通信的示例程序、历史生产程序以及设备树 overlay。当前项目的主控链路已迁移到 `freertos/main.c` 和 `src/linux-app/rpmsg_recv.c`，本目录中的部分程序为历史遗留或调试工具。

---

## 目录结构

```
src/openamp-demo/
├── Makefile                    # 交叉编译脚本
├── openamp.dtso                # OpenAMP 设备树 overlay
├── scripts/
│   └── deploy.sh               # 部署脚本（历史）
├── docs/
│   └── README.md               # 原始 OpenAMP demo 文档
├── linux-master/
│   ├── master_receiver.c       # 旧版 LoRa 数据接收器（DEVICE_LORA_DATA）
│   ├── lora_ctrl.c             # 旧版 LoRa RX 开关控制（DEVICE_LORA_CTRL）
│   ├── rpmsg_master.c          # RPMsg Echo 主控程序
│   ├── rpmsg_test.c            # RPMsg 基础测试
│   └── rpmsg_ping.c            # RPMsg Ping/Pong 测试
└── remote-core/
    └── rpmsg_slave.c           # 早期从核（裸机）模板，当前未使用
```

---

## 工具说明

### master_receiver.c — 旧版 LoRa 数据接收器

**用途**: 监听 RPMsg 通道，接收并打印 FreeRTOS 上报的 LoRa 原始帧。

**当前状态**:
- 使用旧命名 `DEVICE_LORA_DATA 0x0023`，当前 FreeRTOS 代码已改为 `CMD_LORA_RAW 0x0023`（同一数值）。
- **FreeRTOS 侧 `rpmsg_send_lora_raw()` 尚未被业务任务调用**，因此当前运行该程序主要看到心跳，看不到 LoRa 业务数据。
- 建议新开发使用 `src/linux-app/rpmsg_recv.c`，其命令命名与 FreeRTOS `rpmsg_proto.h` 保持一致。

**编译**:
```bash
cd /home/alientek/Phytium/src/openamp-demo
make master-recv
# 输出: build/master_receiver
```

**运行**:
```bash
ssh user@192.168.88.10
./master_receiver
```

---

### lora_ctrl.c — 旧版 LoRa RX 开关控制

**用途**: 通过 RPMsg 命令 `DEVICE_LORA_CTRL 0x0022` 控制 FreeRTOS 侧 LoRa 数据接收的启停。

**当前状态**:
- **当前 FreeRTOS `main.c` 未处理 `DEVICE_LORA_CTRL` 命令**，该功能仅存在于历史 `freertos/src/rpmsg-echo_os.c` 中。
- 因此 `lora_ctrl` 当前无法实际控制 LoRa 接收开关，仅作为协议参考保留。

**编译与运行**:
```bash
make lora-ctrl
# 输出: build/lora_ctrl
ssh user@192.168.88.10
./lora_ctrl start    # 尝试开启（当前无响应）
./lora_ctrl stop     # 尝试关闭（当前无响应）
./lora_ctrl status   # 查询状态（当前无响应）
```

---

### rpmsg_master.c — RPMsg Echo 主控

**用途**: OpenAMP 通用 Echo 测试程序，向 FreeRTOS 从核发送消息并等待回显。

**编译与运行**:
```bash
make master
# 输出: build/rpmsg_master
ssh user@192.168.88.10
./rpmsg_master
```

---

### rpmsg_test.c / rpmsg_ping.c — RPMsg 测试工具

**用途**: 基础 RPMsg 功能测试和延迟测量。

---

### rpmsg_slave.c — 早期从核模板

**用途**: 早期裸机从核程序模板，当前项目已改用 FreeRTOS，该文件不再被编译进固件。

---

### openamp.dtso — 设备树 Overlay

**用途**: 描述 OpenAMP 共享内存、remoteproc CPU、vring 等硬件资源的设备树片段。当前系统启动时已通过其他方式加载，本文件保留作为参考。

---

## 与当前主控链路的对应关系

| 本目录程序 | 对应 FreeRTOS 文件 | 状态 |
|------------|-------------------|------|
| `master_receiver` | `freertos/main.c` `rpmsg_send_lora_raw()` | 命名待统一，数据路径未接线 |
| `lora_ctrl` | `freertos/src/rpmsg-echo_os.c` | 功能已废弃，当前 main.c 不处理 |
| `rpmsg_master` / `rpmsg_test` / `rpmsg_ping` | `freertos/main.c` 通用端点 | 可用，用于 RPMsg 基础验证 |
| `rpmsg_slave` | 无 | 废弃 |

---

## 编译全部目标

```bash
cd /home/alientek/Phytium/src/openamp-demo
make all
```

默认交叉编译器路径已在 `Makefile` 中指定：
```
/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
```

可通过 `CROSS_COMPILE` 变量覆盖：
```bash
make CROSS_COMPILE=/path/to/your/aarch64-none-linux-gnu-
```

---

## 注意事项

1. **本目录程序不是当前 Task 1 主线** — 当前主控链路为 `freertos/main.c` + `src/linux-app/rpmsg_recv.c`。
2. **master_receiver / lora_ctrl 使用旧协议命名** — 若要与当前 FreeRTOS 交互，需同步更新命令名。
3. **运行前确保 remoteproc 已启动** — `echo start > /sys/class/remoteproc/remoteproc0/state`

---

**更新**: 2026-06-24
