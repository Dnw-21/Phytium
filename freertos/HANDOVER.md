# FreeRTOS LoRa 主控移植项目 — 交接文档

> 编写日期: 2026-06-02  |  最后更新: 2026-06-02
> 状态: **移植完成，编译通过，等待实物联调**

---

## 一、当前进度总览

### 1.1 已完成

| 阶段 | 说明 | 状态 | 验证 |
|------|------|:--:|------|
| 共享内存打印 | FreeRTOS → Linux 通过 0xC8000000 通信 | ✅ | trace_reader 实时可见 |
| GPIO 控制 | MD0/AUX LoRa模块控制 | ✅ | 已验证 |
| UART2 PL011 | 115200 8N1, 100MHz 时钟 | ✅ | 收发正常 |
| LoRa AT 配置 | AT命令配置, WLRATE=23,5 | ✅ | 已验证 |
| GICv3 中断 | UART2 IRQ117 → CPU1 | ✅ | ISR 递增 |
| RPMsg 通道 | FreeRTOS ↔ Linux 通信 | ✅ | rpmsg host is online |
| 安全部署 | deploy.sh 条件启动 | ✅ | 无 RCU stall |
| **GD32 主控移植** | **全部业务逻辑移植完成** | ✅ | **编译通过 ELF 707K** |
| 混沌加密 | 8字节 sync_code, uint64_t | ✅ | 与终端节点对齐 |
| 任务架构分离 | recv_task + process_task 分离 | ✅ | 队列解耦 |
| 两层轮询 | Tier1(CMD_POLL_STATUS) + Tier2(CMD_REQUEST_FAULT_DATA) | ✅ | 与 GD32 一致 |
| 共享内存存储 | g_status_buf 模拟 Flash | ✅ | 存储 NodeSample_t |
| CMD_NODE_STATUS | RPMsg 命令码预留 | ✅ | 0x0025 |

### 1.2 当前任务架构

```
main()
  ├── rpm_task            (优先级1) — RPMsg 通道
  ├── aux_task            (优先级2) — AUX 监控
  ├── master_recv_task    (优先级5) — LoRa 接收 + 帧解析 + 入队
  ├── master_process_task (优先级4) — 队列出队 + 混沌解密 + 数据存储
  ├── master_judge_task   (优先级3) — 节点超时检测
  └── master_poll_task    (优先级2) — Tier1+Tier2 两层轮询
```

### 1.3 数据流

```
GD32终端节点 ──LoRa无线──> ATK-MWCC68D模块 ──UART2──> FreeRTOS CPU1
                                                          │
                                                  ┌───────┴────────┐
                                                  │ master_recv_task│  ISR → 环形缓冲 → 软超时 → 帧解析
                                                  │   → g_recv_queue│  入队 RecvPacket_t
                                                  └───────┬────────┘
                                                          │
                                                  ┌───────┴────────┐
                                                  │master_process   │  出队 → chaos_decrypt → 按类型存储
                                                  │  → g_status_buf │  共享内存模拟 Flash
                                                  │  → RPMsg(预留)  │  CMD_NODE_STATUS(0x0025)
                                                  └────────────────┘
```

### 1.4 待完成

| 步骤 | 任务 | 优先级 | 状态 |
|:--:|------|:--:|:--:|
| 1 | 实物联调（开发板上电+LoRa+终端节点） | 高 | ⬜ |
| 2 | Linux 侧 RPMsg 接收程序（CMD_NODE_STATUS） | 高 | ⬜ |
| 3 | 数据桥接（int16/10000 → float）→ UKF 输入 | 高 | ⬜ |
| 4 | 数据文件存储（接收日志持久化） | 中 | ⬜ |
| 5 | 多节点并发测试 | 低 | ⬜ |

---

## 二、核心文件速查

| 文件 | 路径 | 说明 |
|------|------|------|
| **主程序** | `freertos/main.c` | 入口 + UART寄存定义 + 任务创建 |
| **接收任务** | `freertos/src/master_recv.c` | master_recv_task + master_process_task |
| **轮询任务** | `freertos/src/master_poll_task.c` | Tier1+Tier2 两层轮询 |
| **节点管理** | `freertos/src/master_sys.c` | 共享内存存储 + 节点信息管理 |
| **故障判断** | `freertos/src/master_judge.c` | 节点超时检测 |
| **帧格式** | `freertos/inc/data_frame.h` | NodeSample_t(52B), NodeUploadHeader_t, sync_code(uint64_t) |
| **混沌加密** | `freertos/src/chaos_encrypt.c` | 8字节 sync_code 加密/解密 |
| **LoRa UART** | `freertos/src/lora_uart.c` | UART2 驱动 + 环形缓冲 + 软超时 |
| **RPMsg协议** | `freertos/inc/rpmsg_proto.h` | CMD_LORA_RAW/PARSED/NODE_STATUS |
| **一键部署** | `freertos/deploy.sh` | 编译+传输+安全启动+验证 |
| **操作手册** | `freertos/OPERATIONS.md` | 编译/部署/启停/验证 完整命令 |
| **GD32 基准** | `GD32L233C_Prj_Master_v3/` | 移植参考基准 |
| **终端节点** | `GD32L233C_Prj/` | 终端节点工程（与主控兼容） |

---

## 三、GD32 帧格式 (移植基准)

### 3.1 帧结构

```
AA 55 [LEN:2B大端] [TS:4B] [TYPE:1B] [SYNC:8B] [ENC_DATA:nB] [CRC8:1B] 55 AA
|帧头| |帧数据长度  | |时间戳| |类型  | |同步字  | |加密数据  | |CRC  | |帧尾|
```

### 3.2 帧类型

| type | 名称 | 说明 |
|:--:|------|------|
| 0x01 | DATA_TYPE_NODE_HEAD | 节点状态头 → NodeUploadHeader_t |
| 0x03 | DATA_TYPE_POWER | 功率数据（保留） |
| 0x04 | DATA_TYPE_NODE_RAW | 加密采样 → NodeSample_t[] |
| 0x07 | DATA_TYPE_FAULT_HEAD | 故障快照头 |

### 3.3 NodeSample_t 结构 (52字节)

```c
typedef struct {
    int16_t  pg1, pg2, pg3;        // 3×发电机有功 (×10000)
    int16_t  qg1, qg2, qg3;        // 3×发电机无功 (×10000)
    int16_t  vmag1..vmag9;        // 9×母线电压幅值 (×10000)
    int16_t  vangle1..vangle9;     // 9×母线电压相角 (×10000)
    uint32_t timestamp;            // 时间戳 (ms)
} NodeSample_t;
```

### 3.4 命令码

| 命令 | 值 | 方向 | 说明 |
|------|:--:|------|------|
| CMD_POLL_STATUS | 0x14 | 主控→终端 | 轮询节点状态，带时间戳 |
| CMD_REQUEST_FAULT_DATA | 0x15 | 主控→终端 | 请求故障快照数据 |

---

## 四、状态估计兼容性

终端节点 NodeSample_t 与 state_estimation 模拟数据格式对应：

| 终端字段 | state_estimation | 转换 |
|----------|-----------------|------|
| pg1/2/3 (int16) | PG1/2/3 (float) | ÷10000 |
| qg1/2/3 (int16) | QG1/2/3 (float) | ÷10000 |
| vmag1~9 (int16) | Vmag1~9 (float) | ÷10000 |
| vangle1~9 (int16) | Vangle1~9 (float) | ÷10000 |
| timestamp (uint32 ms) | timestamp (float s) | ÷1000 |

节点-母线对应：node0→buses[0,3,4], node1→buses[1,5,6], node2→buses[2,7,8]

---

## 五、关键踩坑记录

| # | 陷阱 | 现象 | 修复 |
|:--|------|------|------|
| 1 | MMU 未映射 | Data Abort, far=0xC8000000 | FMmuMap 必须第一个执行 |
| 2 | sync_code 4字节→8字节 | 混沌解密失败 | 改 uint64_t，组合 x/y 状态位 |
| 3 | enc_len 计算偏移 | 解析出来的数据长度不对 | enc_len = frame_data_len - 13（8B sync + 1B type + 4B ts） |
| 4 | echo stop → RCU stall | 系统卡死 | 绝不 running 时 stop，仅 offline 时 start |
| 5 | 队列满丢帧 | [RECV] queue full | RECV_QUEUE_LENGTH=16，需实物验证 |
| 6 | 旧文件编译错误 | undefined reference | 删除 wave_decode.c, power_system.c 等 |
| 7 | g_recv_queue 未声明 | undefined reference | main.c 中全局声明 |