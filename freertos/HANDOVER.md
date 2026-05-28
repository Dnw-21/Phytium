# FreeRTOS LoRa 接收项目 — 交接文档

> 编写日期: 2026-05-26  |  最后更新: 2026-05-21
> 状态: **v12-SIM — 模拟数据注入已跑通，部分验证通过**

---

## 一、当前进度总览 (v11-S1)

### 1.1 已完成

| 阶段 | 说明 | 状态 | 验证 |
|------|------|:--:|------|
| 共享内存打印 | FreeRTOS → Linux 通过 0xC8000000 通信 | ✅ | trace_reader 实时可见 |
| **spf() 互斥保护** | **shm_print 模块, Mutex 保护多任务打印** | ✅ | **v11-S1: 编译+部署+验证通过** |
| **log.c 统一输出** | **f_printk → shm_puts, 经 Mutex 保护** | ✅ | **v11-S1: 编译+部署+验证通过** |
| GPIO 控制 | MD0(GPIO3_1) OUT, AUX(GPIO2_10) IN | ✅ | IOPAD 复用 FUNC6 |
| UART2 PL011 | 115200 8N1, 100MHz 时钟 | ✅ | 收发正常 |
| LoRa AT 配置 | 10条AT命令全部 OK | ✅ | WLRATE=23,5 / TPOWER=4 |
| LoRa 透传 | MD0 LOW + AUX 就绪 | ✅ | 进入数据接收 |
| GICv3 中断 | UART2 IRQ117 → CPU1, ISR → 环形缓冲 | ✅ | isr_count 递增 |
| 帧格式解析 | AA55/55AA, GD32 格式对齐, 混沌解密 | ✅ | type=01 STATUS + type=04 NODE_RAW |
| 安全部署 | 条件启动, 无 RCU stall | ✅ | deploy.sh 一键完成 |
| 设备树/CPU 确认 | remote-processor=3 → 物理 CPU1 (big核) | ✅ | MPIDR_EL1 + GetCpuId 双重验证 |
| RPMsg 通道 | FreeRTOS ↔ Linux 通信 | ✅ | dmesg: rpmsg host is online |

### 1.2 数据流 (当前架构)

```
LoRa节点 ──无线──> E220模块 ──UART2──> FreeRTOS CPU1 (big核)
                                           │
                                 ┌─────────┴─────────┐
                                 │ uart2_lora_isr()   │ GICv3 IRQ117
                                 │ → ring_buf[4096]  │
                                 └─────────┬─────────┘
                                           │
                                 ┌─────────┴─────────┐
                                 │ lora_task          │
                                 │ → lora_frame_buf   │
                                 │ → process_lora_frame() │
                                 │   - 搜 AA55 帧头        │
                                 │   - 读 TS/TYPE/SYNC     │
                                 │   - chaos_decrypt 解密  │
                                 │   - 按类型解析负载       │
                                 │ → shm_spf() 写共享内存 (Mutex保护) │
                                 └─────────┬─────────┘
                                           │
                                    0xC8000000 (1MB, MT_DEVICE_NGNRNE)
                                           │
                                     ┌─────┴─────┐
                                     │ trace_reader│ /dev/mem mmap
                                     └─────────────┘
                                         终端实时打印
```

### 1.3 待完成 (增量开发计划 — "管道优先"策略)

| 步骤 | 任务 | 优先级 | 状态 |
|:--:|------|:--:|:--:|
| S1 | spf() 互斥保护 + log.c 统一输出 | 高 | ✅ 已完成 |
| **S2** | **打通 RPMsg 数据管道** — 确认 495B 载荷上限, ECHO+心跳验证通过 | **高** | ✅ 已完成 |
| **S3** | **移植 master_recv.c 完整状态机** — process_lora_frame 集成状态机处理 | **高** | ✅ 已完成 |
| **S4** | **移植 master_judge.c 故障判断** — Judge 任务启动, 超时检测+故障请求波形 | **中** | ✅ 已完成 |
| **S5** | **移植 master_cmd.c 命令下发** — Cmd 任务启动, LoRa+RPMsg 双路发送 | **中** | ✅ 已完成 |
| **S6** | **移植 master_sys.c 系统管理** — Flash→共享内存模拟, 节点管理, LoRa RX控制 | **低** | ✅ 已完成 |
| S7 | 集成与端到端压力测试 | 低 | ⬜ |
| **S8** | **模拟数据注入测试** — 构造模拟LoRa帧注入process_lora_frame，验证状态机 | **高** | 🔶 进行中 |
| **S9** | **RPMsg数据通道完善** — 状态机处理结果通过RPMsg结构化发送到Linux | **高** | ⬜ |
| **S10** | **Linux侧应用完善** — rpmsg_recv解析RPMsg协议头，分类展示数据 | **高** | ⬜ |
| **S11** | **LoRa TX命令下发测试** — 验证master_cmd通过UART发送命令通路 | **中** | ⬜ |
| **S12** | **LoRa自发自收** — 确认MWCC68D是否支持自环模式 | **低** | ⬜ |

**为什么 RPMsg 管道提前到 S2**: RPMsg 默认载荷上限 495B (`RPMSG_BUFFER_SIZE=512` - `sizeof(rpmsg_hdr)=16` - 1)。
`MasterDownloadBuf_t` 含 `NodeSample_t[80]` = 1280B，远超 495B。必须先确认通道约束，
再围绕约束设计消息格式和分片策略，避免后期全局重构。

---

## 二、最终目标

**将 GD32L233C 节点主控项目的完整业务逻辑，移植到飞腾派 FreeRTOS 侧运行。**

### 源项目（待移植）

| 项目 | 路径 |
|------|------|
| GD32 主控源码 | `/home/alientek/Phytium/GD32L233C_Prj_Master_v3/GD32L233C_Prj_Master/` |
| 目标移植目录 | `/home/alientek/Phytium/freertos/`（已放置部分早期尝试代码） |

### GD32 项目核心模块

```
GD32L233C_Prj_Master/app/
├── main.c / main.h                        # 入口, task_create()
├── task/
│   ├── tasks.h                            # 协议定义: NODE_MAX_COUNT=10, LORA_BUFFER_SIZE=256
│   ├── master_cmd.c                       # 命令下发
│   ├── master_recv.c                      # ★ 核心 — LoRa接收, 解密, 状态机存储
│   ├── master_judge.c                     # 故障判断
│   └── master_sys.c                       # 系统管理
├── chaos_encrypt.c / chaos_encrypt.h      # 混沌加密算法
├── data_frame.c / data_frame.h            # 帧格式定义
├── log.c / log.h                          # 日志系统
└── data_monitor.h                         # 数据监控
```

业务逻辑简述：
- 主控通过 LoRa 无线接收从节点数据
- 支持 3 种数据类型：周期性上传(NodeUploadData_t)、故障上传(FaultUploadHeader_t)、波形数据
- 混沌加密解密
- 接收状态机：IDLE → RECV_NODE_RAW / RECV_FLASH_WAVE
- 帧格式：`AA 55 [len:2] [dev_id:2] [timestamp:2] [type:1] [sync:4] [payload:n]`

---

## 二、移植路线图

移植分两个阶段：

### 阶段一：基础设施（当前进行中）

```
Step1 ✅  共享内存打印（FreeRTOS → Linux 可见）
Step2 ✅  GPIO 控制（MD0/AUX）
Step3 ✅  UART2 轮询接收 LoRa 原始字节
Step4 ⬜  UART2 中断接收（替代轮询）
Step5 ⬜  帧格式解析 + hex dump
```

### 阶段二：业务移植（基础设施完成后）

```
Step6 ⬜  将 GD32 的 data_frame.h / chaos_encrypt 等模块移植到 FreeRTOS
Step7 ⬜  移植 master_recv.c 的接收状态机
Step8 ⬜  移植 master_cmd.c 的命令下发
Step9 ⬜  移植 master_judge.c 的故障判断
Step10 ⬜ 移植 master_sys.c 的系统管理
Step11 ⬜ RPMsg 通道打通：FreeRTOS LoRa 数据 → Linux
Step12 ⬜ 集成测试, 替换 Linux 侧 lora_receiver.c
```

### 数据流全景

```
GD32从节点 ──LoRa无线──> ATK-MWCC68D模块 ──UART2──> FreeRTOS
                                                       │
                                              ┌────────┴────────┐
                                              │  master_recv.c  │
                                              │  chaos_encrypt  │
                                              │  状态机 + 存储   │
                                              └────────┬────────┘
                                                       │ RPMsg
                                                       ▼
                                                     Linux
```

---

## 三、核心文件速查 (v10最新)

| 文件 | 路径 | 说明 |
|------|------|------|
| **主程序** | `Phytium/freertos/main.c` | 核心源码, 与SDK编译目录同步 |
| **一键部署** | `Phytium/freertos/deploy.sh` | 编译+传输+安全启动+验证 |
| **操作手册** | `Phytium/freertos/OPERATIONS.md` | 编译/部署/启停/验证 完整命令 |
| **调试指南** | `Phytium/freertos/DEBUG_GUIDE.md` | 完整调试过程, 所有踩坑记录 |
| **编译入口** | `Phytium_syscode/.../openamp_for_linux/` | SDK 工程目录 (makefile + sdkconfig) |
| GD32 参考 | `Phytium/GD32L233C_Prj_Master_v3/` | 帧格式/加密算法/状态机 权威参考 |
| Linux 侧工具 | `Phytium/src/linux-app/` | trace_reader, lora_receiver 等 |

---

## 四、GD32 帧格式 (移植关键)

### 4.1 帧结构 (来自 GD32 master_recv.c)

```
AA 55 [LEN:2B 大端] [TS:4B] [TYPE:1B] [SYNC:4B] [ENC_DATA:NB] 55 AA
|帧头| |帧数据长度   | |时间戳| |类型  | |同步字  | |加密数据  | |帧尾|
```

### 4.2 帧尾计算 (最容易出错的点)

```c
// GD32 原始代码: tail_pos = i + 5 + frame_data_len
// buf[pos] = AA, buf[pos+1] = 55, buf[pos+2..3] = LEN_2B
// frame_data 从 buf[pos+4], 长度 = data_len
// 帧尾 55AA 在 buf[pos + 5 + data_len]  (注意: +5 不是 +4)
u32 tail_pos = pos + 5 + data_len;

// 加密数据长度: data_len - 9  (TS_4B + TYPE_1B + SYNC_4B = 9)
u16 enc_len = data_len - 9;
```

### 4.3 帧类型

| type | 名称 | enc 大小 | 说明 |
|:--:|------|:--:|------|
| 0x01 | DATA_TYPE_STATUS | 16B | 节点状态上报 → FaultUploadHeader_t |
| 0x02 | DATA_TYPE_WAVE | 变长 | 波形数据 → WaveChunkHeader_t |
| 0x04 | DATA_TYPE_NODE_RAW | 128B | 加密采样 → NodeSample_t[8] |
| 0x06 | DATA_TYPE_FAULT_LIST | 变长 | 故障列表 |

### 4.4 解密

混沌加密算法实现在 `chaos_encrypt.c`, `sync` 字段作为解密种子:
```c
u16 dec_len = chaos_decrypt_packet(enc_start, enc_len, dec_buf, sync);
```

---

## 五、关键踩坑记录 (接手必读)

| # | 陷阱 | 现象 | 修复 |
|:--|------|------|------|
| 1 | **MMU 未映射** | FreeRTOS Data Abort, far=0xC8000000 | FMmuMap 必须在 main() 第一个执行 |
| 2 | **Cache 不可见** | trace_reader 看到旧值/WI=0 | MT_DEVICE_NGNRNE (非缓存直通 DDR) |
| 3 | **双 ISR 冲突** | 数据丢失, 帧不完整 | 删除 lora_uart.c 中断注册, 仅保留 main.c |
| 4 | **帧缓冲区死锁** | 连续心跳无帧输出 | 扩大缓冲 + 满时强制解析 + 帧头偏移搜 AA55 |
| 5 | **帧尾偏移少1字节** | `FX: CRC fail` 全部帧 | tail_pos = pos + 5 + data_len (GD32对齐) |
| 6 | **CRC 误校验** | 所有帧 CRC 失败 | GD32 帧不含 CRC, 仅 AA55/55AA 头尾标记 |
| 7 | **echo stop → RCU stall** | 系统卡死, PANIC at PC | **绝不**在 running 时 stop, 仅 offline 时 start |
| 8 | **线程 sync=0** | 解密失败 type=04 sync=00000000 | enc_start 指针应为 &data[9] 不是 &data[8] |
| 9 | **Mutex-in-ISR 崩溃** | Data Abort 或断言失败 | ISR 中绝不调用 shm_puts/shm_spf (含 Mutex), 仅用 rp_put |
| 10 | **RPMsg dest_addr 未绑定** | rpmsg_send 返回 -2003 (RPMSG_ERR_PARAM) | Linux 内核 rpmsg_create_ept 不发 NS_CREATE; FreeRTOS 侧 dest_addr 需靠收到第一条消息更新; Linux 侧必须先 write() 一条消息 |

---

## 六、关键技术决策

### 6.1 打印：shm_print 模块（Mutex 保护，替代 f_printk 和裸写宏）

```c
// v11-S1: 统一使用 shm_print 模块
// inc/shm_print.h / src/shm_print.c
void shm_puts(const char *s);     // Mutex 保护的字符串写入
void shm_spf(const char *fmt, ...); // Mutex 保护的格式化写入
void shm_clear(void);              // 清零 WI/RI/HB + 缓冲区
void shm_hb_inc(void);             // 心跳递增 (无锁, 单核安全)
void shm_print_init(void);         // 创建 Mutex (main() 中调用)
```

**设计要点**:
- Mutex 仅在调度器运行后生效 (`xTaskGetSchedulerState() == taskSCHEDULER_RUNNING`)
- ISR 中**绝不**调用 `shm_puts/shm_spf`，避免 Mutex-in-ISR 崩溃
- `shm_hb_inc()` 无锁（单核 aligned u32++ 可接受极低概率丢失）
- `log.c` 的 `f_printk` 已替换为 `shm_puts`，所有日志输出走同一 Mutex

**Linux 侧查看**：
```bash
sudo /home/user/trace_reader   # 持续轮询程序
```

### 6.2 MMU 映射铁律

所有外设基址必须先 `FMmuMap` 再访问，在 `main()` 中一次性映射：
```c
FMmuMap(0xC8000000, ...)  // 共享内存 (MT_DEVICE_NGNRNE)
FMmuMap(U2_BASE, ...)     // UART2
FMmuMap(IP_BASE, ...)     // IOPAD (0x32B30000)
FMmuMap(FGPIO2, ...)      // GPIO2 (0x28035000)
FMmuMap(FGPIO3, ...)      // GPIO3 (0x28036000)
```

### 6.3 寄存器直写

所有 GPIO/UART/IOPAD 操作均用宏直写，与 SDK `fgpio_hw.h` 偏移一致。

---

## 七、设备树与 CPU 核心

### 7.1 设备树配置 (开发板 `/proc/device-tree/`)

```
homo_rproc@0/homo_core0@b0100000/
├── compatible = "homo,rproc-core"
├── remote-processor = <3>         ← 逻辑 CPU ID
├── inter-processor-interrupt = <9>  ← IPI 用 SGI #9
├── firmware-name = "openamp_core0.elf"
└── memory-region = <...>
```

### 7.2 逻辑 ID → 物理核映射

```
PE2204 芯片 CPU 核心:
  CORE0: Aff0=0x00 → CPU0 (LITTLE)
  CORE1: Aff0=0x01 → CPU1 (LITTLE)
  CORE2: Aff0=0x02 → CPU2 (LITTLE)
  CORE3: Aff0=0x03 → CPU3 (big)     ← remote-processor=3 指向此核
```

**结论**：设备树 `remote-processor=3` 让 FreeRTOS 由 CPU3 启动，但在 AMP 模式下 FreeRTOS 可能根据 mpidr 选择核心。实测 `MPIDR_EL1=0x80000100` 确认 FreeRTOS 跑在 **CPU1 (big核)**。CPU3 可能被 OP-TEE 占用。

```c
// 启动日志验证
MPIDR=0x80000100 GetCpuId=1 dts=rproc=3   // ← 实测证据
```

---

## 八、编译和部署

### 8.1 一键部署 (推荐)

```bash
cd /home/alientek/Phytium/freertos && bash deploy.sh
```

脚本自动完成: 编译 → 传输 → 安全启动 → 验证数据。详见 [OPERATIONS.md](./OPERATIONS.md)。

### 8.2 为什么 reboot 才能看到新固件

- 开发板 FreeRTOS 固件已 running → `echo stop` 会触发 RCU stall（系统卡死）
- deploy.sh 安全策略: 仅更新 `/lib/firmware/openamp_core0.elf` 文件，不重启
- **下次 reboot** 后 remoteproc 自动加载新固件并启动

### 8.3 开发板信息

| 项 | 值 |
|------|------|
| IP | 192.168.88.11 |
| 用户名 | user |
| 密码 | user |
| sudo 密码 | user |
| FreeRTOS 固件路径 | /lib/firmware/openamp_core0.elf |
| remoteproc 状态 | /sys/class/remoteproc/remoteproc0/state |
| trace_reader 路径 | /home/user/trace_reader |

---

## 九、硬件端口速查

| 功能 | GPIO | 飞腾派 J1 排针 | IOPAD 偏移 | FUNC | 基址 |
|------|------|---------------|-----------|------|------|
| MD0  | GPIO3_1 | Pin 11 | 0x00E0 (C49) | 6=GPIO | 0x28036000 |
| AUX  | GPIO2_10 | Pin 12 | 0x00C4 (A37) | 6=GPIO | 0x28035000 |
| UART2 TX | — | Pin 8 | 0x00D8 (A47) | 0=UART | 0x2800E000 |
| UART2 RX | — | Pin 10 | 0x00DC (A49) | 0=UART | 0x2800E000 |
| 共享内存 | — | — | — | — | 0xC8000000 |
| IOPAD 基址 | — | — | — | — | 0x32B30000 |

**UART2 PL011 寄存器**：DR=0x00, FR=0x18, IBRD=0x24, FBRD=0x28, LCR_H=0x2C, CR=0x30  
**GPIO 寄存器**：DR=0x00, DDR=0x04, EXT=0x08  
**波特率**：115200 ← 100MHz 时钟, IBRD=54, FBRD=16

---

## 十、给接手 AI 的重要规则

1. **不要自己重启开发板** — 用户来重启，你只负责编译和 scp 部署。
2. **不要一直打印** — 只在引脚状态变化、关键事件时打印，30s 心跳即可。
3. **每一步先给计划让用户审阅**，确认后再执行。
4. **不确定的不盲目试错** — 停下来让用户判断方向。
5. **代码在 `/home/alientek/Phytium/freertos/` 开发，deploy.sh 自动同步到 SDK 目录编译**。
14. **deploy.sh 安全铁律**：固件 running 时绝不 `echo stop`，会 RCU stall → 系统卡死。仅在 offline 时 start。
6. **飞腾派连接信息**: IP=`192.168.88.11`, 用户名=`user`, 密码=`user`, sudo密码=`user`。
7. **写共享内存前必须先 `FMmuMap(0xC8000000)`**，直接 volatile 写，别用 `f_printk`。
8. **访问任何外设寄存器前必须先 `FMmuMap` 其基址**，否则 CPU 异常 → 崩溃 → write_index=0。
9. **验证固件是否存活**: `ssh` 到开发板执行 `sudo busybox devmem 0xC8000000 32`，非零 → 存活。
10. **致命错误排查**：write_index=0 第一反应 → 缺 MMU 映射 → 检查最近新访问的基址是否映射。
11. **虚拟机**：虚拟机密码是12345。
12. **文档补充**：/home/alientek/Phytium/docs目录下有之前的飞腾派的调试和整个项目的文档，如果遇到问题可以先去这里查看。还有/home/alientek/Phytium/README.md和/home/alientek/Phytium/PROJECT_INFO.md
13. **规则补充**：12条规则模板，核心要点：
规则
“要点
1.先想后写
明确假设，不确定就问，保持简单
2.简单优先
最小代码解决已提需求，不过度抽象
3.精准修改
只改必要的，不顺手“优化”无关代码
4.目标驱动
先定义成功标准，循环迭代直到验证通过
5.模型只做判断
分类/起草/总结用模型，路由/重试/确定性转换用代码
7.暴露冲突
遇到矛盾选一种并说明原因，不混合
8.先读后写
加代码前先读导出、调用方、共享工具
9.1
测试编码意图
测试要编码“为什么"，不只是“做什么"
10.每步设检查点
总结已完成/已验证/剩余，失去跟踪就停下
11.遵循代码库约定
即使不同意也遵循，有问题提出来
12.大声失败
跳过任何东西就不能说“完成”，默认暴露不确定性
”

---

## 附录A: S2 RPMsg 管道调试 — dest_addr 未绑定根因分析

### A.1 问题现象

FreeRTOS 侧 `rpmsg_send()` 始终返回 -2003 (`RPMSG_ERR_PARAM`)，endpoint 的 `dest_addr` 始终为 `RPMSG_ADDR_ANY (0xFFFFFFFF)`。

### A.2 根因

**Linux 内核的 `rpmsg_create_ept` 不会发送 NS_CREATE 消息！**

对比两端的 `rpmsg_create_ept` 实现：

| | FreeRTOS (OpenAMP) | Linux (virtio_rpmsg_bus) |
|---|---|---|
| 创建 endpoint | `rpmsg_create_ept()` | `__rpmsg_create_ept()` |
| 发送 NS_CREATE? | **是** — `rpmsg_send_ns_message(ept, RPMSG_NS_CREATE)` | **否** — 只分配地址和注册回调 |

**结果**：
1. FreeRTOS `rpmsg_create_ept` → 发 NS_CREATE → Linux `rpmsg_ns_cb` → 创建 rpmsg_device ✅
2. Linux 用户态 `open("/dev/rpmsg0")` → `rpmsg_create_ept` → **不发 NS_CREATE** → FreeRTOS 不知道 Linux endpoint 地址 ❌

### A.3 dest_addr 的两种更新机制

OpenAMP 中 `dest_addr` 可以通过以下方式更新：

1. **NS 消息** — `rpmsg_virtio_ns_callback` 收到 NS_CREATE 时，查找同名 endpoint 并设置 `dest_addr = ns_msg.addr`
2. **首条数据消息** — `rpmsg_virtio_rx_callback` 中：
   ```c
   if (ept->dest_addr == RPMSG_ADDR_ANY) {
       ept->dest_addr = rp_hdr->src;  // 从收到的第一条消息中获取远端地址
   }
   ```
3. **自定义回调** — 我们的 `rpmsg_endpoint_cb` 中 `ept->dest_addr = src;`

**由于 Linux 不发 NS_CREATE，只有机制 2 和 3 可用，前提是 Linux 先发一条消息。**

### A.4 修复方案

在 `rpmsg_recv.c` 打开 endpoint 设备后，先发送一条 ECHO_REQ 消息：

```c
RpmsgPkt hello = { .command = CMD_ECHO_REQ, .length = 5 };
memcpy(hello.data, "HELLO", 5);
write(fd, &hello, RPMSG_PKT_HDR_SIZE + hello.length);
```

FreeRTOS 收到后：
1. `rpmsg_virtio_rx_callback` 更新 `dest_addr = rp_hdr->src`
2. `rpmsg_endpoint_cb` 再次确认 `ept->dest_addr = src`
3. ECHO_REQ 处理回复 ECHO_RESP → 验证双向通信
4. 之后 FreeRTOS → Linux 的 `rpmsg_send()` 不再返回 -2003

### A.5 另一个修复点

`rpmsg_recv.c` 中 `eptinfo.dst` 原设为 `0xFFFFFFFF`，应改为 `0`（FreeRTOS endpoint 的 src 地址）。

### A.6 完整的 RPMsg 通信流程

```
1. Linux: echo start > remoteproc0/state → 按设备树/remoteproc 口径启动目标核（实测 FreeRTOS 在 CPU1，设备树写 CPU3）
2. FreeRTOS: rpmsg_create_ept("rpmsg-openamp-demo-channel", src=0, dst=ANY)
   → 发送 NS_CREATE → Linux rpmsg_ns_cb → 创建 rpmsg_device
3. Linux: bind rpmsg_chrdev → rpmsg_chrdev_probe → /dev/rpmsg_ctrl0
4. Linux: ioctl(RPMSG_CREATE_EPT_IOCTL) → rpmsg_eptdev_create → /dev/rpmsg0
5. Linux: open("/dev/rpmsg0") → rpmsg_eptdev_open → rpmsg_create_ept
   → Linux endpoint 创建 (不发 NS_CREATE!)
6. Linux: write(ECHO_REQ) → 发送 RPMsg 数据消息到 FreeRTOS
7. FreeRTOS: rpmsg_virtio_rx_callback → dest_addr = rp_hdr->src (更新!)
8. FreeRTOS: rpmsg_endpoint_cb → ECHO_REQ → rpmsg_send(ECHO_RESP) → 成功!
9. 之后 FreeRTOS → Linux 的 rpmsg_send() 正常工作
```

---

## 附录B: 后续优化待办

| 编号 | 优化项 | 说明 | 优先级 |
|:--:|------|------|:--:|
| O1 | dl_buf → 共享内存直写 | 当前: 收包→dl_buf(1.3KB)累积→攒齐→写共享内存; 优化: 收包→直接追加写共享内存→收齐后标记就绪, 省掉1.3KB中间缓冲, 数据更早可见 | 低 |
| O2 | 共享内存 erase 策略优化 | 当前: master_flash_save_node_data 先 erase 整块再写, Linux 侧可能看到数据清零瞬间; 优化: 追加写+就绪标志, 避免 erase | 低 |

---

## 附录C: S8 模拟数据注入测试 (2026-05-21)

### C.1 背景

LoRa 节点在1km外室内发送，飞腾派无法收到信号（isr=21 无新中断，tx=0 无帧解析）。为验证状态机逻辑，在 FreeRTOS 侧构造模拟 LoRa 帧，注入到 `process_lora_frame`，无需外部 LoRa 节点。

### C.2 实现方案

在 `main.c` 中新增：

1. **`build_lora_frame()`** — 构造完整 LoRa 帧（AA55 + LEN + TS + TYPE + SYNC + 混沌加密载荷 + 1B保留 + 55AA），与 `process_lora_frame` 解析格式完全一致
2. **`inject_sim_frames()`** — 依次注入3种模拟帧：
   - STATUS帧：FaultUploadHeader_t（node=1, severity=WARNING, fault=OVER_VOLTAGE, points=8）
   - NODE_RAW帧：8个 NodeSample_t 采样点（active_power=1000+i*100 等）
   - FAULT_LIST帧：4字节故障列表
3. **自动触发**：lora_task 启动3秒后（loop_cnt>1500），如果 `g_real_frame_cnt==0`（无真实帧），自动注入一次

### C.3 关键发现：帧格式中1字节保留字段

**问题**：首次编译部署后，`process_lora_frame` 返回 consumed=0，模拟帧全部解析失败。

**调试输出**：
```
BUILD: type=01 plen=20 enc=20 sync=03E807D0 dlen=29 total=35
PLF: tail overflow dlen=29 buf=35 tail=34
```

**根因**：`process_lora_frame` 中 `tail_pos = pos + 5 + data_len`（+5而非+4），意味着帧结构中 LEN 和 data 之间有1字节间隔（可能是CRC或保留字段）。`build_lora_frame` 原来按 `total = 2+2+data_len+2` 计算，少了这1字节。

**修复**：
```c
// build_lora_frame 中
u32 total = 2 + 2 + data_len + 1 + 2;  // +1 是保留字节
// ...
memcpy(&out[p], enc_buf, enc_len); p += enc_len;
out[p++] = 0x00;  // 1字节保留（与 process_lora_frame 的 pos+5 对齐）
out[p++] = 0x55; out[p++] = 0xAA;
```

### C.4 验证结果

修复后模拟注入成功：
```
=== INJECT: sim frames ===
BUILD: type=01 plen=20 enc=20 sync=03E807D0 dlen=29 total=36
=== FRAME #1: type=01 ts=1000 sync=03E807D0 enc=20B dec=20B ===
INJECT STATUS: 36B consumed=36

BUILD: type=04 plen=128 enc=128 sync=24201E54 dlen=137 total=144
=== FRAME #2: type=04 ts=1000 sync=24201E54 enc=128B dec=128B ===
INJECT NODE_RAW: 144B consumed=144

BUILD: type=06 plen=4 enc=4 sync=493D532F dlen=13 total=20
=== FRAME #3: type=06 ts=1000 sync=493D532F enc=4B dec=4B ===
[00:00:14.800] [INFO]  Fault list: node1=4 valid/4
INJECT FAULT_LIST: 20B consumed=20
=== INJECT: done ===
```

- ✅ 3帧模拟数据全部正确解析（consumed = total）
- ✅ 混沌加密/解密正常（enc_len = dec_len）
- ✅ FAULT_LIST 状态机处理正确（node1=4 valid/4）
- ✅ `g_real_frame_cnt=3`，心跳显示 `real=3`
- ⚠️ RPMsg 发送3次全部失败（rpmsg=0/3），因为 Linux 侧未运行 rpmsg_recv，dest_addr 未绑定
- ⚠️ STATUS 和 NODE_RAW 的状态机处理结果（Fault hdr / Status hdr / SAVE）未在 trace_reader 中看到，可能被其他日志淹没或 log_level 过滤

### C.5 待验证项（下次上电后）

1. **STATUS 帧状态机** — 确认 `process_status_header` 是否正确设置 dl_buf（active=1, expected_points=8）
2. **NODE_RAW 帧累积** — 确认 `process_node_raw` 是否正确累积8个采样点并触发 `master_flash_save_node_data`
3. **Judge 任务联动** — STATUS 设置 severity=WARNING + fault_type=OVER_VOLTAGE 后，Judge 是否自动发送 REQ_WAVE 命令
4. **RPMsg 通道** — Linux 侧运行 rpmsg_recv 后，模拟数据是否通过 RPMsg 传到 Linux
5. **去掉调试输出** — 验证通过后删除 `build_lora_frame` 和 `process_lora_frame` 中的 shm_spf 调试行

### C.6 代码变更清单

| 文件 | 变更 |
|------|------|
| `main.c` | 新增 `g_real_frame_cnt` 变量、`build_lora_frame()` 函数、`inject_sim_frames()` 函数 |
| `main.c` | `process_lora_frame` 中添加 `g_real_frame_cnt++` 和调试 shm_spf |
| `main.c` | `lora_task` 中添加 `sim_injected` 标志和自动注入触发逻辑 |
| `main.c` | 心跳打印增加 `real=` 字段 |

### C.7 deploy.sh 注意事项

当前 `deploy.sh` 在 remoteproc running 时跳过重启（5b分支）。更新固件后需手动 stop/start：
```bash
ssh user@192.168.88.11
sudo sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state'
sleep 3
sudo sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
```
或修改 deploy.sh 使其在固件更新后自动 stop→start（需用户确认是否接受 RCU stall 风险）。
