/**
 * ukf_core.h — Pure C UKF implementation (zero external dependencies)
 * IEEE 9-Bus 3-Generator Dynamic State Estimation
 *
 * All linear algebra implemented in pure C99 (no LAPACK/BLAS required).
 * Matrix sizes: max 24x24, well within practical range for simple O(n³) algorithms.
 *
 * Build: gcc -O2 -o terminal_node terminal_node.c -lm
 *        gcc -O2 -o controller   controller.c   -lm
 */

#ifndef UKF_CORE_H
#define UKF_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

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
 * Memory helpers
 * ================================================================ */
#define ALLOC(n, type)  ((type *)calloc((n), sizeof(type)))
#define FREE(p)         do { if(p) { free(p); p = NULL; } } while(0)

static inline double *vec_r(int n)     { return ALLOC(n, double); }
static inline double *mat_r(int n)     { return ALLOC(n * n, double); }
static inline double complex *vec_c(int n) { return ALLOC(n, double complex); }
static inline double complex *mat_c(int n) { return ALLOC(n * n, double complex); }

/* compatibility aliases */
#define vec_real(n)  vec_r(n)
#define mat_real(n)  mat_r(n)
#define vec_cplx(n)  vec_c(n)
#define mat_cplx(n)  mat_c(n)

/* ================================================================
 * Pure-C Linear Algebra
 * ================================================================ */

/** Real symmetric positive-definite Cholesky: L*L' = A (lower)
 *  A is overwritten with L (lower triangle only).
 *  Returns 0 on success, -1 if not positive-definite. */
static int chol_real_lower(int n, double *A) {
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
    /* zero out upper triangle */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            A[i * n + j] = 0.0;
    return 0;
}

/** Real symmetric eigenvalue decomposition (Jacobi method).
 *  A overwritten with eigenvectors (columns), W = eigenvalues.
 *  Returns 0 on success. */
static int eig_sym_real(int n, double *A, double *W) {
    /* init eigenvectors to identity */
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++)
            A[i * n + j] = (i == j) ? 1.0 : 0.0;
        W[i] = 0.0;
    }
    /* copy A to working matrix (upper part used) */
    double *S = mat_r(n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            S[i * n + j] = A[i * n + j];

    /* Jacobi iterations */
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

        /* rotate */
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
    free(S);
    return 0;
}

/** Real matrix inverse (Gauss-Jordan with partial pivoting, in-place).
 *  Returns 0 on success, -1 if singular. */
static int mat_inv_real(int n, double *A) {
    int *pivot = (int *)malloc(n * sizeof(int));
    double *B = mat_r(n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            B[i * n + j] = (i == j) ? 1.0 : 0.0;

    for (int col = 0; col < n; col++) {
        /* find pivot */
        int max_row = col;
        double max_val = fabs(A[col * n + col]);
        for (int row = col + 1; row < n; row++)
            if (fabs(A[row * n + col]) > max_val) {
                max_val = fabs(A[row * n + col]);
                max_row = row;
            }
        if (max_val < 1e-15) { free(pivot); free(B); return -1; }
        pivot[col] = max_row;

        /* swap rows */
        if (max_row != col) {
            for (int j = 0; j < n; j++) {
                double tmp = A[col * n + j]; A[col * n + j] = A[max_row * n + j]; A[max_row * n + j] = tmp;
                tmp = B[col * n + j]; B[col * n + j] = B[max_row * n + j]; B[max_row * n + j] = tmp;
            }
        }

        /* eliminate */
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
    free(pivot); free(B);
    return 0;
}

/** Complex matrix inverse (Gauss-Jordan, in-place). Returns 0 on success. */
static int mat_inv_cplx(int n, double complex *A) {
    int *pivot = (int *)malloc(n * sizeof(int));
    double complex *B = mat_c(n);
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
        if (max_val < 1e-15) { free(pivot); free(B); return -1; }
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
    free(pivot); free(B);
    return 0;
}

/* ================================================================
 * Matrix multiplication helpers
 * ================================================================ */
/** C(m×n) = A(m×k) * B(k×n) — real */
static inline void mmul_real(int m, int n, int k,
    const double *A, const double *B, double *C) {
    memset(C, 0, m * n * sizeof(double));
    for (int i = 0; i < m; i++)
        for (int l = 0; l < k; l++)
            for (int j = 0; j < n; j++)
                C[i * n + j] += A[i * k + l] * B[l * n + j];
}

/** C(m×n) = A(m×k) * B(k×n) — complex */
static inline void mmul_cplx(int m, int n, int k,
    const double complex *A, const double complex *B, double complex *C) {
    memset(C, 0, m * n * sizeof(double complex));
    for (int i = 0; i < m; i++)
        for (int l = 0; l < k; l++)
            for (int j = 0; j < n; j++)
                C[i * n + j] += A[i * k + l] * B[l * n + j];
}

/** C(m×n) = A(m×k) * B'(n×k) i.e. B transposed — real */
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

#endif /* UKF_CORE_H */
