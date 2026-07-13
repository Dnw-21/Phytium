# GD32L233C RAM 分析报告

> **芯片**: GD32L233C — SRAM **32 KB** (32,768 bytes)
>
> **日期**: 2026-05-24
>
> **当前参数**:
> - `NODE_SAMPLE_RATE` = 1000 (1kHz)
> - `SAMPLES_PER_CYCLE` = 20
> - `NODE_BUFFER_CYCLES` = 5
> - `NODE_BUFFER_SIZE` = 100 点
> - `LORA_SEND_QUEUE_LEN` = 20 ⚠️ 待优化
> - `SAMPLE_DATA_QUEUE_LEN` = 20

---

## 一、数据结构 Sizeof 汇总

| 结构体 | 字段 | sizeof(估算) |
|--------|------|-------------|
| `Node1DataPoint_t` | 8×float | **32 B** |
| `NodeSample_t` | 8×int16_t + uint32_t | **20 B** |
| `SensorData_t` | uint32_t + 8×float | **36 B** |
| `LoRaSendPacket_t` | enum + uint8_t + uint8_t[240] + LoRaSrc_t(4) + uint32_t | **~252 B** |
| `LoRaRecvPacket_t` | enum + uint32_t + uint16_t + uint8_t + uint8_t[128] | **~140 B** |
| `FaultUploadHeader_t` | 6 fields | **~20 B** |
| `NodeUploadData_t` | 5 fields + float + uint16_t | **~22 B** |
| `WaveBuffer_t` | int16_t[2][64] + 11 fields | **~296 B** |
| `FaultRecord_t` | 10 fields | **~28 B** |
| `MidFreqWindow_t` | float[10] + 5 fields | **~65 B** |
| `WaveChunkHeader_t` | 6 fields | **~18 B** |
| `Command_t` | uint8_t + uint8_t[16] | **17 B** |
| `HealthState_t` | 5 fields (4×float + enum) | **~20 B** |
| `ChaosParams_t` | 6×float + float[6] | **48 B** |

---

## 二、全局/静态变量明细

### 2.1 核心数据缓冲区

| 变量 | 文件 | 计算 | 大小 |
|------|------|------|------|
| `g_node` NodeBuffer_t | data_monitor.c | 100点×20B + write_index(2) + cycle_count(2) | **~2,004 B** |
| `g_mid` MidFreqWindow_t | data_monitor.c | float[10]=40 + float×4=16 + uint16_t×2=4 + align | **~65 B** |

### 2.2 仿真数据 (Flash/.rodata)

| 变量 | 计算 | 大小 |
|------|------|------|
| `g_node1_normal[60]` | 60 × 32B | 1,920 B |
| `g_node1_fault[20]` | 20 × 32B | 640 B |

> ⚠️ `const` 修饰，编译器通常放入 `.rodata`（Flash），不计 RAM。
> 但某些链接脚本会把 `.rodata` 也放 SRAM，需确认 `*.ld` 文件。

### 2.3 波形/Flash 相关

| 变量 | 计算 | 大小 |
|------|------|------|
| `g_wave` WaveBuffer_t | int16_t[2][64]=256 + 字段 | **~296 B** |
| `g_fault_records[12]` | 12 × 28B | **336 B** |
| `g_dac_lut[1024]` uint16_t | 1024 × 2B | **2,048 B** |

### 2.4 通信与加密

| 变量 | 计算 | 大小 |
|------|------|------|
| `rxBuffer[512]` (mwcc68_app.c) | 512 × 1B | **512 B** |
| `g_key_stream[256]` (chaos_encrypt.c) | 256 × 1B | **256 B** |
| `g_encrypted_buffer[128]` (lora_send_task.c) | 128 × 1B | **128 B** |

### 2.5 其它静态变量

| 变量 | 大小 |
|------|------|
| `g_health` HealthState_t | ~20 B |
| `g_params + g_x + g_y` (混沌) | ~56 B |
| 各种 flag、计数器合计 | ~150 B |

### 2.6 全局/静态变量汇总

| 类别 | 大小 |
|------|------|
| 节点数据环缓冲 | 2,004 B |
| DAC LUT | 2,048 B |
| 仿真数据 (若入RAM) | 2,560 B |
| 波形/故障记录 | 140 B |
| 通信缓冲 | 896 B |
| 中频窗口 + 健康度 + 其它 | ~290 B |
| 上传静态缓冲区 (pkt_buf×2+raw×2) | 960 B |
| **小计** | **~9,390 B** (不含仿真) / **~11,950 B** (含仿真) |

---

## 三、FreeRTOS 队列

| 队列 | 容量×单项 | 大小 |
|------|----------|------|
| `g_lora_send_queue` | **20** × ~252B | **~5,200 B** ⚠️ |
| `g_sampledata_queue` | **20** × 36B | ~880 B |
| `g_lora_recv_queue` | 3 × ~140B | ~441 B |
| `g_cmd_queue` | 5 × 17B | ~125 B |
| **小计** | | **~6,646 B** |

---

## 四、任务栈

| 任务 | 栈大小(words) | 实际(B) |
|------|---------------|---------|
| lora_send_task | 512 | 2,048 B |
| data_collect_task | 256 | 1,024 B |
| data_judge_task | 256 | 1,024 B |
| status_upload_task | 256 | 1,024 B |
| wave_upload_task | 256 | 1,024 B |
| lora_recv_task | 256 | 1,024 B |
| warning_task | 128 | 512 B |
| danger_task | 128 | 512 B |
| **小计** | | **8,192 B** |

---

## 五、FreeRTOS 内核开销

| 项目 | 大小 |
|------|------|
| 8× TCB (~100B/个) | ~800 B |
| 4× 队列头结构 | ~400 B |
| 事件组 + 互斥锁 | ~200 B |
| 空闲任务栈 + 定时器任务栈 | ~600 B |
| 内核链表/就绪表 | ~300 B |
| **小计** | **~2,300 B** |

---

## 六、汇总

### 场景 A: 仿真数据在 Flash

| 类别 | RAM 占用 | 百分比 |
|------|----------|--------|
| 全局/静态变量 | 9,390 B | 35.1% |
| 队列 | 6,646 B | 24.8% |
| 任务栈 | 8,192 B | 30.6% |
| FreeRTOS 内核 | 2,300 B | 8.6% |
| **总计** | **≈ 26.5 KB** | **82.9%** |
| **剩余** | **≈ 5.5 KB** | **17.1%** |

### 场景 B: 仿真数据在 RAM (.rodata 入 SRAM)

| 类别 | RAM 占用 | 百分比 |
|------|----------|--------|
| 全局/静态变量 | 11,950 B | 41.0% |
| 队列 | 6,646 B | 22.8% |
| 任务栈 | 8,192 B | 28.1% |
| FreeRTOS 内核 | 2,300 B | 7.9% |
| **总计** | **≈ 29.1 KB** | **90.8%** |
| **剩余** | **≈ 2.9 KB** | **9.2%** |

---

## 七、问题分析

### 7.1 `g_lora_send_queue` 是最大浪费源

`LORA_SEND_QUEUE_LEN = 20`，每项 ~252B，合计 **~5.2 KB** — 占总 RAM 的 16%。

实际需求分析：
- 正常上传: `upload_raw_cycles` 每 25s 发一次，40 点需要 40/6 ≈ 7 包 → 7 项入队
- 故障上传: `upload_fault_data` 40 点 ≈ 7 包，只在故障时触发
- 波形上传: `wave_retrieve_by_node_fault` 每包 128B，按需触发
- 峰值场景: 故障发生瞬间，状态上传 + 故障数据上传 ≈ 14 包同时入队

→ **`LORA_SEND_QUEUE_LEN = 10` 即可满足需求。**

### 7.2 `SAMPLE_DATA_QUEUE_LEN = 20` 偏大

`data_collect_task` 1ms 采样一次，`data_judge_task` 5ms 消费一次。
最坏情况下队列堆积: 5ms / 1ms = 5 个，×2 安全余量 = 10 足够。

→ **`SAMPLE_DATA_QUEUE_LEN = 10` 安全可行。**

---

## 八、优化建议

### 🔴 优先级 1 — 立即执行

#### ① 减小 `LORA_SEND_QUEUE_LEN`: 20 → 10

```c
// tasks.h
#define LORA_SEND_QUEUE_LEN  10
```

**节省**: 10 × 252B = **2,520 B**

#### ② 减小 `SAMPLE_DATA_QUEUE_LEN`: 20 → 10

```c
// tasks.h
#define SAMPLE_DATA_QUEUE_LEN  10
```

**节省**: 10 × 36B = **360 B**

### 🟡 优先级 2 — 推荐执行

#### ③ LoRaSendPacket_t 使用指针替代内嵌大数组

```c
// tasks.h — 改前
typedef struct {
    DataType_t data_type;
    uint8_t data_len;
    uint8_t data[240];      // 内嵌 240B
    LoRaSrc_t dest;
    uint32_t timestamp;
} LoRaSendPacket_t;

// tasks.h — 改后
typedef struct {
    DataType_t data_type;
    uint8_t data_len;
    const uint8_t *data_ptr; // 指针 4B 替代 240B
    LoRaSrc_t dest;
    uint32_t timestamp;
} LoRaSendPacket_t;
```

> ⚠️ 要求: `data_monitor.c` 中 `static uint8_t raw[240]` 的指针在发送完成前不能被覆盖。当前 `lora_send_task` 消费队列项时执行 `send_node_data_with_ack`（阻塞式），数据在被消费完成前不会被覆盖，**天然满足要求**。

**节省**: 10 项 × (252-16) = **2,360 B**

### 🟢 优先级 3 — 任务栈微调

| 任务 | 当前 | 建议 | 节省 |
|------|------|------|------|
| data_collect_task | 256 | 192 | 256 B |
| data_judge_task | 256 | 192 | 256 B |
| status_upload_task | 256 | 192 | 256 B |
| lora_recv_task | 256 | 192 | 256 B |

> ⚠️ 减小栈需要运行时验证 `uxTaskGetStackHighWaterMark()` 确认余量。

---

## 九、优化效果对比

| 配置 | g_lora_send 队列 | 采样队列 | 总 RAM | 占用率 |
|------|-----------------|---------|--------|--------|
| 当前代码 (int16优化后) | 20×252B=5,200B | 20×36B=880B | 29.1 KB | 90.8% |
| 仅 P1 (队列优化) | 10×252B=2,600B | 10×36B=360B | 26.2 KB | 82.0% |
| P1+P2 (指针优化) | 10×16B=160B | 10×36B=360B | 23.8 KB | 74.5% |
| P1+P2+P3 (全优化) | 10×16B=160B | 10×36B=360B | 22.8 KB | 71.1% |

---

## 十、风险点

| 风险 | 说明 |
|------|------|
| `.rodata` 入 SRAM | 若链接脚本将 `.rodata` 放入 SRAM，额外消耗 2,560B（仿真数据），建议确认 `*.ld` 文件 |
| `printf` 内部缓冲 | 标准库 `printf` 内部可能有 I/O 缓冲 (~1KB)，若开启浮点打印影响更大 |
| `task.h` 头文件栈 | FreeRTOS 内部断言、trace 宏也可能消耗隐式 RAM |
| 静态局部变量 | `upload_fault_data` / `upload_raw_cycles` 中各有一组 `static int32_t pkt_buf[60]` (240B) + `static raw[240]` = **480B × 2 = 960B** |

---

## 十一、上传数据格式（含 int16 优化后）

### 11.1 上传格式

每个采样点打包为 **5 个 int32_t**（8 个 int16_t 两两合并 + 1 个 timestamp）：

```c
int32_t[0]: [reactive_power(hi16) | active_power(lo16)]
int32_t[1]: [voltage_mag_4(hi16)  | voltage_mag_1(lo16)]
int32_t[2]: [voltage_angle_1(hi16) | voltage_mag_5(lo16)]
int32_t[3]: [voltage_angle_5(hi16) | voltage_angle_4(lo16)]
int32_t[4]: timestamp
```

| 指标 | 之前 (int32×9) | 现在 (int16合并) |
|------|:---:|:---:|
| 每点在 pkt_buf 中的 int32 槽位 | 9 | 5 |
| 每点传输字节 | 36 B | 20 B |
| 每包最多点数 (240B) | 6 | **12** |
| pkt_buf 大小 | int32_t[54] = 216B | int32_t[60] = 240B |
| 上传 40 点所需包数 | 7 包 | 4 包 |

### 11.2 `upload_raw_cycles` / `upload_fault_data` 静态缓冲区

两个函数各有一组 `static` 局部变量:

```c
static int32_t pkt_buf[60];   // 240 B
static uint8_t  raw[240];     // 240 B
```

合计: **480 B × 2 = 960 B**

如果进一步优化，可以将这两个 buffer 提升为文件作用域共用:

```c
// data_monitor.c 文件作用域
static int32_t g_tx_pkt_buf[60];  // 240 B
static uint8_t  g_tx_raw[240];    // 240 B
```

节省: **480 B**

---

*报告结束*
