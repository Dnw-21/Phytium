# UKF 状态估计 — C 语言版本

## 概述

本目录是 Python 主控端的 **C 语言移植版本**，用于嵌入式 Linux 主控（如树莓派、ARM 工控板）。

**系统**：IEEE 9-Bus，3 台发电机（case9_new_Sauer）  
**算法**：无迹卡尔曼滤波（UKF）  
**测量精度**：4 位小数（LoRa 传输量化）  
**故障**：Bus 8 三相短路，Line 8-9 跳闸（0.060s ~ 0.080s）

---

## 目录结构

```
c_version/
│
├── README.md                    ← 本手册
├── Makefile                     ← 编译构建
│
├── include/                     ← 头文件
│   ├── matrix_ops.h             │  线性代数：Cholesky、矩阵运算
│   ├── dynamic_system.h         │  摇摆方程 + 矢量 RK4
│   └── ukf_estimation.h         │  UKF 算法 + 数据加载
│
├── src/                         ← 源文件
│   ├── main.c                   │  主控程序入口
│   ├── matrix_ops.c             │  矩阵运算实现
│   ├── dynamic_system.c         │  动态系统 + RK4 实现
│   └── ukf_estimation.c         │  UKF + 文件读取实现
│
├── scripts/                     ← Python 辅助脚本
│   ├── generate_c_params.py     │  system_params.mat → .txt 转换
│   └── plot_results_c.py        │  从 CSV 绘图
│
├── data/                        ← 运行时数据
│   ├── system_params.txt        │  系统参数（C 可读文本格式）
│   ├── measurements.txt         │  测量数据（从上级目录复制或链接）
│   └── true_states.csv          │  真值（可选）
│
└── output/                      ← 输出图表
    ├── ukf_results_generator1_c.png
    ├── ukf_results_generator2_c.png
    ├── ukf_results_generator3_c.png
    ├── ukf_results_all_generators_c.png
    └── ukf_results_rmse_c.png
```

---

## 快速开始

### 1. 编译

```bash
cd c_version
make
```

编译产物：`ukf_controller`（或 Windows 下 `ukf_controller.exe`）

**依赖**：
- GCC / MinGW（Windows）
- C99 标准库（`<complex.h>`）
- 数学库 `-lm`

### 2. 准备数据

```bash
# 方法 A：从已生成的 Python 数据转换
make prepare

# 方法 B：手动准备
#   a) 复制 measurements.txt 和 true_states.csv 到 data/
#   b) 运行转换脚本：
python scripts/generate_c_params.py
```

`system_params.txt` 格式说明：

```
# 第一行：n s fs ns nm num_samples deltt t_SW t_FC
3 9 1000 6 24 80 0.001 0.06 0.08

# YBUS stage 0 (3x3 复数，每行 real imag)
...（9对实数）

# YBUS stage 1
...

# YBUS stage 2
...

# RV stage 0 (9x3 复数)
...（27对实数）

# RV stage 1 & 2
...

# E_abs (3个实数)
...

# PM, M, D (各3个实数)
...

# X_hat_init (6个实数)
...
```

### 3. 运行 UKF

```bash
make run
```

或手动：

```bash
mkdir -p output
./ukf_controller data/
```

### 4. 绘图

```bash
make plot
```

或：

```bash
python scripts/plot_results_c.py
```

### 5. 一键完整流程

```bash
make all_run
```

---

## 文件接口说明

### 输入文件

| 文件 | 格式 | 来源 | 说明 |
|------|------|------|------|
| `system_params.txt` | 自定义文本 | `generate_c_params.py` 生成 | 系统参数，赛前预置 |
| `measurements.txt` | CSV | 终端 LoRa 传输 | timestamp + 24 测量量，4 位小数 |
| `true_states.csv` | CSV | 终端生成（可选） | 6 状态真值，用于验证 |

### 输出文件

| 文件 | 内容 |
|------|------|
| `ukf_estimation_output.csv` | `[time, delta1,delta2,delta3, omega1,omega2,omega3, RMSE]` × 80 行 |
| `ukf_comparison_output.csv` | `[time, true_delta1..3, true_omega1..3, est_delta1..3, est_omega1..3]` |
| `ukf_results_generator1_c.png` ~ `generator3_c.png` | 单台发电机角度+转速对比 |
| `ukf_results_all_generators_c.png` | 3 台发电机汇总 |
| `ukf_results_rmse_c.png` | RMSE 曲线 |

---

## 算法说明

### 状态向量 (6 维)

```
X = [δ₁, δ₂, δ₃, ω₁, ω₂, ω₃]ᵀ
```

### 测量向量 (24 维)

```
Z = [PG₁,PG₂,PG₃, QG₁,QG₂,QG₃, Vmag₁..Vmag₉, Vangle₁..Vangle₉]ᵀ
```

### UKF 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| σ | 1e-2 | 噪声标准差 |
| P₀ | σ²·I₆ | 初始协方差 |
| Q | σ²·I₆ | 过程噪声 |
| R | σ²·I₂₄ | 测量噪声 |
| Sigma 点数 | 2n = 12 | 无迹变换 |
| 积分器 | RK4 | 四阶 Runge-Kutta |

### 故障拓扑

| 阶段 | 时间 | YBUS 索引 | 说明 |
|------|------|-----------|------|
| 故障前 | t < 0.060s | stage 0 | 正常网络 |
| 故障中 | 0.060s ≤ t ≤ 0.080s | stage 1 | Bus 8 三相短路接地 |
| 故障后 | t > 0.080s | stage 2 | Line 8-9 跳闸 |

---

## 模块说明

### `matrix_ops.c` — 线性代数

| 函数 | 功能 |
|------|------|
| `cholesky_decomp(n, A, L)` | Cholesky 分解 A = L·Lᵀ |
| `cholesky_solve(n, L, b, x)` | 前代+回代求解 |
| `solve_real_spd(n, nrhs, A, B, X)` | 多右端 SPD 求解 |
| `matmul_real(m,k,n, A,B,C)` | 实数矩阵乘法 |
| `matmul_real_AT(m,k, A, C)` | A·Aᵀ 乘法 |
| `cmatvec(m,n, A,x,y,accum)` | 复数矩阵-向量乘法 |

### `dynamic_system.c` — 动态系统

| 函数 | 功能 |
|------|------|
| `dynamic_system(n, x, M, D, Ybusm, E_abs, PM, dx)` | 单步摇摆方程求导 |
| `rk4_vectorized(n, deltt, E_abs, ns, X_sigma, ...)` | 矢量 RK4 传播所有 sigma 点 |

### `ukf_estimation.c` — UKF

| 函数 | 功能 |
|------|------|
| `load_system_params(filename, sp)` | 读取 system_params.txt |
| `load_measurements(filename, nm, max, Z)` | 读取 measurements.txt |
| `load_true_states(filename, ns, max, X)` | 读取 true_states.csv |
| `ukf_estimation(sp, Z_mes, num, X_est, RMSE)` | 执行 UKF 主循环 |

---

## C 与 Python 版本对比

| | Python | C |
|------|--------|---|
| 线性代数 | scipy.linalg (LAPACK) | 手写 Cholesky + 矩阵运算 |
| 复数 | Python complex | C99 `double complex` |
| 文件读取 | `numpy.loadtxt` | 手写 CSV 解析 `strtok` |
| 内存管理 | 自动 GC | 手动 `malloc`/`free` |
| 编译 | 无需 | `gcc -O2` |
| 运行速度 | ~0.5s (80步) | ~0.01s (80步) |
| 依赖 | numpy, scipy | 仅 libc + libm |

---

## 注意事项

1. **C99 复数**：需要在支持 `<complex.h>` 的编译器上编译（GCC ≥ 4.2, MSVC 不完整支持，建议用 MinGW）

2. **Cholesky 数值稳定性**：当 P 矩阵接近非正定时会自动加小量正则化（1e-10），防止分解失败

3. **内存**：总内存分配约 2 MB（80 步），如需更多步数请调整 `max_samples`

4. **Windows 编译**：推荐使用 MinGW-w64 或 MSYS2 + GCC
   ```bash
   mingw32-make
   ```

5. **跨平台**：Linux/ARM/macOS 均可直接 `make`
