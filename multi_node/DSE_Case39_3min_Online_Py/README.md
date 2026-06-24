# DSE Case39 — 在线 UKF（Python 版）

IEEE 39-Bus 10-Generator，2000Hz，3 分钟。结果与 MATLAB 完全一致。

---

## 目录结构

```
DSE_Case39_3min_Online_Py/
├── README.md                本文件
├── controller_online.py     ★ 在线 UKF 主程序
├── ukf_core_39.py           UKF 核心（ukf_init + ukf_step）
├── RK4.py                   矢量四阶 Runge-Kutta 积分器
├── plot_online.py           出图脚本
├── system_params.mat        系统参数（赛前预置）
├── measurements.txt         测量数据（360000 行，LoRa 传输）
└── true_states.csv          真值（可选验证）
```

---

## 快速开始

```bash
# 终端生成数据（赛前一次）
cd ../DSE_Case39_3min/10generator/10generator
python terminal_node_39.py

# 复制数据到主控目录
cp system_params.mat measurements.txt true_states.csv ../../DSE_Case39_3min_Online_Py/

# 主控运行
cd ../../DSE_Case39_3min_Online_Py
python controller_online.py measurements.txt | python plot_online.py
```

---

## 使用方式

```bash
# 文件模式
python controller_online.py measurements.txt

# 管道出图
python controller_online.py measurements.txt | python plot_online.py

# 实时流式（模拟 LoRa 逐条到达）
tail -f measurements.txt | python controller_online.py
```

## 核心 API

```python
from ukf_core_39 import UKFState, ukf_init, ukf_step

st = UKFState()                    # 创建持久状态
ukf_init(sp, st)                   # 初始化（一次）

# 每来一条测量，调用一次
x_est, rmse = ukf_step(st, z_k, t)  # z_k: 98维测量向量, t: 时间
```

---

## 系统参数

| 参数 | 值 |
|------|-----|
| 发电机 | 10 台（Bus 30-39） |
| 母线 | 39 |
| 状态 | 20 维（δ×10 + ω×10） |
| 测量 | 98 维（PG×10, QG×10, Vmag×39, Vangle×39） |
| 采样率 | 2000 Hz |
| 时长 | 180 秒 |
| 故障 | Bus 4 三相短路，Line 4-14 跳闸（5.0s ~ 5.3s） |
| sigma | 0.01（所有状态统一） |

## 输出图

| 文件 | 内容 |
|------|------|
| `ukf_online_generator1.png` ~ `generator10.png` | 每台发电机 δ + ω |
| `ukf_online_all_generators.png` | 10 台汇总 |
| `ukf_online_rmse.png` | RMSE 曲线 |

红色区域 = 故障时段（5.0s ~ 5.3s）。
