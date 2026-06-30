# DSE Z2 — Bus 4 故障 / Line 4-5 跳闸

对应 MATLAB `Generate_Z2Data_Adaptive.m`。与 Z1 版本唯一区别：**故障位置**。

| | Z1 (Generate_ZData_Adaptive.m) | Z2 (Generate_Z2Data_Adaptive.m) |
|---|---|---|
| 故障母线 | Bus 8 | **Bus 4** |
| 跳闸线路 | Line 8-9 | **Line 4-5** |
| 采样率 | 1000 Hz | 1000 Hz |
| 步数 | 80 | 80 |
| 故障时间 | 0.060s ~ 0.080s | 0.060s ~ 0.080s |

## 快速使用

```bash
python terminal_node.py                         # 生成数据
gcc -O2 -std=c99 -o controller_online controller_online.c -lm
./controller_online measurements.txt | python plot_online.py
```

## 如果要同时测多种故障

```bash
# 三个目录各自生成各自的 system_params.bin + measurements.txt
../DSE_Adaptive_1000Hz_80ms/        # Z1: Bus 8, Line 8-9
../DSE_Z2_Adaptive_1000Hz_80ms/     # Z2: Bus 4, Line 4-5
../DSE_Z3_Adaptive_1000Hz_80ms/     # Z3: Bus 3, Line 3-9 (Generate_Z3Data_Adaptive.m)

# 主控端只换两个文件就能跑不同场景
cp 某目录/system_params.bin ./
cp 某目录/measurements.txt ./
./controller_online measurements.txt | python plot_online.py
```
