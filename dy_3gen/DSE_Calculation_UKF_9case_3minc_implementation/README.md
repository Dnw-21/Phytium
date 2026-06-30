# UKF 动态状态估计 — C 语言实现（在线流式）

IEEE 9-Bus 3-Generator，Bus 8 故障，Line 8-9 跳闸（5.0s ~ 5.3s），2000 Hz，180 秒。

与 `python/` 目录下的 Python 版**完全一致**（相同参数、相同数据、相同结果）。

---

## 目录结构

```
DSE_Calculation_UKF_9case_3minc_implementation/
│
├── README.md               本文件
├── Makefile                 编译构建
│
├── controller_online.c      ★ 主控在线 UKF（逐条处理，流式输出）
├── controller.c              主控批量 UKF（备选，一次加载全部）
├── ukf_core.h                共享头文件（矩阵运算、Cholesky）
├── terminal_node.c           终端数据生成（备选，硬编码参数）
│
├── convert_params.py         从 Python system_params.mat 生成 system_params.bin
├── plot_online.py            从估计结果 CSV 生成 PNG 图
│
├── system_params.bin         系统参数（赛前预置）
├── measurements.txt          测量数据（LoRa 传输，360000 行，4 位小数）
└── true_states.csv           真值（可选验证）
```

---

## 快速开始

### 一键运行

```bash
make
```

等价于：`prepare → build → run → plot`

### 分步运行

```bash
make prepare     # 从 ../python/system_params.mat 生成 system_params.bin
make build       # 编译 controller_online
make run         # 运行 UKF + 出图
make plot        # 仅出图（从已有 CSV）
make clean       # 清理
```

### 手动运行

```bash
# 1. 准备数据（赛前跑一次）
python convert_params.py

# 2. 编译
gcc -O2 -std=c99 -o controller_online controller_online.c -lm

# 3. 运行（三种模式）
./controller_online measurements.txt                     # 文件模式
./controller_online measurements.txt | python plot_online.py  # 管道出图
tail -f measurements.txt | ./controller_online                # 实时流式
```

---

## 完整流程

```
┌──────────────────────────┐          LoRa/网络          ┌──────────────────────────┐
│  竞赛电脑（终端）          │ ◄─────────────────────────► │  Linux 主控               │
│                          │                             │                          │
│  python/terminal_node_9  │    measurements.txt          │  controller_online.c     │
│       ↓                  │    (360000行, 4位小数)        │       ↓                  │
│  生成:                   │                             │  逐行读取 → ukf_step()   │
│  ├ system_params.mat     │                             │  结果 → stdout CSV       │
│  ├ measurements.txt      │                             │       ↓                  │
│  └ true_states.csv       │                             │  plot_online.py          │
│                          │                             │       ↓                  │
│  运行:                   │                             │  5 张 PNG 图             │
│  python terminal_node_9  │                             │                          │
└──────────────────────────┘                             └──────────────────────────┘
```

---

## 两种 UKF 模式

| | controller.c（批量） | controller_online.c（流式）★ |
|---|---|---|
| 数据加载 | 全部读到内存 | 逐行读取（stdin/文件） |
| 内存占用 | ~70 MB | ~20 KB |
| 接口 | 一次性 | `ukf_init()` + 逐条 `ukf_step()` |
| 适用 | 离线分析 | **嵌入式实时** |

### controller_online 核心 API

```c
UKFState st;                              // 持久状态
ukf_init(&sp, &st);                       // 初始化（一次）
ukf_step(&sp, &st, z_k, k_time, x, &r);   // 每来一条测量调用一次
```

---

## 系统参数

| 参数 | 值 |
|------|-----|
| 发电机 | 3 台（Bus 1, 2, 3） |
| 状态维数 | 6（δ₁,δ₂,δ₃, ω₁,ω₂,ω₃） |
| 测量维数 | 24（PG×3, QG×3, Vreal×9, Vimag×9） |
| 采样率 | 2000 Hz |
| 时长 | 180 秒（360000 步） |
| 故障 | Bus 8 三相短路, Line 8-9 跳闸（5.0s ~ 5.3s） |
| UKF σ_angle | 0.01 |
| UKF σ_speed | 0.03 |
| UKF σ_meas | 0.01 |

---

## 输出图

| 文件 | 内容 |
|------|------|
| `ukf_online_generator1.png` | Gen1：δ₁(deg) + ω₁(rad/s) |
| `ukf_online_generator2.png` | Gen2：δ₂(deg) + ω₂(rad/s) |
| `ukf_online_generator3.png` | Gen3：δ₃(deg) + ω₃(rad/s) |
| `ukf_online_all_generators.png` | 3 台汇总（2行×3列） |
| `ukf_online_rmse.png` | RMSE = sqrt(trace(P)) |

红色区域 = 故障时段（5.0s ~ 5.3s）。

---

## 编译要求

- GCC ≥ 4.2（C99，`<complex.h>`）
- `-lm`（数学库）
- **零外部依赖**（无 LAPACK/BLAS，纯手写线性代数）

ARM 交叉编译：
```bash
arm-linux-gnueabihf-gcc -O2 -std=c99 -o controller_online controller_online.c -lm
```
