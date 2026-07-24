# FreeRTOS LoRa 接收项目 — 交接文档

> 最后更新: 2026-06-04
> 状态: **v16 — LoRa 收数→UKF 管线在飞腾派上跑通, 终端 24 字段数据待修复**

---

## 一、已完成

| 阶段 | 说明 | 状态 |
|------|------|:--:|
| 通信链路 | LoRa 收发正常, header + data 帧完整 | ✅ |
| 自定义 sinf/cosf | 跨平台一致, ARMCC/GCC/x86_64 输出相同 | ✅ |
| 解密 | 主控正确解密终端数据 | ✅ |
| 飞腾派 UKF | C 语言 UKF 编译运行在开发板上 | ✅ |
| LoRa→UKF 管线 | trace_reader → convert.py → ukf_controller 一键完成 | ✅ |
| 终端 24 字段数据 | PG1 正确, PG2~Vangle9 全为 0, 待排查 | 🔴 |
| 状态估计正确性 | 数据不完整, RMS 偏大 (17° vs 参考 0.004°) | 🔶 |

---

## 二、关键路径

| 用途 | 路径 |
|------|------|
| FreeRTOS 源码 | `/home/alientek/Phytium/freertos/` |
| SDK 编译入口 | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/` |
| 终端源码 | `/home/alientek/Phytium/GD32L233C_Prj_v3/GD32L233C_Prj/app/` |
| 终端改动的文件 | `data_monitor.c`, `data_monitor.h`, `chaos_encrypt.c/h`, `zdata_adaptive.c/h` |
| 状态估计 C 代码 | `/home/alientek/Phytium/Quantized_Measurement_C/` |
| 参考数据 | `Quantized_Measurement_C/data/measurements.txt` (80点) |
| UKF 参考结果 | `Quantized_Measurement_C/data/ukf_estimation_output.csv` |
| 开发板 UKF 目录 | `user@192.168.88.10:/home/user/ukf/` |
| 开发板 IP | `192.168.88.10`, 用户名/密码: `user/user` |

---

## 三、核心问题

**终端发来的数据只有 PG1 和 timestamp 有值, 其他 22 个字段全是 0。**

证据: 解密后的 hex: `FC 1B 00 00 00 00 ... 04 00 00 00`
- FC 1B = pg1 = 7164 = 0.7164 ✅
- timestamp = 4 ✅
- pg2~vangle9 = 全 0 ❌

源文件 `/home/alientek/Phytium/GD32L233C_Prj_v3/GD32L233C_Prj/app/data_monitor.c` 的 `node_upload_by_timestamp` 函数已用 `volatile` 指针写入全部 24 字段, 但 Keil 编译后不生效。

**需在 Keil 里确认**:
1. 右键 `data_monitor.c` → Open Containing Folder, 确认路径是虚拟机拷过去的文件
2. 在文件里搜 `s_volatile` 确认是 volatile 版本
3. Clean Targets + Rebuild All

---

## 四、飞腾派操作命令

```bash
# SSH 登录
ssh user@192.168.88.10   # 密码 user

# FreeRTOS 状态
echo 'user' | sudo -S cat /sys/class/remoteproc/remoteproc0/state

# 启动 FreeRTOS (仅在 offline 时)
echo 'user' | sudo -S sh -c 'echo openamp_core0.elf > /sys/class/remoteproc/remoteproc0/firmware && echo start > /sys/class/remoteproc/remoteproc0/state'

# 查看实时输出
echo 'user' | sudo -S /home/user/trace_reader

# LoRa→UKF 一键管线
echo 'user' | sudo -S timeout 30 /home/user/trace_reader 2>/dev/null > /tmp/raw.txt
grep '\[DEC\].*type=0x04 len=208:' /tmp/raw.txt > /tmp/dec.txt
python3 /home/user/ukf/convert.py /tmp/dec.txt /home/user/ukf/data/measurements.txt
/home/user/ukf/ukf_controller /home/user/ukf/data

# 查看 UKF 结果 vs 参考
cut -d"," -f2,5 /home/user/ukf/data/ukf_estimation_output.csv | head -5
cut -d"," -f2,5 /home/user/ukf/data/ref_ukf_output.csv | head -5

# 查看测量数据 (PG1/PG2/V1)
cut -d"," -f2,3,7 /home/user/ukf/data/measurements.txt | head -5
```

## 五、主控编译部署

```bash
cd /home/alientek/Phytium/freertos && bash deploy.sh
```

## 六、终端数据流

```
zdata_adaptive.c (60正常+20故障=80点)
       │ s_seq 计数器 0→1→2→3
       ▼
node_upload_by_timestamp() → 取 20 点 → send_waveform_packet()
       │ 加密
       ▼
LoRa 发送 → 主控接收 → 解密 → [DEC] 输出 → convert.py → UKF
```

**预期**: 4 次 poll 收齐 80 点 (前60正常 PG1=0.7164, 后20故障 PG1 逐渐下降)
**当前**: 收到 80 点但 PG2~Vangle9 全是 0
