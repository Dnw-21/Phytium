/**
 * ukf_core_opt.h — Optimized Pure C UKF implementation
 * IEEE 9-Bus 3-Generator Dynamic State Estimation
 *
 * Optimizations:
 *   - All heap allocations replaced with fixed-size stack arrays / VLA
 *   - NEON intrinsics for vector operations (ARMv8-A)
 *   - All helpers marked static inline
 */

#ifndef UKF_CORE_OPT_H
#define UKF_CORE_OPT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* ================================================================
 * Constants (9-bus 3-generator system)
 * ================================================================ */
#define N_GEN       3
#define N_BUS       9
#define NS          (2 * N_GEN)           /* state dim = 6 */
#define NM          (2 * N_GEN + 2 * N_BUS) /* meas dim = 24 */
#define N_SIGMA     (2 * NS)              /* sigma points = 12 */
#define FS          2000.0
#define DELTT       0.0005
#define TOTAL_TIME  180.0
#define NUM_SAMPLES 360000
#define T_SW        5.0
#define T_FC        5.3

#define SIG_ANGLE   0.01
#define SIG_SPEED   0.03
#define SIG_MEAS    0.01

/* ================================================================
 * NEON vector operations
 * ================================================================ */
#ifdef __ARM_NEON
static inline void vec_add_neon(int n, const double *a, const double *b, double *c) {
    int i = 0;
    for (; i + 1 < n; i += 2) {
        float64x2_t va = vld1q_f64(a + i);
        float64x2_t vb = vld1q_f64(b + i);
        vst1q_f64(c + i, vaddq_f64(va, vb));
    }
    for (; i < n; i++) c[i] = a[i] + b[i];
}

static inline void vec_sub_neon(int n, const double *a, const double *b, double *c) {
    int i = 0;
    for (; i + 1 < n; i += 2) {
        float64x2_t va = vld1q_f64(a + i);
        float64x2_t vb = vld1q_f64(b + i);
        vst1q_f64(c + i, vsubq_f64(va, vb));
    }
    for (; i < n; i++) c[i] = a[i] - b[i];
}

static inline void vec_scale_neon(int n, const double *a, double s, double *c) {
    int i = 0;
    float64x2_t vs = vdupq_n_f64(s);
    for (; i + 1 < n; i += 2) {
        float64x2_t va = vld1q_f64(a + i);
        vst1q_f64(c + i, vmulq_f64(va, vs));
    }
    for (; i < n; i++) c[i] = a[i] * s;
}

static inline double vec_dot_neon(int n, const double *a, const double *b) {
    int i = 0;
    float64x2_t vsum = vdupq_n_f64(0.0);
    for (; i + 1 < n; i += 2) {
        float64x2_t va = vld1q_f64(a + i);
        float64x2_t vb = vld1q_f64(b + i);
        vsum = vmlaq_f64(vsum, va, vb);
    }
    double sum = vgetq_lane_f64(vsum, 0) + vgetq_lane_f64(vsum, 1);
    for (; i < n; i++) sum += a[i] * b[i];
    return sum;
}
#else
static inline void vec_add_neon(int n, const double *a, const double *b, double *c) {
    for (int i = 0; i < n; i++) c[i] = a[i] + b[i];
}

static inline void vec_sub_neon(int n, const double *a, const double *b, double *c) {
    for (int i = 0; i < n; i++) c[i] = a[i] - b[i];
}

static inline void vec_scale_neon(int n, const double *a, double s, double *c) {
    for (int i = 0; i < n; i++) c[i] = a[i] * s;
}

static inline double vec_dot_neon(int n, const double *a, const double *b) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += a[i] * b[i];
    return sum;
}
#endif

/* ================================================================
 * Pure-C Linear Algebra (all stack-based)
 * ================================================================ */

/** Real symmetric positive-definite Cholesky: L*L' = A (lower) */
static inline int chol_real_lower(int n, double *A) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double sum = A[i * n + j];
            for (int k = 0; k < j; k++)
                sum -= A[i * n + k] * A[j * n + k];
            if (i == j) {
                if (sum <= 0.0) return -1;
                A[i * n + i] = sqrt(sum);
            } else {
                A[i * n + j] = sum / A[j * n + j];
            }
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            A[i * n + j] = 0.0;
    return 0;
}

/** Real symmetric eigenvalue decomposition (Jacobi method). */
static inline int eig_sym_real(int n, double *A, double *W) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++)
            A[i * n + j] = (i == j) ? 1.0 : 0.0;
        W[i] = 0.0;
    }
    double S[n * n];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            S[i * n + j] = A[i * n + j];

    for (int iter = 0; iter < 50; iter++) {
        double max_off = 0.0;
        int p = 0, q = 1;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++)
                if (fabs(S[i * n + j]) > max_off) {
                    max_off = fabs(S[i * n + j]);
                    p = i; q = j;
                }
        if (max_off < 1e-12) break;

        double theta = (S[q * n + q] - S[p * n + p]) / (2.0 * S[p * n + q]);
        double t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
        double c = 1.0 / sqrt(t * t + 1.0);
        double s = t * c;

        double app = S[p * n + p], aqq = S[q * n + q], apq = S[p * n + q];
        S[p * n + p] = c*c*app - 2*s*c*apq + s*s*aqq;
        S[q * n + q] = s*s*app + 2*s*c*apq + c*c*aqq;
        S[p * n + q] = S[q * n + p] = 0.0;

        for (int j = 0; j < n; j++) {
            if (j == p || j == q) continue;
            double ajp = S[j * n + p], ajq = S[j * n + q];
            S[j * n + p] = S[p * n + j] = c*ajp - s*ajq;
            S[j * n + q] = S[q * n + j] = s*ajp + c*ajq;
        }
        for (int i = 0; i < n; i++) {
            double vip = A[i * n + p], viq = A[i * n + q];
            A[i * n + p] = c*vip - s*viq;
            A[i * n + q] = s*vip + c*viq;
        }
    }
    for (int i = 0; i < n; i++) W[i] = S[i * n + i];
    return 0;
}

/** Real matrix inverse (Gauss-Jordan with partial pivoting, in-place). */
static inline int mat_inv_real(int n, double *A) {
    int pivot[n];
    double B[n * n];
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
        pivot[col] = max_row;

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

/** Complex matrix inverse (Gauss-Jordan, in-place). */
static inline int mat_inv_cplx(int n, double complex *A) {
    int pivot[n];
    double complex B[n * n];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            B[i * n + j] = (i == j) ? 1.0 : 0.0;

    for (int col = 0; col < n; col++) {
        int max_row = col;
        double max_val = cabs(A[col * n + col]);
        for (int row = col + 1; row < n; row++)
            if (cabs(A[row * n + col]) > max_val) {
                max_val = cabs(A[row * n + col]);
                max_row = row;
            }
        if (max_val < 1e-15) return -1;
        if (max_row != col) {
            for (int j = 0; j < n; j++) {
                double complex tmp = A[col * n + j]; A[col * n + j] = A[max_row * n + j]; A[max_row * n + j] = tmp;
                tmp = B[col * n + j]; B[col * n + j] = B[max_row * n + j]; B[max_row * n + j] = tmp;
            }
        }
        double complex piv = A[col * n + col];
        for (int j = 0; j < n; j++) { A[col * n + j] /= piv; B[col * n + j] /= piv; }
        for (int row = 0; row < n; row++) {
            if (row == col) continue;
            double complex fac = A[row * n + col];
            for (int j = 0; j < n; j++) {
                A[row * n + j] -= fac * A[col * n + j];
                B[row * n + j] -= fac * B[col * n + j];
            }
        }
    }
    memcpy(A, B, n * n * sizeof(double complex));
    return 0;
}

/* ================================================================
 * Matrix multiplication helpers
 * ================================================================ */
static inline void mmul_real(int m, int n, int k,
    const double *A, const double *B, double *C) {
    memset(C, 0, m * n * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int l = 0; l < k; l++)
            for (int j = 0; j < n; j++)
                C[i * n + j] += A[i * k + l] * B[l * n + j];
}

static inline void mmul_cplx(int m, int n, int k,
    const double complex *A, const double complex *B, double complex *C) {
    memset(C, 0, m * n * sizeof(double complex));
    for (int i = 0; i < m; i++)
        for (int l = 0; l < k; l++)
            for (int j = 0; j < n; j++)
                C[i * n + j] += A[i * k + l] * B[l * n + j];
}

static inline void mmul_real_bt(int m, int n, int k,
    const double *A, const double *B, double *C) {
    memset(C, 0, m * n * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            for (int l = 0; l < k; l++)
                C[i * n + j] += A[i * k + l] * B[j * k + l];
}

/* ================================================================
 * SystemParams structure
 * ================================================================ */
typedef struct {
    double complex YBUS[3][N_GEN * N_GEN];
    double complex RV[3][N_BUS * N_GEN];
    double E_abs[N_GEN];
    double PM[N_GEN];
    double M[N_GEN];
    double D[N_GEN];
    double X_0[NS];
    double deltt, t_SW, t_FC;
    int n, s, ns, nm, fs, num_samples;
} SystemParams;

#endif /* UKF_CORE_OPT_H */
