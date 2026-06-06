/*
 * matrix_ops.c — Self-contained linear algebra implementation.
 */
#include "matrix_ops.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Cholesky decomposition (lower): A = L * L^T
 * A is destroyed; L written to lower triangle.
 * ================================================================ */
int cholesky_decomp(int n, const double *A, double *L)
{
    memcpy(L, A, (size_t)n * n * sizeof(double));
    for (int j = 0; j < n; j++) {
        double sum = 0.0;
        for (int k = 0; k < j; k++) {
            double Ljk = L[j * n + k];
            sum += Ljk * Ljk;
        }
        double diag = L[j * n + j] - sum;
        if (diag <= 0.0) return -1;
        L[j * n + j] = sqrt(diag);
        double inv = 1.0 / L[j * n + j];
        for (int i = j + 1; i < n; i++) {
            sum = 0.0;
            for (int k = 0; k < j; k++)
                sum += L[i * n + k] * L[j * n + k];
            L[i * n + j] = (L[i * n + j] - sum) * inv;
        }
    }
    /* Zero upper triangle */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            L[i * n + j] = 0.0;
    return 0;
}

/* ================================================================
 * Forward + back substitution with Cholesky L.
 * ================================================================ */
void cholesky_solve(int n, const double *L, const double *b, double *x)
{
    /* Forward: L * y = b */
    for (int i = 0; i < n; i++) {
        double sum = b[i];
        for (int j = 0; j < i; j++)
            sum -= L[i * n + j] * x[j];
        x[i] = sum / L[i * n + i];
    }
    /* Backward: L^T * x = y  (store directly in x) */
    for (int i = n - 1; i >= 0; i--) {
        double sum = x[i];
        for (int j = i + 1; j < n; j++)
            sum -= L[j * n + i] * x[j];
        x[i] = sum / L[i * n + i];
    }
}

/* ================================================================
 * Solve A*X = B for SPD A via Cholesky (multiple RHS).
 * ================================================================ */
int solve_real_spd(int n, int nrhs, const double *A,
                   const double *B, double *X)
{
    double *L = (double*)malloc((size_t)n * n * sizeof(double));
    if (!L) return -1;
    if (cholesky_decomp(n, A, L) != 0) { free(L); return -1; }
    for (int r = 0; r < nrhs; r++) {
        double *tmp = (double*)malloc((size_t)n * sizeof(double));
        for (int i = 0; i < n; i++) tmp[i] = B[r * n + i];
        cholesky_solve(n, L, tmp, &X[r * n]);
        free(tmp);
    }
    free(L);
    return 0;
}

/* ================================================================
 * Real matrix multiply: C(m,n) = A(m,k) * B(k,n)
 * ================================================================ */
void matmul_real(int m, int k, int n,
                 const double *A, const double *B, double *C)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int p = 0; p < k; p++)
                s += A[i * k + p] * B[p * n + j];
            C[i * n + j] = s;
        }
    }
}

/* C = A * A^T  (A is m x k, C is m x m) */
void matmul_real_AT(int m, int k, const double *A, double *C)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < m; j++) {
            double s = 0.0;
            for (int p = 0; p < k; p++)
                s += A[i * k + p] * A[j * k + p];
            C[i * m + j] = s;
        }
    }
}

/* C = A^T * B  (A is k x m, B is k x n, C is m x n) */
void matmul_real_ATB(int k, int m, int n,
                     const double *A, const double *B, double *C)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int p = 0; p < k; p++)
                s += A[p * m + i] * B[p * n + j];
            C[i * n + j] = s;
        }
    }
}

void transpose_real(int rows, int cols, const double *A, double *B)
{
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            B[j * rows + i] = A[i * cols + j];
}

/* ================================================================
 * Complex operations
 * ================================================================ */
void cmatvec(int m, int n, const double complex *A,
             const double complex *x, double complex *y, int accum)
{
    if (!accum)
        for (int i = 0; i < m; i++) y[i] = 0.0 + 0.0*I;
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            y[i] += A[i * n + j] * x[j];
}

void compute_vbus(int n_bus, int n_gen, const double complex *RV,
                  const double complex *E, double complex *Vbus)
{
    cmatvec(n_bus, n_gen, RV, E, Vbus, 0);
}

void cvec_conj_prod(int n, const double complex *a,
                    const double complex *b, double complex *c)
{
    for (int i = 0; i < n; i++)
        c[i] = a[i] * conj(b[i]);
}

void print_matrix_real(const char *name, int rows, int cols, const double *A)
{
    printf("%s (%dx%d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++)
            printf(" %10.6f", A[i * cols + j]);
        printf("\n");
    }
}
