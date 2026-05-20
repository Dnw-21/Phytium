# FreeRTOS LoRa 接收项目 — 交接文档

> 编写日期: 2026-05-26  |  最后更新: 2026-05-21
> 状态: **v10 Final — 生产就绪** ✅  LoRa 帧完整解析 + 安全部署已验证

---

## 一、当前进度总览 (v10 Final)

### 1.1 已完成

| 阶段 | 说明 | 状态 | 验证 |
|------|------|:--:|------|
| 共享内存打印 | FreeRTOS → Linux 通过 0xC8000000 通信 | ✅ | trace_reader 实时可见 |
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
                                 │ → spf() 写共享内存      │
                                 └─────────┬─────────┘
                                           │
                                    0xC8000000 (1MB, MT_DEVICE_NGNRNE)
                                           │
                                     ┌─────┴─────┐
                                     │ trace_reader│ /dev/mem mmap
                                     └─────────────┘
                                         终端实时打印
```

### 1.3 待完成

| # | 任务 | 优先级 |
|---|------|:--:|
| 1 | RPMsg 打通: FreeRTOS LoRa 数据 → Linux 结构化传递 | 高 |
| 2 | 移植 master_recv.c 完整状态机 (当前简化版只解析不存储) | 高 |
| 3 | 移植 master_judge.c 故障判断 | 中 |
| 4 | 移植 master_cmd.c 命令下发 | 中 |
| 5 | 移植 master_sys.c 系统管理 | 低 |
| 6 | 集成测试, 替换 Linux 侧 lora_receiver.c | 低 |
| 7 | spf() 输出互斥 (多任务并发写共享内存会字符交错) | 低 |

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

---

## 六、关键技术决策

### 6.1 打印：裸写共享内存（不用 f_printk）

```c
#define SHM_BASE  ((volatile u32 *)0xC8000000UL)
#define SHM_WI    (SHM_BASE[0])     // write_index
#define SHM_RI    (SHM_BASE[1])     // read_index
#define SHM_BUF   ((volatile char *)(SHM_BASE + 3))

// 宏裸写 volatile, 零函数调用
#define put(c) do { u32 w=SHM_WI, r=SHM_RI, n=(w+1)%SHM_BSZ; \
                    if(n!=r){SHM_BUF[w]=(c);SHM_WI=n;} }while(0)
#define puts(s) do { const char *p=(s); while(*p) put(*p++); } while(0)
```

`f_printk` 在多任务环境下不可靠（write_index 始终为 0），裸写 volatile 是唯一已验证可靠的方式。

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
