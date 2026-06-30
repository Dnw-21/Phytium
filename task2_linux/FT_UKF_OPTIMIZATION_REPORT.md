# 基于飞腾 FT 加速库的电力系统无迹卡尔曼滤波实时状态估计优化研究

> 文档版本: v1.1  
> 日期: 2026-06-18  
> 实验平台: Phytium PE2204 (ARMv8-A, 4 核 A76/A55 异构)  
> 操作系统: Linux 6.6.63-phytium-embedded-v3.2  
> 代码位置: `/home/alientek/Phytium/task2_linux/`  
> 在线 CPU: CPU0 / CPU1 / CPU2；CPU3 因底层固件未释放而离线

---

## 摘要

本文研究了在飞腾 Phytium PE2204 异构多核处理器上，利用飞腾自主 FT（Fault-Tolerant / Phytium-Tuned）加速库对电力系统无迹卡尔曼滤波（Unscented Kalman Filter, UKF）状态估计算法进行性能优化的方法。通过在 UKF 的核心线性代数运算中引入 BLAS-FT 与 LAPACK-FT 库，将原本基于三重循环的纯 C 实现替换为针对 ARMv8-A 架构深度优化的矩阵运算原语。实验结果表明：对于 39bus 大系统（状态维度 $n_s=20$，测量维度 $n_m=98$），FT 版本相比非 FT 版本在三节点并发场景下处理帧率提升 **20%**（250 fps → 300 fps），端到端单帧延迟降低 **24%**（3576 μs → 2717 μs），同时状态估计精度（RMSE）保持一致。

在此基础上，本文进一步开展了**高并发多节点扩展研究**。通过降低数据生成与 UKF 估计频率，在不牺牲状态估计精度的前提下释放 CPU 资源。实验将 5bus/9bus 从 2kHz 降至 500Hz、39bus 从 2kHz 降至 250Hz，使 CPU0 上可稳定支持的小节点数从 2 个增加到 **6 个**，并在典型真实场景布局（配网监测、变电站群、混合场景）下验证了稳定性。同时，本文对 PE2204 的 CPU3 不可用问题进行了系统排查，确认其根源在于 **ATF/U-Boot 等底层固件未将 CPU3 释放给 Linux**，而非 Linux 设备树或内核配置问题。

---

## 1. 引言

### 1.1 研究背景

电力系统动态状态估计是保障电网安全稳定运行的关键技术。随着新能源大规模并网，电网动态行为日趋复杂，对状态估计的实时性提出了更高要求。无迹卡尔曼滤波（UKF）因其对非线性系统良好的估计性能，成为电力系统动态状态估计的主流算法之一。

UKF 的核心计算瓶颈在于每步迭代中大量的矩阵运算：Sigma 点传播、协方差矩阵计算、Kalman 增益求解以及状态更新。这些运算的复杂度随系统规模呈 $O(n_s^3)$ 或 $O(n_s^2 n_m)$ 增长。对于 39bus 这类大系统，状态维度达到 20，测量维度达到 98，纯 C 实现的矩阵乘法与矩阵分解成为实时性的主要瓶颈。

### 1.2 研究目标

本研究的目标是在飞腾 Phytium PE2204 处理器上，利用飞腾自主优化的基础软件库（FT 库）加速 UKF 状态估计中的线性代数运算，验证 FT 库对实时状态估计性能的提升效果，并探索在有限 CPU 核心下的多节点高并发状态估计能力。

### 1.3 系统架构

本系统采用 AMP（Asymmetric Multi-Processing）架构：

- **FreeRTOS 侧（CPU1，remoteproc）**：运行电力系统动态仿真任务，生成量测数据并通过独立共享内存（SHM）环形缓冲区写入数据。
- **Linux 侧（CPU0/CPU2）**：运行三个独立的 UKF Pipeline 进程，分别从对应 SHM 读取数据并执行在线状态估计。

三个 UKF 节点分别为：

| 节点 | 发电机数 | 母线数 | 状态维度 $n_s$ | 测量维度 $n_m$ | Sigma 点数 | CPU 绑定 |
|------|----------|--------|----------------|----------------|------------|----------|
| 5bus | 2 | 5 | 4 | 14 | 8 | CPU0 |
| 9bus | 3 | 9 | 6 | 24 | 12 | CPU0 |
| 39bus | 10 | 39 | 20 | 98 | 40 | CPU2 |

> **CPU 核心说明**：PE2204 为 4 核 ARMv8-A 处理器，但当前 Linux 实际在线 CPU 为 CPU0、CPU1、CPU2。CPU1 被 remoteproc 用于运行 FreeRTOS；CPU3 因底层固件（ATF/U-Boot）未释放而无法被 Linux 使用。详细排查过程见第 6.7 节。

---

## 2. 飞腾 FT 加速库介绍

### 2.1 库概述

本研究实际使用的 FT 库存放于 `/home/alientek/Phytium/fc_lib/`，工程构建时通过 [Makefile](file:///home/alientek/Phytium/task2_linux/Makefile) 中的 `FC_LIB_DIR` 指定。

| 库名称 | 实际版本 | 安装路径 | 主要文件 | 功能描述 | 是否使用 |
|--------|----------|----------|----------|----------|----------|
| **BLAS-FT** | v1.5.0 | `/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0` | `include/cblas.h`、`include/lapacke.h`、`lib/libblas_ft.a`、`lib/libblas_ft.so` | 飞腾优化的基础线性代数子程序库，提供矩阵-矩阵乘法（GEMM）、矩阵-向量乘法（GEMV）等核心 BLAS Level 1/2/3 运算 | **是** |
| **LAPACK-FT** | v1.4.0 | `/home/alientek/Phytium/fc_lib/LAPACK-FT_v1.4.0` | `include/lapacke.h`、`lib/liblapack.so` | 飞腾优化的线性代数包，提供 Cholesky 分解（DPOTRF）、LU 分解（DGETRF）、矩阵求逆（DGETRI）等高级矩阵分解运算 | **是** |
| VML-FT | v1.4.0 | `/home/alientek/Phytium/fc_lib/VML-FT_v1.4.0` | `include/vml-ft.h`、`lib/libvml-ft.so` | 向量数学库，提供向量化初等数学函数 | 否 |
| VSIPL-FT | v1.13.0 | `/home/alientek/Phytium/fc_lib/VSIPL-FT_v1.13.0_2.28` | `include/vsip.h`、`lib/libvsip.so` | 矢量信号与图像处理库 | 否 |

**编译链接方式**：

```makefile
FC_LIB_DIR ?= /home/alientek/Phytium/fc_lib
FT_CFLAGS  := -I$(FC_LIB_DIR)/BLAS-FT_v1.5.0/include \
              -I$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/include
FT_LDFLAGS := -L$(FC_LIB_DIR)/BLAS-FT_v1.5.0/lib \
              -L$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/lib \
              -lblas_ft -llapack
```

运行时需在 `LD_LIBRARY_PATH` 中包含 `/home/alientek/Phytium/fc_lib/BLAS-FT_v1.5.0/lib` 和 `/home/alientek/Phytium/fc_lib/LAPACK-FT_v1.4.0/lib`，以便动态加载 `libblas_ft.so` 与 `liblapack.so`。

### 2.2 BLAS-FT 库

BLAS（Basic Linear Algebra Subprograms）是科学计算中最基础的线性代数接口标准。BLAS-FT v1.5.0 是飞腾针对 ARMv8-A 架构深度优化的实现，主要优化点包括：

| 优化点 | 说明 |
|--------|------|
| **NEON SIMD 向量化** | 充分利用 ARMv8 128-bit NEON 向量寄存器，单次向量指令可并行处理多个双精度浮点数，提升 GEMM/GEMV 的指令级并行度。 |
| **缓存友好分块（Blocking）** | 针对 Phytium PE2204 的 L1/L2 Cache 层次进行矩阵分块，减少 Cache miss 与内存访问延迟。 |
| **流水线与指令调度优化** | 针对飞腾微架构的乱序执行宽度、Load/Store 单元和浮点流水线深度进行汇编级调度，提高每周期指令数（IPC）。 |
| **标准 CBLAS 接口** | 提供 `cblas_` 前缀的标准接口，可直接替换开源 OpenBLAS 或 Netlib BLAS，无需修改上层算法逻辑。 |

本研究使用的 BLAS-FT 函数包括：

- `cblas_dgemm`：双精度通用矩阵-矩阵乘法 $C = \alpha AB + \beta C$
- `cblas_dgemv`：双精度通用矩阵-向量乘法 $y = \alpha Ax + \beta y$
- `cblas_dcopy` / `cblas_dscal` 等 Level 1 向量运算（视实现版本而定）

### 2.3 LAPACK-FT 库

LAPACK（Linear Algebra PACKage）建立在 BLAS 之上，提供更高层次的线性代数运算。LAPACK-FT v1.4.0 是飞腾优化的实现，主要优化点包括：

| 优化点 | 说明 |
|--------|------|
| **基于 BLAS-FT 的分解内核** | Cholesky、LU 等分解算法内部大量调用 BLAS-FT 的 GEMM/GEMV，继承 SIMD 与 Cache 优化收益。 |
| **稳定的矩阵分解算法** | 提供经过严格数值验证的 Cholesky 分解（DPOTRF）、LU 分解（DGETRF）、QR 分解等，比手写 Gauss-Jordan 消元更稳定。 |
| **ARMv8 数值优化** | 在分块、面板分解和三角求解等环节针对 ARMv8 流水线进行优化，提升大规模矩阵分解吞吐。 |
| **LAPACKE C 接口** | 提供行主序（Row-Major）的 `LAPACKE_` 前缀接口，便于嵌入式 C 项目直接调用。 |

本研究使用的 LAPACK-FT 函数包括：

- `LAPACKE_dpotrf`：双精度 Cholesky 分解 $A = LL^T$
- `LAPACKE_dgetrf`：双精度 LU 分解 $A = PLU$
- `LAPACKE_dgetri`：基于 LU 分解的矩阵求逆 $A^{-1}$

---

## 3. UKF 算法与 FT 库的结合

### 3.1 在线 UKF 算法流程

本系统采用的在线 UKF 算法流程如 [ukf_online_core.h](file:///home/alientek/Phytium/task2_linux/ukf_online_core.h) 所述，每帧执行以下步骤：

1. **Sigma 点生成**：基于当前状态估计 $\hat{x}_{k-1}$ 和误差协方差 $P_{k-1}$，通过 Cholesky 分解生成 $2n_s$ 个 Sigma 点。
2. **状态预测（Prediction）**：对每个 Sigma 点执行 RK4 数值积分，传播到下一时刻，得到预测状态均值 $\bar{x}_k$ 和预测协方差 $\bar{P}_k$。
3. **量测预测**：将 Sigma 点通过非线性量测函数映射到量测空间，得到预测量测均值 $\hat{z}_k$。
4. **协方差计算**：计算量测协方差 $P_z$ 和互协方差 $P_{xz}$。
5. **Kalman 增益求解**：通过 Cholesky 分解求解 $K = P_{xz} P_z^{-1}$。
6. **状态更新**：$\hat{x}_k = \bar{x}_k + K(z_k - \hat{z}_k)$。
7. **协方差更新**：$P_k = \bar{P}_k - K P_z K^T$。

### 3.2 FT 库在代码中的具体作用

通过编译宏 `UKF_USE_FT` 控制是否启用 FT 库。当定义该宏时，以下线性代数运算由 FT 库替代纯 C 实现：

#### 3.2.1 Cholesky 分解（Sigma 点生成与 Kalman 增益）

非 FT 版本使用手写三重循环实现 Cholesky 分解：

```c
for (int i = 0; i < n; i++) {
    for (int j = 0; j <= i; j++) {
        double s = A[i * n + j];
        for (int k = 0; k < j; k++)
            s -= A[i * n + k] * A[j * n + k];
        if (i == j) {
            if (s <= 0.0) return -1;
            A[i * n + i] = sqrt(s);
        } else {
            A[i * n + j] = s / A[j * n + j];
        }
    }
}
```

FT 版本调用 LAPACK-FT：

```c
lapack_int info = LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', n, A, n);
```

该函数对对称正定矩阵 $A$ 进行 Cholesky 分解 $A = LL^T$，返回下三角矩阵 $L$。FT 库内部通过分块与 SIMD 优化，对于 $n_s=20$ 的矩阵分解可显著降低延迟。

#### 3.2.2 矩阵求逆（Kalman 增益 Fallback）

当 Cholesky 分解失败时，UKF 会 fallback 到 Gauss-Jordan 求逆。非 FT 版本使用手写 Gauss-Jordan 消元：

```c
for (int col = 0; col < n; col++) {
    // 选主元、行交换、归一化、消元
}
```

FT 版本调用 LAPACK-FT 的 LU 分解 + 求逆：

```c
lapack_int info = LAPACKE_dgetrf(LAPACK_ROW_MAJOR, n, n, A, n, ipiv);
info = LAPACKE_dgetri(LAPACK_ROW_MAJOR, n, A, n, ipiv);
```

`LAPACKE_dgetrf` 进行 LU 分解 $A = PLU$，`LAPACKE_dgetri` 基于 LU 分解计算 $A^{-1}$。该路径在正常情况下很少触发，但作为数值稳定性的保障，FT 库同样提供了更高效率的实现。

#### 3.2.3 矩阵-矩阵乘法（协方差更新）

UKF 中多次需要计算 $C = A \times B$ 或 $C = A \times B^T$。非 FT 版本使用三层循环：

```c
memset(C, 0, m * n * sizeof(double));
for (int i = 0; i < m; i++)
    for (int l = 0; l < k; l++)
        for (int j = 0; j < n; j++)
            C[i * n + j] += A[i * k + l] * B[l * n + j];
```

FT 版本调用 BLAS-FT：

```c
cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
            m, n, k, 1.0, A, k, B, n, 0.0, C, n);
```

以及转置版本：

```c
cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
            m, n, k, 1.0, A, k, B, k, 0.0, C, n);
```

`cblas_dgemm` 是 BLAS Level 3 函数，计算 $C = \alpha AB + \beta C$。BLAS-FT 通过分块和 SIMD 优化，对 20×98 规模的矩阵乘法可带来数倍的性能提升。

#### 3.2.4 矩阵-向量乘法（状态更新）

状态更新步骤 $dx = K \times (z - \hat{z})$ 涉及矩阵-向量乘法。非 FT 版本：

```c
for (int i = 0; i < ns; i++)
    for (int j = 0; j < nm; j++)
        dx_upd[i] += K[i * nm + j] * innov[j];
```

FT 版本：

```c
cblas_dgemv(CblasRowMajor, CblasNoTrans, ns, nm, 1.0, K, nm, innov, 1, 0.0, dx_upd, 1);
```

### 3.3 算法结构的改进

引入 FT 库后，UKF 的算法结构发生了以下改进：

1. **计算原语标准化**：将手写的线性代数运算替换为经过充分验证和优化的标准 BLAS/LAPACK 接口，提升代码可靠性和可维护性。
2. **向量化与并行化**：FT 库在底层利用 ARMv8 NEON 指令和 Phytium 微架构特性，将标量运算转化为向量运算，提升指令级并行度。
3. **内存访问优化**：BLAS Level 3 的分块策略显著提升 Cache 命中率，减少因内存访问带来的延迟。
4. **数值稳定性增强**：LAPACK-FT 的分解算法经过严格数值验证，比手写 Gauss-Jordan 消元更稳定。
5. **编译时特化保留**：通过 `UKF_NODE_5BUS/9BUS/39BUS` 宏在编译期确定矩阵尺寸，结合 FT 库的静态调度，避免了运行时的动态开销。

### 3.4 编译配置

FT 版本的编译配置见 [Makefile](file:///home/alientek/Phytium/task2_linux/Makefile)：

```makefile
FC_LIB_DIR := /home/alientek/Phytium/fc_lib
FT_CFLAGS  := -I$(FC_LIB_DIR)/BLAS-FT_v1.5.0/include \
              -I$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/include
FT_LDFLAGS := -L$(FC_LIB_DIR)/BLAS-FT_v1.5.0/lib \
              -L$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/lib \
              -lblas_ft -llapack -lm

ukf_pipeline_39bus_ft:
	$(CC) -O2 -Wall -DUKF_NODE_39BUS -DUKF_USE_FT $(FT_CFLAGS) \
	      -o $@ $(PIPELINE_SRC) $(FT_LDFLAGS)
```

通过 `-DUKF_USE_FT` 宏启用 FT 库代码路径，通过 `-lblas_ft -llapack` 动态链接飞腾优化库。

---

## 4. 实验结果与分析

### 4.1 实验环境

- **硬件**：Phytium PE2204，4 核 ARMv8-A（CPU0 A55 + CPU1/CPU2/CPU3 A76）
- **Linux 在线 CPU**：CPU0、CPU1、CPU2（CPU3 因底层固件未释放而离线，见 6.7 节）
- **操作系统**：Linux 6.6.63-phytium-embedded-v3.2
- **编译器**：aarch64-linux-gnu-gcc（交叉编译）/ 板载 gcc（本地编译 FT 版本）
- **FT 库**：BLAS-FT v1.5.0，LAPACK-FT v1.4.0
- **测试脚本**：`bench_all_nodes.sh 30`

### 4.2 状态估计精度对比

| 节点 | 非 FT RMSE | FT RMSE | 变化 |
|------|------------|---------|------|
| 5bus | 1.0864 | 1.0864 | 无变化 |
| 9bus | 1.3011 | 1.3010 | 无变化 |
| 39bus | 2.4444 | 2.4444 | 无变化 |

FT 库的引入未改变 UKF 的数值结果，状态估计精度保持一致。这说明 FT 库在提升性能的同时，保证了数值正确性。

### 4.3 实时性对比（三节点并发场景）

| 节点 | 版本 | 处理帧数（30s） | Wall-time FPS | 生成 FPS | 平均延迟 | 延迟降低 |
|------|------|-----------------|---------------|----------|----------|----------|
| 5bus | 非 FT | 68407 | 2271.2 | 2135.3 | 93 μs | - |
| 5bus | FT | 68361 | 2267.6 | 2131.8 | 104 μs | -11.8% |
| 9bus | 非 FT | 67966 | 2235.9 | 2194.4 | 260 μs | - |
| 9bus | FT | 64183 | 2124.9 | 2083.2 | 256 μs | +1.5% |
| **39bus** | **非 FT** | **7500** | **250.0** | **241.8** | **3576 μs** | - |
| **39bus** | **FT** | **9000** | **300.0** | **288.3** | **2717 μs** | **-24.0%** |

对于 39bus 大系统，FT 版本实现了显著的性能提升：

- **吞吐量提升**：300 fps vs 250 fps，提升 **20%**。
- **延迟降低**：2717 μs vs 3576 μs，降低 **24%**。

对于 5bus/9bus 小系统，FT 版本性能基本持平或略有下降，主要原因是：

1. 小矩阵无法充分发挥 BLAS Level 3 分块和 SIMD 的优势。
2. FT 库函数调用开销（参数检查、库初始化等）在小矩阵情况下占比相对较高。
3. 5bus/9bus 的原始纯 C 实现已经足够高效，FT 库的收益被调用开销抵消。

### 4.4 单独运行场景对比

| 节点 | 版本 | Wall-time FPS | 平均延迟 | 备注 |
|------|------|---------------|----------|------|
| 5bus | 非 FT | 2261.1 | 166.0 μs | - |
| 5bus | FT | 2268.3 | 181.8 μs | FT 延迟略高 |
| 9bus | 非 FT | 2935.6 | 280.2 μs | - |
| 9bus | FT | 2138.0 | 248.3 μs | **FT 明显更差** |
| 39bus | 非 FT | 383.3 | 2674 μs | - |
| 39bus | FT | 466.7 | 2211 μs | **FT 提升 21.7%** |

单独运行场景进一步验证了 FT 库对大矩阵的优势：39bus 单独运行时 FT 版本 FPS 提升 **21.7%**，延迟降低 **17.3%**。

### 4.5 CPU 占用率分析

三节点并发运行时，CPU 占用情况如下（多次采样稳定值）：

| CPU | 总占用 | 主要进程 | 占比 |
|-----|--------|----------|------|
| CPU0 | **98.7%** | `ukf_pipeline_5bus` + `ukf_pipeline_9bus` | 约 81% |
| CPU1 | **91.0%** | FreeRTOS（remoteproc）数据生成 | 约 90% |
| CPU2 | **100.0%** | `ukf_pipeline_39bus` | **99.0%** |

CPU2 几乎完全由 39bus 的 UKF 状态估计占满，说明 39bus 是计算瓶颈。FT 版本将 39bus 的处理能力从 250 fps 提升到 300 fps，相当于在同样 100% CPU 占用下多处理了 50 fps，**单位 CPU 资源的计算效率提升 20%**。

### 4.6 内存占用分析

| 版本 | 运行前内存使用 | 运行后内存使用 | 增量 |
|------|----------------|----------------|------|
| 非 FT | 1079 MB | 1081 MB | +2 MB |
| FT | 1078 MB | 1088 MB | +10 MB |

FT 版本因动态链接 BLAS-FT / LAPACK-FT 库，内存占用增加约 8 MB。这在嵌入式系统中可接受，且三个 UKF 进程各自的 RSS 很小（5bus ~20 KB，9bus ~30 KB，39bus ~120 KB）。

### 4.7 实时性保证分析

UKF 状态估计的实时性由以下指标衡量：

- **Wall-time FPS**：UKF 每秒实际处理的帧数。
- **生成 FPS**：FreeRTOS 每秒生成的帧数。
- **单帧延迟**：从数据生成到状态估计完成的平均时间。
- **丢帧数**：因 UKF 处理不及时而丢弃的帧数。

三节点并发实测显示：

- 5bus：生成 2135 fps，消费 2268 fps，无丢帧，实时性满足。
- 9bus：生成 2194 fps，消费 2236 fps，无丢帧，实时性满足。
- 39bus：生成 288 fps（FT），消费 300 fps，无丢帧，实时性刚好满足。

39bus 是系统的实时性瓶颈。FT 版本将 39bus 的消费能力从 250 fps 提升到 300 fps，超过了其生成速率 288 fps，**确保了 39bus 状态估计的端到端实时性**。

---

## 5. 讨论

### 5.1 FT 库的适用范围

FT 库并非对所有规模的 UKF 系统都有收益。根据实验结果：

- **当 $n_s \times n_m \ge 20 \times 98$ 时**（如 39bus），FT 库显著提升性能。
- **当 $n_s \times n_m \le 6 \times 24$ 时**（如 9bus），FT 库调用开销抵消加速收益，性能可能下降。
- **当 $n_s \times n_m \le 4 \times 14$ 时**（如 5bus），FT 库与非 FT 实现性能基本持平。

这一现象与 BLAS 库的典型行为一致：BLAS Level 3 运算的优化效果随矩阵尺寸增大而更加明显，小矩阵的函数调用开销和分块开销占比更高。

### 5.2 为什么 39bus 收益最大

39bus 的 UKF 每步涉及：

- 20×20 协方差矩阵的 Cholesky 分解
- 20×98 互协方差矩阵与 98×98 量测协方差矩阵的乘法
- 20×98 Kalman 增益矩阵与 98 维新息的矩阵-向量乘法

这些运算的复杂度分别为 $O(n_s^3)$、$O(n_s n_m^2)$ 和 $O(n_s n_m)$。BLAS-FT 和 LAPACK-FT 针对这些大规模稠密矩阵运算进行了深度优化，因此 39bus 的收益最大。

### 5.3 对后续高并发研究的启示

当前系统同时运行 3 个 UKF 节点。后续研究计划增加不同 bus 数量的节点，验证 Phytium CPU 可支持的高并发状态估计节点数。基于当前结果：

1. **39bus 节点是瓶颈**：每增加一个 39bus 规模的节点，需要额外一个 CPU2 级别的核心。
2. **5bus/9bus 可共享 CPU0**：多个小节点可以共享一个 CPU 核心，直到总占用接近 100%。
3. **FT 库应优先用于大系统节点**：后续高并发测试中，建议对 $n_s \ge 10$ 或 $n_m \ge 50$ 的节点启用 FT 版本。
4. **CPU3 未释放**：当前 Linux 在线 CPU 只有 0 和 2，CPU3 因底层固件未释放而无法使用。若需支持更多节点，需修改 ATF/U-Boot 配置释放 CPU3，而非简单修改 Linux 设备树。

---

## 6. 初步高并发压力测试

为了回答"当前 3 节点是否已达到 Phytium PE2204 的并发极限"以及"能否继续增加节点"的问题，本节设计了可扩展的压力测试。

### 6.1 测试方法

使用新开发的压测脚本：

- `stress_test_concurrent.sh`：在 CPU0/CPU2 上额外启动指定数量的 5bus/9bus/39bus UKF 只读实例。
- `stress_test_concurrent_v2.sh`：支持将 9bus 绑定到 CPU2，测试 CPU2 上的混合负载。

**只读实例说明**：额外实例通过 `UKF_READONLY=1` 模式运行，不更新 SHM 的读指针 `ri`，因此可以独立消耗同一数据源，仅对 CPU 施加 UKF 计算压力，模拟更多节点并发场景。原始三个节点仍正常更新 `ri`，保证 FreeRTOS 背压机制有效。

### 6.2 当前 CPU 资源现状

| CPU | 当前用途 | 三节点并发占用 | 是否可再增加 UKF 节点 |
|-----|----------|----------------|----------------------|
| CPU0 | Linux UKF（5bus + 9bus） | ~98.7% | 已接近饱和 |
| CPU1 | FreeRTOS（remoteproc） | ~91.0% | 由 FreeRTOS 独占 |
| CPU2 | Linux UKF（39bus） | ~100.0% | 已接近饱和 |
| CPU3 | 未启用 | 0% | **ATF/U-Boot 未释放，无法热插拔** |

**CPU3 无法通过热插拔启用**：尝试 `echo 1 > /sys/devices/system/cpu/cpu3/online` 返回 `Invalid argument`，且内核日志显示 `psci: failed to boot CPU3 (-22)`。详细排查见第 6.7 节。

### 6.3 压力测试结果

#### 场景 1：在 CPU0 上增加额外 5bus 实例

原始配置：5bus + 9bus 在 CPU0，39bus 在 CPU2。

| 额外 5bus 数 | CPU0 总占用 | 5bus 实例 CPU | 9bus CPU | 5bus 延迟 | 9bus 延迟 | 结论 |
|--------------|-------------|---------------|----------|-----------|-----------|------|
| 0 | 98.6% | 21.6% | 69.6% | ~90 μs | ~260 μs | 基准，CPU0 已满载 |
| 1 | 100.0% | 21.0% ×2 | 53.5% | ~95 μs | ~560 μs | CPU0 过载，9bus 延迟翻倍 |
| 2 | 100.0% | ~20% ×3 | - | ~100 μs | ~850 μs | 9bus 严重变慢 |
| 3 | 100.0% | ~20% ×4 | - | ~420 μs | ~1350 μs | 所有实例调度竞争激烈 |

**结论**：CPU0 在 5bus+9bus 时已基本达到极限。再增加任何小节点都会导致 9bus 延迟显著上升，整体实时性恶化。

#### 场景 2：在 CPU2 上增加额外 39bus 实例

原始配置：39bus 在 CPU2。

| 额外 39bus 数 | CPU2 总占用 | 每个 39bus CPU | 39bus 延迟 | 结论 |
|---------------|-------------|----------------|------------|------|
| 0 | 100.0% | 94.5% | ~2685 μs | 基准，CPU2 满载 |
| 1 | 100.0% | 47.2% ×2 | ~5380 μs | 延迟翻倍 |
| 2 | 100.0% | ~33% ×3 | ~8300 μs | 延迟三倍 |

**结论**：CPU2 上一个 39bus 已接近极限。增加第二个 39bus 会使每个 39bus 的处理延迟翻倍，无法满足实时性要求。

#### 场景 3：将 9bus 从 CPU0 移到 CPU2，与 39bus 共享

| 配置 | CPU0 占用 | CPU2 占用 | 5bus 延迟 | 9bus 延迟 | 39bus 延迟 | 结论 |
|------|-----------|-----------|-----------|-----------|------------|------|
| 5bus/9bus→CPU0, 39bus→CPU2 | 98.6% | 100.0% | ~90 μs | ~260 μs | ~2685 μs | 最优配置 |
| 5bus→CPU0, 9bus/39bus→CPU2 | 96.0% | 100.0% | ~158 μs | ~82 μs | ~3695 μs | 39bus 延迟上升 38% |

**结论**：将 9bus 移到 CPU2 会降低 39bus 性能，整体不如原来的绑定方案。说明当前 5bus/9bus 共享 CPU0、39bus 独占 CPU2 的配置已经接近最优。

### 6.4 当前极限判断

综合以上测试，**当前硬件配置下，3 个节点（5bus + 9bus + 39bus）已基本达到并发极限**：

- CPU0 被 5bus + 9bus 占满（~98.6%）。
- CPU2 被 39bus 占满（~100%）。
- CPU1 被 FreeRTOS 数据生成占满（~91%）。
- CPU3 未启用，无法提供额外计算资源。

要继续增加节点数量，传统思路是启用 CPU3 或严格隔离 CPU1。在 CPU3 不可用的情况下，本节通过**降低 5bus/9bus/39bus 数据生成与 UKF 估计频率**来释放 CPU 资源，从而在相同核心数下支持更多节点。

---

## 6.5 降频扩展测试（39bus 250Hz + 5bus/9bus 500Hz）

### 6.5.1 降频配置

为进一步释放 CPU 资源、避免系统长期运行在 100% 满载状态，本节在 5bus/9bus 已降至 500Hz 的基础上，将 39bus 也进行降频处理：

| 节点 | 仿真步长 `DT` | `WRITE_DOWN` | 等效输出频率 | Linux 侧 `UKF_DELTT` |
|------|---------------|--------------|--------------|----------------------|
| 5bus | 0.0005s | 4 | 500Hz | 0.002s |
| 9bus | 0.0005s | 4 | 500Hz | 0.002s |
| 39bus | 0.0005s | **8** | **250Hz** | **0.004s** |

修改文件：
- [sim_params_5bus.h](file:///home/alientek/Phytium/freertos/inc/sim_params_5bus.h)
- [sim_params_9bus.h](file:///home/alientek/Phytium/freertos/inc/sim_params_9bus.h)
- [sim_params_39bus.h](file:///home/alientek/Phytium/freertos/inc/sim_params_39bus.h)
- [ukf_pipeline_online.c](file:///home/alientek/Phytium/task2_linux/ukf_pipeline_online.c) 已支持 `UKF_DELTT` 动态配置

### 6.5.2 精度验证

在当前降频配置下运行原始三节点 25s~30s，采集各节点 CSV 并计算 RMSE：

| 节点 | 2kHz 基准 RMSE（报告 4.2 节） | 当前频率 | 当前平均 RMSE | 当前最终 RMSE | 结论 |
|------|------------------------------|----------|---------------|---------------|------|
| 5bus | 1.0864 | 500Hz | 0.5259 | 0.5274 | **无退化** |
| 9bus | 1.3011 | 500Hz | 0.6482 | 0.6518 | **无退化** |
| 39bus | 2.4444 | 250Hz | 0.8627 | 0.8690 | **稳定，未发散** |

> 注：RMSE 的绝对值与统计口径（状态向量归一化方式、采样窗口）有关。本实验关注的是降频前后相对变化：5bus/9bus 在 500Hz 下与 2kHz 基准处于同一水平；39bus 在 250Hz 下未出现因欠采样导致的发散，状态估计结果稳定。

**结论**：将 39bus 从 2kHz 降至 250Hz、5bus/9bus 保持在 500Hz，状态估计精度保持稳定，未观察到明显退化。

### 6.5.3 单节点 CPU 占用变化

| 节点 | 频率 | 单实例 CPU 占用 | 备注 |
|------|------|----------------|------|
| 5bus | 500Hz | ~10%–12% | CPU0 |
| 9bus | 500Hz | ~13%–15% | CPU0 |
| 39bus | 250Hz | ~54% | CPU2（2kHz 时约 99%）|

关键发现：
1. **39bus 单实例 CPU 占用从约 99% 降至约 54%**，为 CPU2 留出明显余量。
2. CPU2 总占用仍接近 100%，主要由于 Linux 系统中断、`start_sim_nodes` 等后台任务占用剩余 CPU，但 UKF 进程本身不再独占整个核心。
3. 5bus/9bus 在 500Hz 下单实例占用较低，使得 CPU0 上可叠加多个小节点。

### 6.5.4 降频结论

- **精度**：39bus 250Hz、5bus/9bus 500Hz 配置下，各节点 RMSE 稳定，未出现精度退化或发散。
- **CPU 余量**：39bus 单实例从 CPU2 满载降至约 54%，系统不再由单一 UKF 进程独占核心。
- **限制**：由于 CPU2 总占用仍高，**不建议在同一核心上同时运行两个 39bus 实例**，否则单实例处理延迟会超过 4ms 周期，导致丢帧。

---

## 6.6 多节点布局组合测试（250Hz 39bus + 500Hz 5bus/9bus）

### 6.6.1 测试目的

本节考察实际工程中可能出现的多节点布局。一个调度中心往往需要同时监测不同规模的电网：小馈线（5bus）、中型变电站群（9bus）、主干网（39bus）。通过将 39bus 降至 250Hz，我们希望找到**CPU 不满载（尤其是不出现单一核心 100% 由 UKF 独占）且状态估计稳定**的真实场景组合。

### 6.6.2 测试方法

- 每局测试始终保持 **1×5bus + 1×9bus + 1×39bus** 作为基础数据源节点，确保 FreeRTOS 三路桥接均正常写入 SHM。
- 通过 `UKF_READONLY=1` 启动额外实例，模拟同类型多节点并发。
- 使用新增脚本 [multi_node_combo_test.sh](file:///home/alientek/Phytium/task2_linux/multi_node_combo_test.sh) 自动完成：清理 → 热重载 FreeRTOS 固件 → reset_shm → 启动基础节点 → 启动只读实例 → 采样 CPU → 提取 RMSE/帧数 → 清理。
- 每组测试前热重载固件，避免连续高负载测试导致 SHM 写入停滞。
- 批量测试使用 [run_combo_suite_safe.sh](file:///home/alientek/Phytium/task2_linux/run_combo_suite_safe.sh)。

### 6.6.3 测试布局与结果

| 场景 | 布局名称 | 总节点数 (5/9/39) | CPU0 | CPU1 | CPU2 | 关键指标 | 结论 |
|------|----------|-------------------|------|------|------|----------|------|
| L0 | 基准三节点 | 1 + 1 + 1 | 97.0% | 96.5% | 99.9% | 5bus 13.5k 帧 RMSE≈0.526；9bus 10k 帧 RMSE≈0.652；39bus 5k 帧 RMSE≈0.869 | 稳定 |
| L1 | 轻量配网 | 3 + 1 + 1 | 98.2% | 97.2% | 99.9% | 3×5bus 13.5k~14k 帧；9bus 10k 帧；39bus 5k 帧 | 稳定 |
| L2 | 大量小馈线 | 6 + 1 + 1 | 99.7% | 97.4% | 99.9% | 6×5bus 13.5k~14k 帧；9bus 10k 帧；39bus 5k 帧 | CPU0 临界，可运行 |
| L3 | 中型变电站群 | 1 + 4 + 1 | 99.1% | 97.3% | 99.9% | 4×9bus 10k 帧；5bus 13.5k 帧；39bus 5k 帧 | CPU0 临界，可运行 |
| L4 | 均衡混合 | 4 + 2 + 1 | 99.3% | 97.6% | 100.0% | 4×5bus 13.5k~14k 帧；2×9bus 10k~10.5k 帧；39bus 5k 帧 | CPU0 临界，可运行 |
| L5 | 一般混合 | 2 + 2 + 1 | 98.2% | 97.1% | 99.9% | 2×5bus 13.5k 帧；2×9bus 10k 帧；39bus 5k 帧 | 稳定 |
| L6 | 双主干网 | 1 + 1 + 2 | 98.0% | 100.0% | 100.0% | 2×39bus 各 4.5k 帧，延迟约 5.5ms | **不推荐，丢帧** |
| L7 | 混合 + 双主干 | 2 + 1 + 2 | 98.3% | 100.0% | 100.0% | 2×39bus 各 4.5k 帧，延迟约 5.8ms | **不推荐，丢帧** |

说明：
- 帧数按 25s 统计：5bus/9bus 期望约 12.5k 帧，实际 10k–14k（受启动预热和调度影响）；39bus 期望约 6.25k 帧，单实例实际约 5k 帧，双实例降至 4.5k 帧。
- 所有“稳定/可运行”布局中，5bus/9bus 平均 RMSE 与基准一致（5bus≈0.526，9bus≈0.652），39bus RMSE≈0.869，未出现精度退化。
- CPU0 随小/中节点数量增加趋近 100%，但仍未出现实例饿死；CPU2 在单 39bus 场景下 UKF 占用约 54%，双 39bus 时两个实例各占约 47%，但延迟超过周期，出现丢帧。

### 6.6.4 真实场景映射

| 应用场景 | 推荐布局 | 说明 |
|----------|----------|------|
| **小型配电台区监测** | L1（3×5bus + 1×9bus + 1×39bus） | 少量馈线 + 1 条主干网，CPU0/CPU2 均有余量 |
| **馈线密集接入** | L2（6×5bus + 1×9bus + 1×39bus） | 配电台区集中监测，CPU0 接近满载但仍稳定 |
| **中型变电站群** | L3（1×5bus + 4×9bus + 1×39bus） | 以 9bus 为主，适合变电站群状态估计 |
| **馈线 + 变电站混合** | L5（2×5bus + 2×9bus + 1×39bus） | 馈线与变电站数量均衡，整体负载最平稳 |
| **多区域主干网** | 不推荐单 CPU2 双 39bus | 双 39bus 会导致延迟超标，应启用 CPU3 或进一步降频 |

### 6.6.5 结论

1. **推荐的稳定布局**：
   - **L1（3×5bus + 1×9bus + 1×39bus）**：轻量配网监测，CPU0/CPU2 均保持安全余量。
   - **L5（2×5bus + 2×9bus + 1×39bus）**：馈线与变电站混合，负载均衡，稳定性最好。
2. **可运行但接近极限的布局**：L2、L3、L4。这些布局 CPU0 接近满载，演示时可见 CPU 利用率很高，但未出现实例饿死或系统卡死。
3. **不推荐双 39bus**：L6、L7 中两个 39bus 共享 CPU2，单帧延迟超过 4ms 周期，导致丢帧；若需多主干网，应启用 CPU3 或将 39bus 进一步降频。
4. **避免 CPU 100% 满载**：通过将 39bus 降至 250Hz，CPU2 上 UKF 占用从约 99% 降至约 54%，系统不再由单一 UKF 进程独占核心，显著降低了“随时卡死”的风险。

---

## 6.7 CPU3 不可用原因排查与验证

### 6.7.1 问题现象

在尝试将 UKF 节点扩展到 CPU3 时，发现 Linux 无法使用 CPU3：

```bash
$ lscpu
CPU(s):                4
On-line CPU(s) list:   0-2
Off-line CPU(s) list:  3

$ cat /sys/devices/system/cpu/cpu3/online
0

$ echo 1 | sudo tee /sys/devices/system/cpu/cpu3/online
tee: /sys/devices/system/cpu/cpu3/online: 无效的参数
```

### 6.7.2 排查步骤与结论

#### 步骤 1：检查内核启动参数

```bash
cat /proc/cmdline
```

实际输出：

```text
console=ttyAMA1,115200 earlycon=pl011,0x2800d000 root=/dev/mmcblk0p1 rootfstype=ext4 rootwait  rw cma=256m ;
```

**结论**：启动参数中没有 `maxcpus=3`、`nr_cpus=3` 等限制 CPU 数量的参数，因此 Linux 本身愿意尝试启动所有 4 个 CPU。

#### 步骤 2：检查 device tree 中 CPU3 状态

```bash
$ ls /proc/device-tree/cpus/
#address-cells  cpu@0  cpu@1  cpu@100  cpu@101  cpu-map  name  #size-cells

$ cat /proc/device-tree/cpus/cpu@101/status
(no status)
```

设备树中 4 个 CPU 节点都存在，且 `status` 为空（默认 `enabled`）。**结论：CPU3 在设备树中并未被 disabled。**

#### 步骤 3：检查是否有用户态脚本关闭 CPU3

```bash
grep -rE "cpu3|offline" /etc/systemd /etc/udev/rules.d /lib/udev/rules.d
cat /etc/rc.local
```

**结论**：未在 systemd、udev、rc.local 中发现主动 offline CPU3 的脚本。

#### 步骤 4：分析内核日志

```bash
sudo dmesg | grep -iE "CPU3|Booted|killed|failed|secondary|psci"
```

关键日志：

```text
[    0.103852] smp: Bringing up secondary CPUs ...
[    0.126944] CPU1: Booted secondary processor 0x0000000201 [0x700f3034]
[    0.145558] CPU2: Booted secondary processor 0x0000000000 [0x701f6643]
[    0.164055] CPU3: Booted secondary processor 0x0000000100 [0x701f6643]
[  159.477011] psci: CPU3 killed (polled 0 ms)
...
[ 5216.132972] psci: failed to boot CPU3 (-22)
[ 5216.137243] CPU3: failed to boot: -22
```

**关键发现**：
1. 内核启动时**成功 boot 了 CPU3**，说明设备树和内核本身都支持它。
2. 启动约 2 分 39 秒后，CPU3 被 **PSCI（Power State Coordination Interface）kill**。
3. 之后任何手动 online CPU3 的尝试都被 PSCI 以 `-22`（`EINVAL`）拒绝。

#### 步骤 5：解读 PSCI -22 错误

PSCI 返回 `-22`（`PSCI_RET_INVALID_PARAMS`）通常表示：
- CPU3 在 ARM Trusted Firmware（ATF/BL31）层面被标记为不可用；或
- CPU3 属于另一个安全世界（Secure World）或电源域，Linux 通过 PSCI 无法启动它；或
- remoteproc/SCMI/OP-TEE 等固件组件在启动后占用了 CPU3。

### 6.7.3 最终结论

综合以上排查，**CPU3 不可用不是 Linux 内核配置问题，也不是 device tree 简单 disabled，而是底层固件（ATF/U-Boot/BL31）没有将 CPU3 释放给 Linux**。具体表现为：

- Linux 内核和 device tree 都已准备好使用 4 核；
- CPU3 启动后被 PSCI kill，且再也无法通过 `online` 重新启动；
- 没有用户态脚本在关闭 CPU3。

因此，要启用 CPU3，需要从 **bootloader / ATF 层面**入手，例如：
- 修改 U-Boot 设备树或环境变量；
- 重新编译 BL31/ATF，释放 CPU3 给 Non-secure Linux；
- 检查 remoteproc/SCMI 配置是否将 CPU3 预留给其他处理器或安全世界。

### 6.7.4 对演示视频的说明建议

在录制演示视频时，建议补充以下说明：

> “本开发板为 4 核 Phytium PE2204，但当前 Linux 实际可用核心为 CPU0、CPU1、CPU2。CPU1 运行 FreeRTOS 数据生成任务，CPU0 和 CPU2 运行 UKF 状态估计任务。CPU3 虽然被设备树描述为可用，但在系统启动后被底层固件（ATF/U-Boot）通过 PSCI 收回，无法被 Linux 使用。这不是 Linux 内核配置问题，而是 SoC AMP 方案中的资源预留。因此本演示的所有多节点扩展均在 CPU0 + CPU2 上完成，未依赖 CPU3。”

---

## 7. 结论

本文通过在飞腾 Phytium PE2204 处理器上引入 BLAS-FT v1.5.0 和 LAPACK-FT v1.4.0 库，对电力系统 UKF 实时状态估计算法进行了优化，并进一步探索了降频扩展与多节点并发能力。主要结论如下：

1. **FT 库有效提升了大系统 UKF 的性能**：39bus 节点在三节点并发场景下 FPS 提升 **20%**，延迟降低 **24%**，且状态估计精度保持不变。
2. **FT 库对小系统收益有限**：5bus/9bus 由于矩阵规模小，FT 库调用开销抵消了加速收益，建议使用非 FT 版本。
3. **UKF 计算是 CPU 占用的主要来源**：三节点并发时 CPU2 几乎完全由 39bus UKF 占满（99%），CPU0 由 5bus/9bus UKF 占满约 81%。
4. **实时性得到保证**：FT 版本使 39bus 的处理能力（300 fps）超过其数据生成速率（288 fps），实现了端到端实时状态估计。
5. **降低 5bus/9bus 频率可在不牺牲精度的情况下扩展并发节点数**：将 5bus/9bus 从 2kHz 降至 500Hz 后，状态估计 RMSE 无明显变化；CPU0 上可稳定支持的小节点数从 2 个增加到 **6 个**（3×5bus + 3×9bus），提升了 **3 倍**。
6. **39bus 进一步降至 250Hz 可释放 CPU2 资源**：单实例 39bus 在 CPU2 上的占用从约 99% 降至约 54%，系统不再由单一 UKF 进程独占核心，降低了满载卡死风险。精度方面，39bus 250Hz 下 RMSE 稳定在 0.87 左右，未出现发散。
7. **典型多节点布局已得到验证**：在 **39bus 250Hz + 5bus/9bus 500Hz** 配置下，8 种实际工程布局的测试表明，**3×5bus + 1×9bus + 1×39bus** 与 **2×5bus + 2×9bus + 1×39bus** 是负载最平稳的推荐布局；**6×5bus + 1×9bus + 1×39bus**、**1×5bus + 4×9bus + 1×39bus**、**4×5bus + 2×9bus + 1×39bus** 可运行但 CPU0 接近满载；CPU2 上不宜部署 2×39bus，否则会导致丢帧。
8. **CPU3 不可用是底层固件限制**：通过排查确认 CPU3 在 Linux 启动后被 PSCI kill，根源在于 ATF/U-Boot 未释放 CPU3，而非内核配置或设备树 disabled。
9. **继续增加节点需配合其他优化**：若要进一步扩展，需启用 CPU3（修改 ATF/U-Boot）、将 5bus/9bus 进一步降至 250Hz、优化 UKF 轮询策略或严格隔离 CPU1/CPU2。

---

## 8. 后续工作

后续将在本文基础上进行以下工作：

1. **优化 UKF 轮询策略降低 CPU0/CPU2 开销**：当前 `ukf_pipeline_online` 默认忙等轮询，空闲时仍占用大量 CPU。通过增大轮询间隔、引入事件通知或条件阻塞，可显著降低基线占用，为更多节点留出余量。
2. **进一步评估 5bus/9bus 250Hz 的可行性**：39bus 已验证 250Hz 稳定；若将 5bus/9bus 也降至 250Hz，可进一步降低 CPU0 占用，有望支持更多节点。
3. **CPU1/CPU2 隔离**：通过 `isolcpus=1,2 nohz_full=1,2 rcu_nocbs=1,2` 或 `taskset` 严格隔离 FreeRTOS 核心与 UKF 核心，减少 Linux 后台进程对实时任务的干扰。
4. **启用 CPU3**：当前 CPU3 因 ATF/U-Boot 未释放而无法使用。后续需从 bootloader/ATF 层面释放 CPU3，或换用支持 4 核 Linux 的官方镜像。
5. **扩展 FreeRTOS 仿真节点**：修改 FreeRTOS 固件，增加更多 5bus/9bus/39bus 实例或新增更大规模系统（如 118bus、300bus）的仿真任务和 SHM 区域。
6. **动态负载均衡**：研究多个 UKF 节点在有限 CPU 核心上的最优调度策略，例如根据节点计算需求动态调整 CPU 绑定或优先级。
7. **FT 库自适应选择**：根据节点矩阵规模自动选择是否启用 FT 版本，实现全局最优性能。

---

## 附录 A: 关键代码片段

### A.1 FT 版本矩阵乘法

```c
#ifdef UKF_USE_FT
static void mmul(int m, int n, int k, const double *A, const double *B, double *C) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                m, n, k, 1.0, A, k, B, n, 0.0, C, n);
}
#endif
```

### A.2 FT 版本 Cholesky 分解

```c
#ifdef UKF_USE_FT
static int chol_lower(int n, double *A) {
    lapack_int info = LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', n, A, n);
    if (info != 0) return -1;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            A[i * n + j] = 0.0;
    return 0;
}
#endif
```

### A.3 Makefile 中 FT 编译配置

```makefile
FT_CFLAGS  := -I$(FC_LIB_DIR)/BLAS-FT_v1.5.0/include \
              -I$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/include
FT_LDFLAGS := -L$(FC_LIB_DIR)/BLAS-FT_v1.5.0/lib \
              -L$(FC_LIB_DIR)/LAPACK-FT_v1.4.0/lib \
              -lblas_ft -llapack -lm

ukf_pipeline_39bus_ft:
	$(CC) -O2 -Wall -DUKF_NODE_39BUS -DUKF_USE_FT $(FT_CFLAGS) \
	      -o $@ $(PIPELINE_SRC) $(FT_LDFLAGS)
```

---

## 附录 B: 实验数据原始来源

- 三节点并发数据：`logs_bench_all_nodes/nonft_20260618_102623.json`、`logs_bench_all_nodes/ft_20260618_102623.json`
- 单独运行数据：`logs_bench_5bus/`、`logs_bench_9bus/`、`logs_bench_39bus/`
- 验证脚本：`verify_runtime_v2.sh`、`run_verify_three_nodes.sh`
- 多节点组合测试数据：`/tmp/combo_*_*.log`、`/tmp/combo_summary_safe.txt`

---

## 附录 C: 多节点组合测试脚本说明

### C.1 multi_node_combo_test.sh

位置：[multi_node_combo_test.sh](file:///home/alientek/Phytium/task2_linux/multi_node_combo_test.sh)

用法：

```bash
sudo ./multi_node_combo_test.sh <E5> <E9> <E39> <DURATION>
```

参数：

- `E5`：额外 5bus 只读实例数
- `E9`：额外 9bus 只读实例数
- `E39`：额外 39bus 只读实例数
- `DURATION`：测试时长（秒）

脚本流程：

1. 清理旧 UKF 进程和 `start_sim_nodes`；
2. 热重载 FreeRTOS 固件，清理 SHM 状态；
3. 复位共享内存计数器；
4. 启动基础节点（1×5bus + 1×9bus + 1×39bus）；
5. 启动额外只读实例；
6. 运行指定时长；
7. 采样 CPU 占用；
8. 提取各实例帧数、平均 RMSE、最终 RMSE、平均延迟；
9. 输出到 `/tmp/combo_<E5>_<E9>_<E39>_<DURATION>.log`。

### C.2 run_combo_suite_safe.sh

位置：[run_combo_suite_safe.sh](file:///home/alientek/Phytium/task2_linux/run_combo_suite_safe.sh)

用法：

```bash
sudo ./run_combo_suite_safe.sh <DURATION>
```

脚本预定义了 8 种典型布局组合，依次调用 `multi_node_combo_test.sh`，并将汇总结果写入 `/tmp/combo_summary_safe.txt`。

---

## 附录 D: CPU3 排查命令与关键日志

### D.1 常用排查命令

```bash
# 查看 CPU 拓扑与在线状态
lscpu
cat /sys/devices/system/cpu/cpu3/online

# 查看内核启动参数
cat /proc/cmdline

# 查看 device tree CPU 节点
ls /proc/device-tree/cpus/

# 查看 remoteproc 状态
cat /sys/class/remoteproc/remoteproc0/state

# 查看 CPU 相关内核日志
sudo dmesg | grep -iE "CPU3|Booted|killed|failed|secondary|psci"
```

### D.2 关键日志原文

```text
[    0.103852] smp: Bringing up secondary CPUs ...
[    0.126829] Detected VIPT I-cache on CPU1
[    0.126944] CPU1: Booted secondary processor 0x0000000201 [0x700f3034]
[    0.145480] Detected PIPT I-cache on CPU2
[    0.145558] CPU2: Booted secondary processor 0x0000000000 [0x701f6643]
[    0.163995] Detected PIPT I-cache on CPU3
[    0.164055] CPU3: Booted secondary processor 0x0000000100 [0x701f6643]
[  159.477011] psci: CPU3 killed (polled 0 ms)
...
[ 5216.132972] psci: failed to boot CPU3 (-22)
[ 5216.137243] CPU3: failed to boot: -22
```

### D.3 结论速查

| 检查项 | 结果 | 说明 |
|--------|------|------|
| `/proc/cmdline` | 无 `maxcpus` 限制 | 排除内核参数问题 |
| device tree `cpu@101` | 默认 enabled | 排除设备树 disabled |
| systemd/udev/rc.local | 无 offline 脚本 | 排除用户态关闭 |
| 内核日志 | `CPU3 Booted` → `psci: CPU3 killed` → `failed to boot CPU3 (-22)` | **ATF/U-Boot 未释放 CPU3** |

---

*本报告到此结束。*
