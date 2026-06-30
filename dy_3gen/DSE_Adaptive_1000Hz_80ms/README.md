# DSE Adaptive Sampling — 1000Hz / 80ms

完全对应 MATLAB `Generate_ZData_Adaptive.m`。IEEE 9-Bus 3-Generator，1000Hz，80 步（60 正常 + 20 故障）。

---

## 目录结构

```
DSE_Adaptive_1000Hz_80ms/
│
├── README.md                本文件
│
│  === 终端生成 ===
├── terminal_node.py          数据生成（跑一次）
│
│  === 主控端 ===
├── controller_online.c       C 在线 UKF（流式逐条处理）
├── ukf_core.h                共享头文件
├── plot_online.py            出图脚本
│
│  === 生成的数据 ===
├── system_params.mat         Python 主控输入
├── system_params.bin         C 主控输入
├── measurements.txt          测量数据（80 行，4 位小数）
└── true_states.csv           真实状态（可选验证）
```

---

## 系统参数（与 MATLAB 一致）

| 参数 | 值 |
|------|-----|
| 发电机 | 3 台（Bus 1, 2, 3） |
| 采样率 | 1000 Hz |
| 步长 | 0.001 s |
| 正常步数 | 60（0 ~ 0.060s） |
| 故障步数 | 20（0.060s ~ 0.080s） |
| 故障 | Bus 8 三相短路, Line 8-9 跳闸 |
| sigma | 1e-2（所有状态统一） |
| 测量 | Vreal/Vimag（直角坐标），4 位小数 |

## MATLAB 对应关系

```
Generate_ZData_Adaptive.m    →   terminal_node.py
  runpf(case9_new_Sauer)     →   硬编码潮流解
  sig=1e-2                   →   sig=1e-2
  fs=1000, 60+20步            →   fs=1000, 60+20步
  round(z,4)                 →   np.round(Z_mes,4)
  zdata_adaptive.c/.h        →   measurements.txt
  DSE_Quantized_Measurement_Compare.m  →  controller_online.c + plot_online.py
```

---

## 快速开始

### 1. 终端侧生成数据（赛前一次）

```bash
python terminal_node.py
```

输出：`system_params.mat`, `system_params.bin`, `measurements.txt`, `true_states.csv`

### 2. 主控侧 C 版（Linux 嵌入式）

```bash
gcc -O2 -std=c99 -o controller_online controller_online.c -lm
./controller_online measurements.txt | python plot_online.py
```

### 3. 主控侧 Python 版（PC 离线）

```bash
python -c "
import numpy as np; from scipy.io import loadmat
# 加载参数 → 读取测量 → 运行 UKF → 画图
"
```

---

## 换测量数据

**只要故障场景不变（Bus 8, Line 8-9），只换 `measurements.txt` 即可**：

```bash
# 不同的噪声水平、不同的量测误差、不同的采样点...
./controller_online 另一份测量数据.txt | python plot_online.py
```

80 行、800 行、8000 行都行——`controller_online.c` 读到 EOF 自动停止。

---

## 编译要求

- GCC ≥ 4.2（C99 + `<complex.h>`）
- `-lm`（数学库）
- 零外部依赖
