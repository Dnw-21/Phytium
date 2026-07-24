# 调试记录 — 2026-07-16

## 问题背景

飞腾派作为主控（master），通过 LoRa 轮询多个终端节点（node0/node1/node2），接收状态数据和采样点后通过 RPMsg 转发到 Linux 上层。

---

## 问题 1: 全局共享 dl 导致多节点数据互相干扰

### 现象

开启 node0 和 node1 同时在线时，只有一个节点的 RAW 数据能被正常上传，另一个节点的数据静默丢失。日志中只看到 node1 的 RAW 帧，node0 的 NODE_STATUS header 能到 Linux 但之后无数据。

### 根因

`MasterDownloadBuf_t g_dl_buf` 是全局单例，所有节点共用同一个下载缓冲区。`master_process_task` 在处理 HEADER 帧时无条件执行 `dl->active = 0`，导致：

- node0 下载进行中时 node1 header 到达 → `active=0` 直接杀死 node0 的下载状态
- node0 的残余 RAW 帧可能被误归到 node1 的 buffer

### 修复

将 `g_dl_buf` 改为 `g_dl_buf[MASTER_MAX_NODES]` 数组，每个节点独立维护 `active`、`node_buffer`、`received_points` 等状态。

**改动文件：**
- [master.h](file:///e:/飞腾派/Phytium/freertos/inc/master.h) — `master_get_download_buf(void)` → `master_get_download_buf(uint8_t node_id)`
- [master_sys.c](file:///e:/飞腾派/Phytium/freertos/src/master_sys.c) — 单实例改数组 + 边界检查
- [master_recv.c](file:///e:/飞腾派/Phytium/freertos/src/master_recv.c) — 按 `node_id` 获取对应 dl
- [master_poll_task.c](file:///e:/飞腾派/Phytium/freertos/src/master_poll_task.c) — 循环中按 `i` 获取独立 dl

---

## 问题 3: 轮询超时和流程控制

### 3.1 `wait_download_done` 提前超时

`recv_raw_points` 原来在 `process_node_raw` 中累加（process_task 消费队列后才更新），但 poll_task 的 `wait_download_done` 需要实时检测数据到达。修复：将累加逻辑移到 `master_recv_task` 解析帧时立即执行。

### 3.2 `dl->active` 未在 `send_lora_cmd` 预置

`send_lora_cmd` 移除 `dl->active = 1` 后，poll_task 的 `while(dl->active)` 循环可能在 header 被 process_task 处理前就退出。修复：`send_lora_cmd` 恢复 `dl->active = 1`。

### 3.3 无响应死等

`while(dl->active || dl->flash_save_pending)` 原先无超时，节点离线时 poll_task 永久卡住。修复：加 5 秒超时，超时后强制清零 `active` 和 `flash_save_pending`。

### 3.4 首次无响应加重试

E220 半双工模块在 RX→TX 切模式时可能丢首包。修复：首次 `no response` 后延时 200ms 重试 1 次，两次都无响应才 skip。

### 3.5 节点间静默保护

node1 传输结束后空口仍有残余帧，污染 node2 的 poll 窗口。修复：每个节点处理完毕后加 300ms 静默延迟。

**改动文件：** [master_poll_task.c](file:///e:/飞腾派/Phytium/freertos/src/master_poll_task.c)

### 新流程

```
send_lora_cmd(node_i)          ← dl[i].active=1, counters=0
    ↓
wait_download_done(3s)
    ├── 超时 && recv_raw_points==0 → 200ms 延时 → retry 1 次
    │       └── 仍超时 → "no response, skip" → dl[i].active=0 → next
    ├── 超时 && recv_raw_points>0  → "partial, waiting..."
    └── 成功                       → 继续
            ↓
    while (dl[i].active || dl[i].flash_save_pending)  ← 最多等 5s
            ↓
    "done (XXXXms)"
            ↓
    vTaskDelay(300ms)            ← 静默保护
            ↓
    next node
```

---

## 关键常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `SLAVE_ADDR_BASE` | 0x000B | 终端节点 LoRa 地址起始 |
| `MASTER_ADDR` | 0x000A | 主控 LoRa 地址 |
| `MASTER_POLL_MAX_NODES` | 3 | 最大轮询节点数 |
| `MASTER_POLL_CYCLE_MS` | 5000 | 轮询周期 |
| `TIER1_TIMEOUT_MS` | 3000 | 正常路径超时 |
| `TIER2_TIMEOUT_MS` | 3000 | 故障路径超时 |

---

## 涉及文件总览

| 文件 | 改动 |
|------|------|
| [data_frame.h](file:///e:/飞腾派/Phytium/freertos/inc/data_frame.h) | +`src_addr_l` 字段 |
| [data_frame.c](file:///e:/飞腾派/Phytium/freertos/src/data_frame.c) | 提取 E220 FP 接收头源地址 |
| [tasks.h](file:///e:/飞腾派/Phytium/freertos/inc/tasks.h) | +`src_node_id` 字段 |
| [master.h](file:///e:/飞腾派/Phytium/freertos/inc/master.h) | `master_get_download_buf` 加 `node_id` 参数 |
| [master_sys.c](file:///e:/飞腾派/Phytium/freertos/src/master_sys.c) | `g_dl_buf` 单例 → 数组 |
| [master_recv.c](file:///e:/飞腾派/Phytium/freertos/src/master_recv.c) | recv_task 按节点计数; process_task 源地址路由 |
| [master_poll_task.c](file:///e:/飞腾派/Phytium/freertos/src/master_poll_task.c) | 超时重试、静默保护、地址日志 |

---

## 待排查

- 终端节点 LoRa 地址是否互不相同（`AT+ADDR?` 确认）
- 终端固件确认模式下传输完成逻辑是否可靠（残余帧问题）

