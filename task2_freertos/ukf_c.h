/*
 * ukf_c.h — C语言 UKF 状态估计器 (5-Bus/2-Gen)
 * ===============================================
 * 与 Python ukf_estimation_5.py 逐行对应, 确保结果一致。
 * 双精度 (double) 保证精度匹配。
 */

#ifndef UKF_C_H
#define UKF_C_H

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── 系统维度 ─── */
#define UKF_N_GEN   2
#define UKF_N_BUS   5
#define UKF_NS      4     /* 状态维度: 2*N_GEN */
#define UKF_NM      14    /* 测量维度: 2*N_GEN + 2*N_BUS */
#define UKF_N_SIGMA 8     /* 2*NS = 8 sigma points */
#define UKF_DT      0.0005
#define UKF_FS      2000.0
#define UKF_SIGMA   1e-2

/* ─── 矩阵/向量类型 ─── */
typedef double Mat4x4[4][4];
typedef double Mat8x4[8][4];   /* sigma points: NS × (2*NS) */
typedef double Mat4x8[4][8];
typedef double Mat14x8[14][8];
typedef double Mat8x14[8][14];
typedef double Vec4[4];
typedef double Vec14[14];
typedef double Vec8[8];
typedef double Vec2[2];

/* ─── 复数 (实部/虚部分离) ─── */
typedef struct { double real[2][2], imag[2][2]; } CMat2x2;
typedef struct { double real[5][2], imag[5][2]; } CMat5x2;

/* ─── 系统参数 ─── */
typedef struct {
    CMat2x2 YBUS[3];     /* 降阶导纳矩阵 (pre/during/post) */
    CMat5x2 RV[3];       /* 电压恢复矩阵 */
    double E_abs[2];     /* 内电势幅值 */
    double PM[2];        /* 机械功率 */
    double M[2];         /* 惯性常数 */
    double D[2];         /* 阻尼系数 */
    double X0[4];        /* 初始状态 */
} UKFParams;

/* ─── UKF 状态 ─── */
typedef struct {
    double X_hat[4];      /* 状态估计 */
    double P[4][4];       /* 协方差矩阵 */
    double Q[4][4];       /* 过程噪声 */
    double R[14][14];     /* 测量噪声 */
    double W[8];          /* sigma 点权重 */
    int step_count;
    double current_t;
} UKFState;

/* ─── 线性代数 ─── */

/* C = A * B,  A: m×k, B: k×n, C: m×n */
void mat_mul(int m, int n, int k, const double *A, const double *B, double *C);

/* A = A + alpha * B * B^T  (rank-k update) */
void mat_syrk(int n, int k, double alpha, const double *B, double *A);

/* Cholesky 分解 A = L * L^T, A: n×n 正定对称, 返回 0=成功 */
int mat_cholesky(int n, const double *A, double *L);

/* 求解 L * L^T * x = b (L 是 Cholesky 下三角) */
void mat_cholesky_solve(int n, const double *L, const double *b, double *x);

/* 求解 A * X = B 返回 X (A: n×n, B: n×m), 内部用 Cholesky */
int mat_solve(int n, int m, const double *A, const double *B, double *X);

/* 矩阵转置 */
void mat_transpose(int m, int n, const double *A, double *AT);

/* ─── RK4 矢量传播 (与 Python RK4.py 的 rk4() 对应) ─── */
void ukf_rk4_propagate(const UKFParams *sp, int ps,
    const double X_sigma[8][4], double X_breve[8][4]);

/* ─── 测量函数 (与 Python h(x) 对应) ─── */
void ukf_measurement(const UKFParams *sp, int ps,
    const double X_sigma[8][4], double Z_breve[14][8]);

/* ─── UKF 初始化 ─── */
void ukf_init(UKFState *s, const UKFParams *sp);

/* ─── UKF 单步 ─── */
int ukf_step(UKFState *s, const UKFParams *sp, const double Z_meas[14], double t);

/* ─── 故障状态判断 ─── */
static inline int ukf_fault_state(double t) {
    if (t < 5.0) return 0;
    if (t <= 5.3) return 1;
    return 2;
}

#endif /* UKF_C_H */
