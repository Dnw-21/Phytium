/*
 * ukf_online_core.h — 在线 UKF 状态估计核心
 * ==========================================================
 * 通过编译期节点专用化生成 5bus/9bus/39bus 三个独立 UKF。
 *
 * 编译示例:
 *   gcc -DUKF_NODE_5BUS  -o ukf_pipeline_5bus  ukf_pipeline_online.c -lm
 *   gcc -DUKF_NODE_9BUS  -o ukf_pipeline_9bus  ukf_pipeline_online.c -lm
 *   gcc -DUKF_NODE_39BUS -o ukf_pipeline_39bus ukf_pipeline_online.c -lm
 *
 * 核心设计:
 *   1. 分离过程噪声 Q 与测量噪声 R，分别设置 sigma
 *   2. 矩阵求逆计算 Kalman 增益
 *   3. 每步对称化误差协方差 P = (P+P')/2
 *   4. 所有临时矩阵静态分配，零 malloc/free
 * ==========================================================
 */

#ifndef UKF_ONLINE_CORE_H
#define UKF_ONLINE_CORE_H

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef UKF_USE_FT
#include <cblas.h>
#include <lapacke.h>
#endif

/* ──────────────────────────────────────────────────────────────
 * Node selection
 * ────────────────────────────────────────────────────────────── */
#if defined(UKF_NODE_5BUS)
  #define UKF_N_GEN   2
  #define UKF_N_BUS   5
  #define UKF_NS      4
  #define UKF_NM      14
  #define UKF_N_SIGMA 8
  #define UKF_VFMT    0    /* Vmag + Vangle */
  #define UKF_SIG_A   0.01
  #define UKF_SIG_W   0.03
  #define UKF_SIG_M   0.01
  #define UKF_NODE_NAME "5bus"
#elif defined(UKF_NODE_39BUS)
  #define UKF_N_GEN   10
  #define UKF_N_BUS   39
  #define UKF_NS      20
  #define UKF_NM      98
  #define UKF_N_SIGMA 40
  #define UKF_VFMT    0    /* Vmag + Vangle */
  #define UKF_SIG_A   0.01
  #define UKF_SIG_W   0.03
  #define UKF_SIG_M   0.01
  #define UKF_NODE_NAME "39bus"
#elif defined(UKF_NODE_9BUS)
  #define UKF_N_GEN   3
  #define UKF_N_BUS   9
  #define UKF_NS      6
  #define UKF_NM      24
  #define UKF_N_SIGMA 12
  #define UKF_VFMT    1    /* Vreal + Vimag */
  #define UKF_SIG_A   0.01
  #define UKF_SIG_W   0.03
  #define UKF_SIG_M   0.01
  #define UKF_NODE_NAME "9bus"
#else
  #error "Must define UKF_NODE_5BUS, UKF_NODE_39BUS, or UKF_NODE_9BUS"
#endif

#define UKF_MAX_NS   UKF_NS
#define UKF_MAX_NM   UKF_NM
#define UKF_MAX_GEN  UKF_N_GEN
#define UKF_MAX_BUS  UKF_N_BUS
#define UKF_MAX_SIG  UKF_N_SIGMA

/* ──────────────────────────────────────────────────────────────
 * System parameters (compile-time specialized)
 * ────────────────────────────────────────────────────────────── */
typedef struct {
    double YBUS_real[UKF_MAX_GEN][UKF_MAX_GEN][3];
    double YBUS_imag[UKF_MAX_GEN][UKF_MAX_GEN][3];
    double RV_real[UKF_MAX_BUS][UKF_MAX_GEN][3];
    double RV_imag[UKF_MAX_BUS][UKF_MAX_GEN][3];
    double E_abs[UKF_MAX_GEN];
    double PM[UKF_MAX_GEN];
    double M[UKF_MAX_GEN];
    double D[UKF_MAX_GEN];
    double X_0[UKF_MAX_NS];
    double deltt, fs, fault_start, fault_end;
    int n_gen, n_bus, ns, nm, np, voltage_format;
} UKFParams;

/* ──────────────────────────────────────────────────────────────
 * Persistent UKF state (retained between ukf_step calls)
 * ────────────────────────────────────────────────────────────── */
typedef struct {
    double P[UKF_MAX_NS * UKF_MAX_NS];
    double X_hat[UKF_MAX_NS];
    double Q_mat[UKF_MAX_NS * UKF_MAX_NS];
    double R_meas[UKF_MAX_NM * UKF_MAX_NM];
    double W[UKF_MAX_SIG];
    int initialized;

    /* Cached params (read-only after init) */
    int n, s, ns, nm;
    double E_abs[UKF_MAX_GEN];
    double PM[UKF_MAX_GEN];
    double M[UKF_MAX_GEN];
    double D[UKF_MAX_GEN];
    const double (*YBUS_real)[UKF_MAX_GEN][3];
    const double (*YBUS_imag)[UKF_MAX_GEN][3];
    const double (*RV_real)[UKF_MAX_GEN][3];
    const double (*RV_imag)[UKF_MAX_GEN][3];
    double deltt, t_SW, t_FC;
} UKFState;

/* ──────────────────────────────────────────────────────────────
 * Linear algebra (pure C99, static allocation)
 * ────────────────────────────────────────────────────────────── */

/** Cholesky decomposition L*L' = A (lower triangular).
 *  Returns 0 on success, -1 if not positive-definite. */
#ifdef UKF_USE_FT
static int chol_lower(int n, double *A) {
    lapack_int info = LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', n, A, n);
    if (info != 0) return -1;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            A[i * n + j] = 0.0;
    return 0;
}
#else
static int chol_lower(int n, double *A) {
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
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            A[i * n + j] = 0.0;
    return 0;
}
#endif

/** Forward substitution L*x = b. L is lower-triangular. */
static void forward_sub(int n, const double *L, const double *b, double *x) {
    for (int i = 0; i < n; i++) {
        double s = b[i];
        for (int j = 0; j < i; j++)
            s -= L[i * n + j] * x[j];
        x[i] = s / L[i * n + i];
    }
}

/** Backward substitution L'*x = b. L is lower-triangular (use its transpose). */
static void back_sub(int n, const double *L, const double *b, double *x) {
    for (int i = n - 1; i >= 0; i--) {
        double s = b[i];
        for (int j = i + 1; j < n; j++)
            s -= L[j * n + i] * x[j];
        x[i] = s / L[i * n + i];
    }
}

/** 实矩阵求逆 (Gauss-Jordan), 用于 Cholesky 失败后的 fallback.
 *  A 被替换为 A^{-1}. 返回 0 成功, -1 失败. */
#ifdef UKF_USE_FT
static int mat_inv_gauss_jordan(int n, double *A) {
    lapack_int ipiv[UKF_MAX_NM];
    lapack_int info = LAPACKE_dgetrf(LAPACK_ROW_MAJOR, n, n, A, n, ipiv);
    if (info != 0) return -1;
    info = LAPACKE_dgetri(LAPACK_ROW_MAJOR, n, A, n, ipiv);
    return (info != 0) ? -1 : 0;
}
#else
static int mat_inv_gauss_jordan(int n, double *A) {
    double B[UKF_MAX_NM * UKF_MAX_NM];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            B[i * n + j] = (i == j) ? 1.0 : 0.0;
    for (int col = 0; col < n; col++) {
        int max_row = col;
        double max_val = fabs(A[col * n + col]);
        for (int row = col + 1; row < n; row++)
            if (fabs(A[row * n + col]) > max_val) {
                max_val = fabs(A[row * n + col]);
                max_row = row;
            }
        if (max_val < 1e-15) return -1;
        if (max_row != col) {
            for (int j = 0; j < n; j++) {
                double tmp = A[col * n + j]; A[col * n + j] = A[max_row * n + j]; A[max_row * n + j] = tmp;
                tmp = B[col * n + j]; B[col * n + j] = B[max_row * n + j]; B[max_row * n + j] = tmp;
            }
        }
        double piv = A[col * n + col];
        for (int j = 0; j < n; j++) { A[col * n + j] /= piv; B[col * n + j] /= piv; }
        for (int row = 0; row < n; row++) {
            if (row == col) continue;
            double fac = A[row * n + col];
            for (int j = 0; j < n; j++) {
                A[row * n + j] -= fac * A[col * n + j];
                B[row * n + j] -= fac * B[col * n + j];
            }
        }
    }
    memcpy(A, B, n * n * sizeof(double));
    return 0;
}
#endif

/** Kalman增益: K = Pxz * inv(Pz), 用Cholesky求解 (比矩阵求逆更稳定)
 *  解 L*L'*K_row' = Pxz_row' via forward/back substitution.
 *  若 Cholesky 失败则 fallback 到 Gauss-Jordan 求逆.
 *  返回 0 成功, -1 失败 */
static int kalman_gain_cholesky(int ns, int nm,
    const double *Pz, const double *Pxz, double *K) {
    double L[UKF_MAX_NM * UKF_MAX_NM];
    memcpy(L, Pz, nm * nm * sizeof(double));

    /* 数值稳定性: 对角加微量扰动 */
    for (int i = 0; i < nm; i++) L[i * nm + i] += 1e-12;

    if (chol_lower(nm, L) != 0) {
        /* 重试: 增大正则化 */
        memcpy(L, Pz, nm * nm * sizeof(double));
        for (int i = 0; i < nm; i++) L[i * nm + i] += 1e-8;
        if (chol_lower(nm, L) != 0) {
            /* Fallback: Gauss-Jordan 求逆 (与原始 39bus C 代码一致) */
            double Pz_inv[UKF_MAX_NM * UKF_MAX_NM];
            memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
            for (int i = 0; i < nm; i++) Pz_inv[i * nm + i] += 1e-8;
            if (mat_inv_gauss_jordan(nm, Pz_inv) != 0) return -1;
            /* K = Pxz * Pz_inv */
            memset(K, 0, ns * nm * sizeof(double));
            for (int i = 0; i < ns; i++)
                for (int j = 0; j < nm; j++)
                    for (int k = 0; k < nm; k++)
                        K[i * nm + j] += Pxz[i * nm + k] * Pz_inv[k * nm + j];
            return 0;
        }
    }

    /* 逐行求解: L*L'*K_row' = Pxz_row' */
    for (int i = 0; i < ns; i++) {
        double y[UKF_MAX_NM], kt[UKF_MAX_NM];
        forward_sub(nm, L, &Pxz[i * nm], y);
        back_sub(nm, L, y, kt);
        for (int j = 0; j < nm; j++) K[i * nm + j] = kt[j];
    }
    return 0;
}

/* C(m,n) = A(m,k) * B(k,n) — real */
#ifdef UKF_USE_FT
static void mmul(int m, int n, int k, const double *A, const double *B, double *C) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                m, n, k, 1.0, A, k, B, n, 0.0, C, n);
}
#else
static void mmul(int m, int n, int k, const double *A, const double *B, double *C) {
    memset(C, 0, m * n * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int l = 0; l < k; l++)
            for (int j = 0; j < n; j++)
                C[i * n + j] += A[i * k + l] * B[l * n + j];
}
#endif

/* C(m,n) = A(m,k) * B'(n,k) i.e. B transposed */
#ifdef UKF_USE_FT
static void mmul_bt(int m, int n, int k, const double *A, const double *B, double *C) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                m, n, k, 1.0, A, k, B, k, 0.0, C, n);
}
#else
static void mmul_bt(int m, int n, int k, const double *A, const double *B, double *C) {
    memset(C, 0, m * n * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            for (int l = 0; l < k; l++)
                C[i * n + j] += A[i * k + l] * B[j * k + l];
}
#endif

/* ──────────────────────────────────────────────────────────────
 * Complex matrix-vector multiply helpers
 * ────────────────────────────────────────────────────────────── */

/* YBUS(n×n) @ E(n×1): I = Y*E, complex */
static void cmv_ybus(int ng, int ps,
    const double Yr[UKF_MAX_GEN][UKF_MAX_GEN][3],
    const double Yi[UKF_MAX_GEN][UKF_MAX_GEN][3],
    const double *Er, const double *Ei, double *Ir, double *Ii) {
    for (int i = 0; i < ng; i++) {
        Ir[i] = Ii[i] = 0;
        for (int j = 0; j < ng; j++) {
            double a = Yr[i][j][ps], b = Yi[i][j][ps];
            double c = Er[j], d = Ei[j];
            Ir[i] += a*c - b*d;
            Ii[i] += a*d + b*c;
        }
    }
}

/* RV(nb×ng) @ E(ng×1): V = RV*E, complex */
static void cmv_rv(int nb, int ng, int ps,
    const double Rr[UKF_MAX_BUS][UKF_MAX_GEN][3],
    const double Ri[UKF_MAX_BUS][UKF_MAX_GEN][3],
    const double *Er, const double *Ei, double *Vr, double *Vi) {
    for (int i = 0; i < nb; i++) {
        Vr[i] = Vi[i] = 0;
        for (int j = 0; j < ng; j++) {
            double a = Rr[i][j][ps], b = Ri[i][j][ps];
            double c = Er[j], d = Ei[j];
            Vr[i] += a*c - b*d;
            Vi[i] += a*d + b*c;
        }
    }
}

/* ──────────────────────────────────────────────────────────────
 * Vectorized RK4 sigma point propagation
 * ────────────────────────────────────────────────────────────── */
static void rk4_sigma(const UKFParams *sp, int ps,
    const double *X_sigma, double *xbreve, int n_sigma) {
    int ng = sp->n_gen, ns = sp->ns;
    double dt = sp->deltt;

    /* Stage 1 */
    double k1_delta[UKF_MAX_SIG][UKF_MAX_GEN], k1_w[UKF_MAX_SIG][UKF_MAX_GEN];
    for (int si = 0; si < n_sigma; si++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[si * ns + i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[si * ns + i]);
        }
        cmv_ybus(ng, ps, sp->YBUS_real, sp->YBUS_imag, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            k1_w[si][i]     = dt * (sp->PM[i] - PG - sp->D[i] * X_sigma[si*ns + ng + i]) / sp->M[i];
            k1_delta[si][i] = dt * X_sigma[si*ns + ng + i];
        }
    }

    /* Stage 2 */
    double k2_delta[UKF_MAX_SIG][UKF_MAX_GEN], k2_w[UKF_MAX_SIG][UKF_MAX_GEN];
    for (int si = 0; si < n_sigma; si++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[si*ns + i] + 0.5*k1_delta[si][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[si*ns + i] + 0.5*k1_delta[si][i]);
        }
        cmv_ybus(ng, ps, sp->YBUS_real, sp->YBUS_imag, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            double w2 = X_sigma[si*ns + ng + i] + 0.5*k1_w[si][i];
            k2_w[si][i]     = dt * (sp->PM[i] - PG - sp->D[i]*w2) / sp->M[i];
            k2_delta[si][i] = dt * w2;
        }
    }

    /* Stage 3 */
    double k3_delta[UKF_MAX_SIG][UKF_MAX_GEN], k3_w[UKF_MAX_SIG][UKF_MAX_GEN];
    for (int si = 0; si < n_sigma; si++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[si*ns + i] + 0.5*k2_delta[si][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[si*ns + i] + 0.5*k2_delta[si][i]);
        }
        cmv_ybus(ng, ps, sp->YBUS_real, sp->YBUS_imag, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            double w3 = X_sigma[si*ns + ng + i] + 0.5*k2_w[si][i];
            k3_w[si][i]     = dt * (sp->PM[i] - PG - sp->D[i]*w3) / sp->M[i];
            k3_delta[si][i] = dt * w3;
        }
    }

    /* Stage 4 + final */
    double k4_delta[UKF_MAX_SIG][UKF_MAX_GEN], k4_w[UKF_MAX_SIG][UKF_MAX_GEN];
    for (int si = 0; si < n_sigma; si++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[si*ns + i] + k3_delta[si][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[si*ns + i] + k3_delta[si][i]);
        }
        cmv_ybus(ng, ps, sp->YBUS_real, sp->YBUS_imag, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            double w4 = X_sigma[si*ns + ng + i] + k3_w[si][i];
            k4_w[si][i]     = dt * (sp->PM[i] - PG - sp->D[i]*w4) / sp->M[i];
            k4_delta[si][i] = dt * w4;
        }

        /* Assemble */
        for (int i = 0; i < ng; i++) {
            xbreve[si*ns + i] = X_sigma[si*ns + i]
                + (k1_delta[si][i] + 2*k2_delta[si][i] + 2*k3_delta[si][i] + k4_delta[si][i]) / 6.0;
        }
        for (int i = 0; i < ng; i++) {
            xbreve[si*ns + ng + i] = X_sigma[si*ns + ng + i]
                + (k1_w[si][i] + 2*k2_w[si][i] + 2*k3_w[si][i] + k4_w[si][i]) / 6.0;
        }
    }
}

/* ──────────────────────────────────────────────────────────────
 * Measurement prediction: X_sigma → Z_breve
 * ────────────────────────────────────────────────────────────── */
static void meas_predict(const UKFParams *sp, int ps,
    const double *X_sigma, double *Z_breve, int n_sigma) {
    int ng = sp->n_gen, nb = sp->n_bus, ns = sp->ns, nm = sp->nm;

    for (int si = 0; si < n_sigma; si++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        double Vr[UKF_MAX_BUS], Vi[UKF_MAX_BUS];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[si*ns + i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[si*ns + i]);
        }
        cmv_ybus(ng, ps, sp->YBUS_real, sp->YBUS_imag, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            Z_breve[si*nm + i]        = Er[i]*Ir[i] + Ei[i]*Ii[i];  /* PG */
            Z_breve[si*nm + ng + i]   = Ei[i]*Ir[i] - Er[i]*Ii[i];  /* QG */
        }
        cmv_rv(nb, ng, ps, sp->RV_real, sp->RV_imag, Er, Ei, Vr, Vi);
        if (sp->voltage_format == 1) {
            /* Vreal + Vimag */
            for (int i = 0; i < nb; i++) {
                Z_breve[si*nm + 2*ng + i]     = Vr[i];
                Z_breve[si*nm + 2*ng + nb + i] = Vi[i];
            }
        } else {
            /* Vmag + Vangle */
            for (int i = 0; i < nb; i++) {
                Z_breve[si*nm + 2*ng + i]      = sqrt(Vr[i]*Vr[i] + Vi[i]*Vi[i]);
                Z_breve[si*nm + 2*ng + nb + i]  = atan2(Vi[i], Vr[i]);
            }
        }
    }
}

/* ──────────────────────────────────────────────────────────────
 * Topology selector (fault state)
 * ────────────────────────────────────────────────────────────── */
static inline int get_ps(double t, double t_SW, double t_FC) {
    if (t < t_SW) return 0;
    if (t <= t_FC) return 1;
    return 2;
}

/* ──────────────────────────────────────────────────────────────
 * ukf_init — Initialize persistent UKF state (call once)
 * ────────────────────────────────────────────────────────────── */
static void ukf_init(UKFState *st, const UKFParams *sp) {
    memset(st, 0, sizeof(*st));
    int n = sp->n_gen, s = sp->n_bus, ns = sp->ns, nm = sp->nm, n_sigma = sp->np;

    st->n = n; st->s = s; st->ns = ns; st->nm = nm;
    st->deltt = sp->deltt; st->t_SW = sp->fault_start; st->t_FC = sp->fault_end;

    memcpy(st->E_abs, sp->E_abs, n * sizeof(double));
    memcpy(st->PM, sp->PM, n * sizeof(double));
    memcpy(st->M, sp->M, n * sizeof(double));
    memcpy(st->D, sp->D, n * sizeof(double));
    st->YBUS_real = sp->YBUS_real;
    st->YBUS_imag = sp->YBUS_imag;
    st->RV_real   = sp->RV_real;
    st->RV_imag   = sp->RV_imag;

    /* P — separate angle/speed sigmas */
    memset(st->P, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->P[i * ns + i]         = UKF_SIG_A * UKF_SIG_A;
        st->P[(n + i) * ns + (n + i)] = UKF_SIG_W * UKF_SIG_W;
    }

    /* Q — same structure as P */
    memset(st->Q_mat, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->Q_mat[i * ns + i]         = UKF_SIG_A * UKF_SIG_A;
        st->Q_mat[(n + i) * ns + (n + i)] = UKF_SIG_W * UKF_SIG_W;
    }

    /* R — measurement noise (diagonal) */
    memset(st->R_meas, 0, nm * nm * sizeof(double));
    for (int i = 0; i < nm; i++)
        st->R_meas[i * nm + i] = UKF_SIG_M * UKF_SIG_M;

    /* Weights */
    double w = 1.0 / (2.0 * ns);
    for (int i = 0; i < n_sigma; i++) st->W[i] = w;

    /* Initial state */
    memcpy(st->X_hat, sp->X_0, ns * sizeof(double));

    st->initialized = 1;
}

/* ──────────────────────────────────────────────────────────────
 * ukf_step — Process ONE measurement vector (returns RMSE)
 *
 * Parameters:
 *   st      — persistent UKF state (updated in-place)
 *   sp      — system params (read-only)
 *   z_k     — measurement vector [nm doubles]
 *   k_time  — current time in seconds
 *   x_out   — output: estimated state [ns doubles] (may be NULL)
 *   rmse_out— output: sqrt(trace(P)) (may be NULL)
 *
 * Returns: 0 on success, -1 on error
 * ────────────────────────────────────────────────────────────── */
static int ukf_step(UKFState *st, const UKFParams *sp,
                    const double *z_k, double k_time,
                    double *x_out, double *rmse_out) {
    if (!st->initialized) return -1;

    int ns = st->ns, nm = st->nm, n_sigma = 2 * ns;
    double w_val = st->W[0];
    int ps = get_ps(k_time, st->t_SW, st->t_FC);

    /* ---- Sigma points from P (prediction) ---- */
    double P_scaled[UKF_MAX_NS * UKF_MAX_NS];
    for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];

    double L[UKF_MAX_NS * UKF_MAX_NS];
    memcpy(L, P_scaled, ns * ns * sizeof(double));
    if (chol_lower(ns, L) != 0) {
        for (int i = 0; i < ns; i++) st->P[i * ns + i] += 1e-8;
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
        memcpy(L, P_scaled, ns * ns * sizeof(double));
        if (chol_lower(ns, L) != 0) {
            memset(L, 0, ns * ns * sizeof(double));
            for (int i = 0; i < ns; i++) L[i * ns + i] = 1e-6;
        }
    }

    double X_sigma[UKF_MAX_SIG * UKF_MAX_NS];
    for (int si = 0; si < ns; si++)
        for (int ii = 0; ii < ns; ii++) {
            X_sigma[si * ns + ii]             = st->X_hat[ii] + L[ii * ns + si];
            X_sigma[(si + ns) * ns + ii]      = st->X_hat[ii] - L[ii * ns + si];
        }

    /* ---- Predict (RK4) ---- */
    double xbreve[UKF_MAX_SIG * UKF_MAX_NS];
    rk4_sigma(sp, ps, X_sigma, xbreve, n_sigma);

    /* Weighted mean */
    double X_pred[UKF_MAX_NS];
    memset(X_pred, 0, ns * sizeof(double));
    for (int si = 0; si < n_sigma; si++)
        for (int ii = 0; ii < ns; ii++)
            X_pred[ii] += w_val * xbreve[si * ns + ii];

    /* Predicted covariance */
    double P_pred[UKF_MAX_NS * UKF_MAX_NS];
    memset(P_pred, 0, ns * ns * sizeof(double));
    for (int si = 0; si < n_sigma; si++) {
        double dev[UKF_MAX_NS];
        for (int ii = 0; ii < ns; ii++) dev[ii] = xbreve[si * ns + ii] - X_pred[ii];
        for (int ii = 0; ii < ns; ii++)
            for (int jj = 0; jj < ns; jj++)
                P_pred[ii * ns + jj] += w_val * dev[ii] * dev[jj];
    }

    /* P = P_pred + Q, then symmetrize */
    for (int i = 0; i < ns * ns; i++) st->P[i] = P_pred[i] + st->Q_mat[i];
    for (int i = 0; i < ns; i++)
        for (int j = i + 1; j < ns; j++) {
            double avg = (st->P[i*ns + j] + st->P[j*ns + i]) / 2.0;
            st->P[i*ns + j] = st->P[j*ns + i] = avg;
        }

    /* ---- Update: new sigma points ---- */
    for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
    memcpy(L, P_scaled, ns * ns * sizeof(double));
    if (chol_lower(ns, L) != 0) {
        for (int i = 0; i < ns; i++) st->P[i * ns + i] += 1e-8;
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
        memcpy(L, P_scaled, ns * ns * sizeof(double));
        if (chol_lower(ns, L) != 0) {
            memset(L, 0, ns * ns * sizeof(double));
            for (int i = 0; i < ns; i++) L[i * ns + i] = 1e-6;
        }
    }

    double X_sigma2[UKF_MAX_SIG * UKF_MAX_NS];
    for (int si = 0; si < ns; si++)
        for (int ii = 0; ii < ns; ii++) {
            X_sigma2[si * ns + ii]         = X_pred[ii] + L[ii * ns + si];
            X_sigma2[(si + ns) * ns + ii]  = X_pred[ii] - L[ii * ns + si];
        }

    /* ---- Measurement prediction ---- */
    double Z_breve[UKF_MAX_SIG * UKF_MAX_NM];
    meas_predict(sp, ps, X_sigma2, Z_breve, n_sigma);

    double zhat[UKF_MAX_NM];
    memset(zhat, 0, nm * sizeof(double));
    for (int si = 0; si < n_sigma; si++)
        for (int ii = 0; ii < nm; ii++)
            zhat[ii] += w_val * Z_breve[si * nm + ii];

    /* ---- Pz, Pxz ---- */
    double Pz[UKF_MAX_NM * UKF_MAX_NM], Pxz[UKF_MAX_NS * UKF_MAX_NM];
    memset(Pz,  0, nm * nm * sizeof(double));
    memset(Pxz, 0, ns * nm * sizeof(double));

    for (int si = 0; si < n_sigma; si++) {
        double dz[UKF_MAX_NM], dx[UKF_MAX_NS];
        for (int ii = 0; ii < nm; ii++) dz[ii] = Z_breve[si * nm + ii] - zhat[ii];
        for (int ii = 0; ii < ns; ii++) dx[ii] = X_sigma2[si * ns + ii] - X_pred[ii];
        for (int ii = 0; ii < nm; ii++)
            for (int jj = 0; jj < nm; jj++)
                Pz[ii * nm + jj] += w_val * dz[ii] * dz[jj];
        for (int ii = 0; ii < ns; ii++)
            for (int jj = 0; jj < nm; jj++)
                Pxz[ii * nm + jj] += w_val * dx[ii] * dz[jj];
    }
    for (int i = 0; i < nm * nm; i++) Pz[i] += st->R_meas[i];

    /* 对称化 Pz (消除浮点误差, 保证 Cholesky 输入严格对称) */
    for (int i = 0; i < nm; i++)
        for (int j = i + 1; j < nm; j++) {
            double avg = (Pz[i * nm + j] + Pz[j * nm + i]) / 2.0;
            Pz[i * nm + j] = Pz[j * nm + i] = avg;
        }

    /* ---- Kalman gain K = Pxz * inv(Pz) (Cholesky求解) ---- */
    double K[UKF_MAX_NS * UKF_MAX_NM];
    int kg_ret = kalman_gain_cholesky(ns, nm, Pz, Pxz, K);
    if (kg_ret != 0) {
        /* 极端情况下用对角近似: K[i][j] = Pxz[i][j] / Pz[j][j] */
        memset(K, 0, ns * nm * sizeof(double));
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < nm; j++)
                K[i * nm + j] = Pxz[i * nm + j] / (Pz[j * nm + j] + 1e-6);
    }

    /* ---- State update: X = X_pred + K*(z - zhat) ---- */
    double innov[UKF_MAX_NM], dx_upd[UKF_MAX_NS];
    for (int ii = 0; ii < nm; ii++) innov[ii] = z_k[ii] - zhat[ii];
#ifdef UKF_USE_FT
    cblas_dgemv(CblasRowMajor, CblasNoTrans, ns, nm, 1.0, K, nm, innov, 1, 0.0, dx_upd, 1);
#else
    memset(dx_upd, 0, ns * sizeof(double));
    for (int ii = 0; ii < ns; ii++)
        for (int jj = 0; jj < nm; jj++)
            dx_upd[ii] += K[ii * nm + jj] * innov[jj];
#endif

    double X_est[UKF_MAX_NS];
    for (int ii = 0; ii < ns; ii++) X_est[ii] = X_pred[ii] + dx_upd[ii];

    /* ---- Covariance update: P = P - K*Pz*K' ---- */
    double KPz[UKF_MAX_NS * UKF_MAX_NM];
    double KPzK[UKF_MAX_NS * UKF_MAX_NS];
    mmul(ns, nm, nm, K, Pz, KPz);
    mmul_bt(ns, ns, nm, KPz, K, KPzK);
    for (int i = 0; i < ns * ns; i++) st->P[i] -= KPzK[i];

    /* Symmetrize */
    for (int i = 0; i < ns; i++)
        for (int j = i + 1; j < ns; j++) {
            double avg = (st->P[i*ns + j] + st->P[j*ns + i]) / 2.0;
            st->P[i*ns + j] = st->P[j*ns + i] = avg;
        }

    memcpy(st->X_hat, X_est, ns * sizeof(double));

    if (x_out) memcpy(x_out, st->X_hat, ns * sizeof(double));
    if (rmse_out) {
        double tr = 0;
        for (int i = 0; i < ns; i++) tr += st->P[i * ns + i];
        *rmse_out = sqrt(tr);
    }

    return 0;
}

#endif /* UKF_ONLINE_CORE_H */
