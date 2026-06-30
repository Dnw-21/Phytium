/*
 * ukf_core.h — 泛化 UKF 状态估计器 (支持 N-generator 系统)
 * ============================================================
 * 最大维度: 10-gen/39-bus (ns=20, nm=98)
 * 通过参数 n_gen/n_bus 切换 2-gen 和 10-gen 模式
 */

#ifndef UKF_CORE_H
#define UKF_CORE_H

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define UKF_MAX_GEN  10
#define UKF_MAX_BUS  39
#define UKF_MAX_NS   (2 * UKF_MAX_GEN)   /* 20 */
#define UKF_MAX_NM   (2 * UKF_MAX_GEN + 2 * UKF_MAX_BUS) /* 98 */
#define UKF_MAX_SIGMA (2 * UKF_MAX_NS)    /* 40 */

/* ─── 系统参数 ─── */
typedef struct {
    int n_gen, n_bus, ns, nm, np;  /* np = 2*ns */
    double dt, fs, fault_start, fault_end, sigma;

    /* YBUS: [n_gen × n_gen × 3] (pre/during/post fault) */
    double YBUS_real[UKF_MAX_GEN][UKF_MAX_GEN][3];
    double YBUS_imag[UKF_MAX_GEN][UKF_MAX_GEN][3];

    /* RV: [n_bus × n_gen × 3] */
    double RV_real[UKF_MAX_BUS][UKF_MAX_GEN][3];
    double RV_imag[UKF_MAX_BUS][UKF_MAX_GEN][3];

    double E_abs[UKF_MAX_GEN];
    double PM[UKF_MAX_GEN];
    double M[UKF_MAX_GEN];
    double D[UKF_MAX_GEN];
    double X0[UKF_MAX_NS];
    int voltage_format;  /* 0=Vmag+Vangle(5bus/39bus), 1=Vreal+Vimag(9bus) */
} UKFParams;

/* ─── UKF 运行时状态 ─── */
typedef struct {
    int ns, nm, np, n_gen, n_bus;
    double X_hat[UKF_MAX_NS];
    double P[UKF_MAX_NS][UKF_MAX_NS];
    double W[UKF_MAX_SIGMA];
    int step_count;
    double current_t;
} UKFState;

/* ─── 线性代数 ─── */
void mat_mul(int m, int n, int k, const double *A, const double *B, double *C);
void mat_transpose(int m, int n, const double *A, double *AT);
int  mat_cholesky(int n, const double *A, double *L);
void mat_cholesky_solve(int n, const double *L, const double *b, double *x);
int  mat_solve(int n, int m, const double *A, const double *B, double *X);

/* ─── UKF 核心 ─── */
void ukf_init(UKFState *s, const UKFParams *sp);
int  ukf_step(UKFState *s, const UKFParams *sp, const double *Z_meas, double t);

/* ─── 辅助 ─── */
static inline int ukf_fault_state(const UKFParams *sp, double t) {
    if (t < sp->fault_start) return 0;
    if (t <= sp->fault_end) return 1;
    return 2;
}

#endif
