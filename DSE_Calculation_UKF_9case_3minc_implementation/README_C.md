================================================================
电力系统动态状态估计 - UKF算法 - C语言实现
IEEE 9节点3机系统
================================================================

目录结构：
----------
  ukf_core.h        - 公共头文件 (LAPACK封装、数据结构、常量)
  terminal_node.c   - 终端端：系统初始化 + 测量数据生成
  controller.c      - 主控端：UKF状态估计
  Makefile          - 编译脚本


一、依赖安装
------------

  【Linux (Ubuntu/Debian)】
  sudo apt install liblapack-dev libblas-dev build-essential

  【Linux (RHEL/CentOS/Fedora)】
  sudo dnf install lapack-devel blas-devel gcc

  【macOS (Homebrew)】
  brew install lapack openblas
  # 需修改 Makefile 中 LDFLAGS:
  # LDFLAGS = -L/opt/homebrew/opt/lapack/lib -L/opt/homebrew/opt/openblas/lib -llapack -lblas -lm

  【Windows (MSYS2)】
  pacman -S mingw-w64-x86_64-lapack mingw-w64-x86_64-openblas

  【Windows (WSL2)】
  sudo apt install liblapack-dev libblas-dev build-essential


二、编译
--------

  make all          # 编译 terminal_node 和 controller
  make terminal_node  # 仅编译终端端
  make controller     # 仅编译主控端


三、运行流程（比赛模式）
------------------------

  【步骤1：终端端生成测量数据】
  ./terminal_node
  输出：
    system_params.bin   - 系统参数（二进制格式）
    measurements.txt    - 24维测量向量 × 360,000行（4位小数）
    true_states.csv     - 6维真实状态（用于验证）

  【步骤2：传输文件到主控端】
  将 system_params.bin 和 measurements.txt 传输到主控端

  【步骤3：主控端执行UKF估计】
  ./controller
  输出：
    ukf_est.csv         - UKF估计状态（6维 × 360,000行）
    ukf_rmse.csv        - 每步RMSE
    ukf_compare.csv     - 真值 vs 估计对比（每100步采样，用于绘图）

  【一键运行】
  make run   # 先跑终端端，再跑主控端


四、数据格式说明
-----------------

  system_params.bin（二进制，Little-Endian）：
    [6 x int32]     n, s, ns, nm, fs, num_samples
    [4 x float64]   deltt, t_SW, t_FC, total_time
    [3*(n*n) x complex128]  YBUS[ps][n*n] for ps=0,1,2
    [3*(s*n) x complex128]  RV[ps][s*n]   for ps=0,1,2
    [n x float64]   E_abs
    [n x float64]   PM
    [n x float64]   M
    [n x float64]   D
    [ns x float64]  X_0

  measurements.txt（CSV，逗号分隔）：
    列头：timestamp,PG1,PG2,PG3,QG1,QG2,QG3,
         Vreal1..Vreal9, Vimag1..Vimag9
    数据：360,000行，每行24个测量值（4位小数）
    Z向量组成（24维）：
      Z[0:3]   = PG1~PG3       (发电机有功, pu)
      Z[3:6]   = QG1~QG3       (发电机无功, pu)
      Z[6:15]  = Re(V1)~Re(V9) (母线电压实部, pu)
      Z[15:24] = Im(V1)~Im(V9) (母线电压虚部, pu)
    注：使用复数电压实部/虚部，消除angle()相位卷绕问题


五、与MATLAB/Python版本的对应关系
---------------------------------

  MATLAB函数                C实现
  -----------               ------
  chol(ns*P, 'lower')    → dpotrf_('L', ...) (LAPACK)
  eig(ns*P)              → dsyev_('V','L', ...) (LAPACK)
  inv(matrix)            → dgetrf_ + dgetri_ / zgetrf_ + zgetri_
  Pxz/Pz                 → dgemm_(Pxz * inv(Pz))
  ode45 / rk4_step       → rk4_step() (hand-written RK4)
  repmat + array ops     → explicit loops
  try-catch regularization → if (chol fails) { P += 1e-8; retry; }

  关键算法修正（C版本同步实现）：
  1. 矩阵平方根方向: root = L (下三角Cholesky, L*L'=ns*P)
  2. 复数电压测量: Re(V) + Im(V) 替代 |V| + ∠V
  3. Cholesky try-catch + P正则化
  4. Pz求逆 try-catch + 正则化
  5. 故障RV行映射修正 (setdiff)
  6. 分离Q矩阵 (sig_angle=0.01, sig_speed=0.03)
  7. P对称化强制


六、系统参数
------------

  系统        : IEEE 9节点 (Peter Sauer / Chow)
  发电机      : 3台 (Bus 1, 2, 3)
  母线        : 9条
  支路        : 9条
  基准功率    : 100 MVA
  额定频率    : 60 Hz
  仿真时长    : 180秒 (3分钟)
  采样频率    : 2000 Hz
  时间步长    : 0.0005 秒
  总采样点    : 360,000
  状态维度    : 6 (3δ + 3ω)
  测量维度    : 24 (3PG + 3QG + 9Re(V) + 9Im(V))

  发电机参数：
  Xd = [0.06080, 0.11980, 0.18130] pu
  H  = [23.64,   6.40,    3.01  ] s
  D  = [0.0255,  0.00663, 0.00265] (非零阻尼！)
  R  = [0, 0, 0] pu

  故障：
  母线8三相短路 (5.0s - 5.3s)，切除线路8-9


七、结果绘图（用Python辅助）
----------------------------

  C程序输出CSV文件，可用以下Python脚本快速绘图：

  ```python
  import numpy as np
  import matplotlib.pyplot as plt

  data = np.loadtxt('ukf_compare.csv', delimiter=',', skiprows=1)
  t = data[:, 0]

  fig, axes = plt.subplots(2, 3, figsize=(15, 8))
  for i in range(3):
      axes[0, i].plot(t, data[:, 1+2*i], 'b', label='True')
      axes[0, i].plot(t, data[:, 2+2*i], 'r--', label='UKF')
      axes[0, i].set_title(f'Angle Gen {i+1}')
      axes[0, i].legend()
      axes[1, i].plot(t, data[:, 7+2*i], 'b', label='True')
      axes[1, i].plot(t, data[:, 8+2*i], 'r--', label='UKF')
      axes[1, i].set_title(f'Speed Gen {i+1}')
      axes[1, i].legend()
  plt.tight_layout()
  plt.savefig('ukf_results_c.png', dpi=150)
  ```


八、性能说明
------------

  C版本（LAPACK）:
    terminal_node: ~2-5分钟 (360,000次RK4 + 文件写入)
    controller:    ~5-15分钟 (360,000次UKF迭代 + LAPACK调用)

  性能优化建议：
  - 使用 -O3 -march=native 编译
  - 链接优化的BLAS (OpenBLAS / MKL)
  - 减少文件I/O频率，使用缓冲写入


九、文件清单
------------
  ukf_core.h          - 公共头文件
  terminal_node.c     - 终端端主程序
  controller.c        - 主控端主程序
  Makefile            - 编译脚本
  README_C.md         - 本文档
================================================================
