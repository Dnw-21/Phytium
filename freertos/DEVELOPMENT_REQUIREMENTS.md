# 开发需求文档 — Master_v3(2) → FreeRTOS 移植审查

> 版本: v1.0 | 日期: 2026-06-03 | 状态: 待审核

---

## 一、项目目标

将 `GD32L233C_Prj_Master_v3 (2)` 的逻辑和实现**无条件移植**到飞腾 FreeRTOS 主控器，确保：
1. 帧格式、加密/解密、轮询逻辑与 Master_v3(2) 完全一致
2. 终端节点（GD32L233C_Prj_v3）可以正常接收主控命令并正确响应
3. 主控器可以正确解析终端加密响应数据

---

## 二、现状分析

### 2.1 已完成（与 Master_v3(2) 一致）

| 模块 | 文件 | 状态 | 说明 |
|------|------|:--:|------|
| 混沌加密 | `src/chaos_encrypt.c/h` | ✅ | 算法一致：8字节 sync、memcpy IEEE754、纯 XOR、无 scramble |
| 帧解析 | `src/data_frame.c` frame_parse() | ✅ | 8字节 sync @ frame[9..16]、enc_len = data_len - 13 |
| 接收状态机 | `src/master_recv.c` process_node_header/raw | ✅ | 逻辑一致 |
| 节点超时 | `src/master_judge.c` | ✅ | 逻辑一致 |
| 系统管理 | `src/master_sys.c` | ✅ | RAM 替代 Flash（FreeRTOS 无内部 Flash API） |
| 数据结构 | `inc/master.h` / `inc/data_frame.h` | ✅ | 结构体定义一致 |

### 2.2 关键差异（需要修复）

#### 差异1: `master_poll_task.c` — 下发命令未加密

| 对比项 | Master_v3(2) | FreeRTOS 当前 |
|--------|-------------|--------------|
| 函数 | `lora_send_encrypted()` | `lora_send_cmd()` |
| sync 大小 | **8 字节** (uint64_t) | **4 字节** (uint32_t) |
| 是否加密 | ✅ `chaos_encrypt_packet()` | ❌ 明文复制 |
| sync 值 | `chaos_get_sync_code()` 返回 64-bit | 硬编码 `sync = 0` |

**Master_v3(2) 源码** ([master_poll_task.c:15-34](../GD32L233C_Prj_Master_v3%20(2)/GD32L233C_Prj_Master_v3/GD32L233C_Prj_Master/app/task/master_poll_task.c#L15-L34)):
```c
static void lora_send_encrypted(const uint8_t *data, uint16_t len, uint8_t data_type,
                                 uint16_t addr, uint8_t channel)
{
    uint64_t sync = 0;
    uint16_t enc_len = chaos_encrypt_packet(data, len, g_enc_buf, &sync);
    // 8字节 sync 大端写入
    g_lora_pkt[0] = (sync >> 56) & 0xFF;  // ← 8字节!
    ...
    g_lora_pkt[7] = sync & 0xFF;
    memcpy(&g_lora_pkt[8], g_enc_buf, enc_len);
    send_node_data_with_ack(g_lora_pkt, enc_len + 8, data_type, &dest, 3, ...);
}
```

**FreeRTOS 当前** ([master_poll_task.c:16-32](../freertos/src/master_poll_task.c#L16-L32)):
```c
static void lora_send_cmd(const uint8_t *data, uint16_t len, uint8_t cmd_code,
                           uint16_t addr, uint8_t channel)
{
    uint32_t sync = 0;  // ← 只有4字节! 且未加密!
    uint16_t enc_len = len;
    memcpy(g_enc_buf, data, len);  // ← 明文复制，未调用 chaos_encrypt_packet
    g_lora_pkt[0] = (sync >> 24) & 0xFF;  // ← 4字节
    ...
    g_lora_pkt[3] = sync & 0xFF;
    memcpy(&g_lora_pkt[4], g_enc_buf, enc_len);
    send_node_data_with_ack(g_lora_pkt, enc_len + 4, cmd_code, &dest, 3, ...);
}
```

**影响**:
- 终端节点解密 poll 命令时，从 8 字节 sync 恢复的混沌状态与主控不一致
- 终端 `[DEC]` 输出乱码：`0xB8 0xC9 0x09 0x8B 0xA3`（期望首字节 `0x14`）
- 虽然终端仍能响应（因为终端 recv 任务可能有容错），但解密结果不正确

#### 差异2: `data_frame.c` — send_node_data_with_ack() FP 模式头 ✅ 已确认兼容

| 对比项 | Master_v3(2) | FreeRTOS 当前 |
|--------|-------------|--------------|
| FP 模式头 | `LoRa_SendData_Direct()` 内部添加 3字节 | 手动添加 3字节 `destH destL channel` |

**已确认**: Master_v3(2) 的 `LoRa_SendData_Direct()` ([mwcc68_app.c:471-494](../GD32L233C_Prj_Master_v3%20(2)/GD32L233C_Prj_Master_v3/GD32L233C_Prj_Master/app/BSP/mwcc68_app.c#L471-L494)) 在发送前**先发送 3 字节 FP 模式头**（destH, destL, channel），然后再发送数据。

FreeRTOS 的 `send_node_data_with_ack()` ([data_frame.c:73-113](../freertos/src/data_frame.c#L73-L113)) 手动添加了同样的 3 字节 FP 模式头。

**结论**: 两者行为一致，**无需修改**。

#### 差异3: `master_recv.c` — 解密已激活（与 Master_v3(2) 一致）

| 对比项 | Master_v3(2) | FreeRTOS 当前 |
|--------|-------------|--------------|
| 解密调用 | ✅ `chaos_decrypt_packet()` L176-178 | ✅ `chaos_decrypt_packet()` L182-184 |

**注意**: HANDOVER.md 和 .recallloom 中记录"Master_v3(2) 解密被注释"是**错误的**！实际 Master_v3(2) 源码中解密是**激活的**。

---

## 三、数据验证分析

### 3.1 终端发送数据（terminal_one_cycle.txt）

终端成功解密 poll 命令并加密响应：
```
[DEC] type=0x14 len=5: 0xB8 0xC9 0x09 0x8B 0xA3   ← 终端解密结果（乱码）
[ENC]TX Raw: type=0x01, len=20                       ← 终端加密前明文
  0x01 0x00 0x00 0x01 0x00 ...
Encrypted: sync=0xBFD1B213403937A9, len=28           ← 终端加密后 8字节 sync
```

### 3.2 主控接收数据（captured_one_cycle.txt）

主控接收到终端响应：
```
[RECV] sync=3F2D8953 type=0x01 len=24               ← 只读到 4字节 sync
[RAW] 40B: AA 55 00 21 00 52 EF B1 01 3F 2D 89 53 BF B1 8B AA ...
[DEC] sync=3F2D8953 type=0x01 len=24: BF B1 8B AA ...  ← 解密结果（密文，因为 sync 不完整）
```

### 3.3 问题定位

| 问题 | 根因 | 修复 |
|------|------|------|
| 终端 `[DEC]` 乱码 | FreeRTOS 发送未加密 poll + 4字节 sync | 加密 poll + 8字节 sync |
| 主控 `[DEC]` 乱码 | 主控用 4字节 sync 做 XOR 解密 | 用完整 8字节 sync 解密 |
| 主控 `[DEC]` sync=3F2D8953 | frame_parse 读 8字节但 printf 只打印 4字节 | 修正 printf 格式（已有 %016llX） |
| Poll 部分 timeout | 加密 header 的 node_index 非法 → active=0 | 已修复（v12-S3） |

### 3.4 帧格式验证

终端发送的帧格式（8字节 sync）：
```
AA 55 00 21 00 52 EF B1 01 [3F 2D 89 53 BF B1 8B AA] [enc 20B] [CRC] 55 AA
           len=33  ts?    type  ────── sync 8字节 ──────  加密数据   CRC  帧尾
```

FreeRTOS frame_parse() 提取：
- `rx_type` = frame[8] = `0x01` ✅
- `sync_code` = frame[9..16] = `0x3F2D8953BFB18BAA` ✅（8字节大端）
- `enc_len` = 33 - 13 = 20 ✅
- `enc_start` = &frame[17] ✅

**帧解析逻辑正确**，8字节 sync 提取无误。

---

## 四、开发需求

### 需求1: 修复 poll 命令加密（P0 — 必须）

**文件**: `src/master_poll_task.c`

**改动**:
1. 将 `lora_send_cmd()` 重命名为 `lora_send_encrypted()`，对齐 Master_v3(2)
2. `sync` 类型改为 `uint64_t`
3. 调用 `chaos_encrypt_packet()` 加密 payload
4. 8字节 sync 大端写入 `g_lora_pkt[0..7]`
5. 加密数据写入 `g_lora_pkt[8..]`
6. `send_node_data_with_ack()` 长度参数改为 `enc_len + 8`

**参考实现**: Master_v3(2) `master_poll_task.c:15-34`

### 需求2: 确认 FP 模式头兼容性 ✅ 已确认无需修改

Master_v3(2) 的 `LoRa_SendData_Direct()` 内部添加 3 字节 FP 模式头，与 FreeRTOS 的 `send_node_data_with_ack()` 行为一致。

### 需求3: 移除调试输出（P2 — 优化）

**文件**: `src/master_recv.c`

**改动**:
1. 移除 `[DBG] dec done, switching` / `[DBG] before parse` / `[DBG] ack sent` / `[RV] rx loop` 等调试行
2. 保留 `[RECV]` / `[DEC]` / `[RAW]` 数据输出行（用于数据验证）

### 需求4: 更新文档（P2 — 同步）

**文件**: `HANDOVER.md` / `.recallloom`

**改动**:
1. 修正"Master_v3(2) 解密被注释"的错误记录 → 实际是**激活的**
2. 更新 sync_code 大小：终端 8字节 → 主控也必须 8字节
3. 记录 poll 命令加密修复

---

## 五、验证计划

### 5.1 编译验证
```bash
cd /home/alientek/Phytium/freertos && bash deploy.sh
```

### 5.2 功能验证
1. 固件部署后 `trace_reader` 观察：
   - `[ENC][CMD]` 行确认 poll 命令已加密
   - `[RECV]` 行确认 8字节 sync 正确显示
   - `[DEC]` 行确认解密结果首字节 = `0x01`（type）
   - `Poll: nodeX status ok` 确认不再 timeout

### 5.3 数据对比验证
```bash
# 保存主控日志
ssh user@192.168.88.10 'echo user|sudo -S timeout 60 /home/user/trace_reader' 2>/dev/null \
  | grep -E '\[RECV\]|\[DEC\]|\[ENC\]|Poll:' > /tmp/freertos_log.txt

# 对比终端日志
# 终端 [DEC] type=0x14 首字节应为 0x14（CMD_POLL_STATUS）
# 主控 [DEC] type=0x01 首字节应为 0x01（DATA_TYPE_NODE_HEAD）
```

### 5.4 全链路验证
```
主控发送 poll(加密+8B sync) → 终端解密成功([DEC]首字节=0x14)
                              → 终端加密响应(sync=8字节)
                              → 主控接收+解密([DEC]首字节=0x01)
                              → Poll: nodeX status ok
```

---

## 六、风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|:--:|:--:|------|
| ARMCC/GCC 浮点差异导致解密不匹配 | 中 | 高 | Master_v3(2) 已验证解密可用；如不行则透传 |
| FP 模式头双重添加 | ✅ 已确认兼容 | — | LoRa_SendData_Direct() 内部也添加 FP 头 |
| 终端无法解密加密 poll | 低 | 高 | 终端 chaos_init(0x12345678) 与主控相同 |
| 帧格式不兼容 | 低 | 中 | 已验证 frame_parse 逻辑一致 |

---

## 七、相关文件索引

| 文件 | 用途 |
|------|------|
| `freertos/HANDOVER.md` | 项目交接文档 |
| `freertos/.recallloom` | AI 项目记忆文件 |
| `freertos/DEBUG_GUIDE.md` | 调试指南（含历史踩坑） |
| `freertos/OPERATIONS.md` | 操作命令手册 |
| `GD32L233C_Prj_v3/` | 终端节点源码（只读参考） |
| `GD32L233C_Prj_Master_v3 (2)/` | 移植源（无条件照搬） |
| `freertos/captured_one_cycle.txt` | 主控接收数据 |
| `freertos/terminal_one_cycle.txt` | 终端发送数据 |

---

## 八、Skill 工具推荐

以下 skill 可用于提高开发效率：

| Skill | 用途 | 适用场景 |
|-------|------|---------|
| `recallloom` | 项目上下文记忆恢复 | 每次对话开始时恢复项目状态 |
| `rtk` | Token 优化 CLI 代理 | 减少 git/编译等命令的 token 消耗 |
| `code-review` | 代码审查 | 每次修改后审查正确性 |
| `verify` | 功能验证 | 部署后验证功能是否正常 |
| `mattpocock-diagnose` | 问题诊断 | 解密不匹配等问题排查 |
| `superpowers-verification-before-completion` | 完成前验证 | 确保修改完成后功能正确 |

---

*请审核以上需求，确认后开始实施。*
