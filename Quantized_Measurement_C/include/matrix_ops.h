/*
 * matrix_ops.h
 * ============================================================
 * Self-contained linear algebra for UKF state estimation.
 * Supports real symmetric Cholesky, triangular solve,
 * complex & real matrix multiplication.
 * Uses C99 complex.h for complex numbers.
 * ============================================================
 */
#ifndef MATRIX_OPS_H
#define MATRIX_OPS_H

#include <complex.h>
#include <stddef.h>

/* ---------- Real matrix operations ---------- */

/*
 * Cholesky decomposition: A = L * L^T  (lower triangular)
 * A: n x n real symmetric positive-definite, input
 * L: n x n real, output (lower triangle filled, upper untouched)
 * Returns 0 on success, -1 if matrix is not SPD.
 */
int cholesky_decomp(int n, const double *A, double *L);

/*
 * Solve L * L^T * x = b  (using pre-computed Cholesky L)
 * L: n x n lower triangular from cholesky_decomp
 * b: n-vector (input)
 * x: n-vector (output, may alias b)
 */
void cholesky_solve(int n, const double *L, const double *b, double *x);

/*
 * Real matrix multiply: C = A * B
 * A: m x k,  B: k x n,  C: m x n
 */
void matmul_real(int m, int k, int n,
                 const double *A, const double *B, double *C);

/*
 * Real matrix multiply with transpose: C = A * B^T
 * A: m x k,  B: n x k,  C: m x n
 */
void matmul_real_AT(int m, int k, const double *A, double *C);

/*
 * Real matrix multiply with A^T * B: C = A^T * B
 * A: k x m,  B: k x n,  C: m x n
 */
void matmul_real_ATB(int k, int m, int n,
                     const double *A, const double *B, double *C);

/*
 * Solve linear system A * X = B  (via Cholesky, A must be SPD)
 * A: n x n SPD matrix
 * B: n x nrhs right-hand sides
 * X: n x nrhs solution (may alias B)
 * Returns 0 on success.
 */
int solve_real_spd(int n, int nrhs, const double *A,
                   const double *B, double *X);

/* ---------- Complex matrix operations ---------- */

/*
 * Complex matrix-vector multiply: y = A * x
 * A: m x n complex,  x: n-vector complex,  y: m-vector complex (output)
 * If accum != 0, does y += A*x instead of y = A*x
 */
void cmatvec(int m, int n, const double complex *A,
             const double complex *x, double complex *y, int accum);

/*
 * Compute bus voltages from internal voltages: Vbus = RV * E
 * RV: n_bus x n_gen complex,  E: n_gen complex,  Vbus: n_bus complex (output)
 */
void compute_vbus(int n_bus, int n_gen, const double complex *RV,
                  const double complex *E, double complex *Vbus);

/*
 * Complex element-wise product: c[i] = a[i] * conj(b[i])
 */
void cvec_conj_prod(int n, const double complex *a,
                    const double complex *b, double complex *c);

/* ---------- Utility ---------- */

/*
 * Matrix transpose (real): B = A^T
 * A: rows x cols, B: cols x rows
 */
void transpose_real(int rows, int cols, const double *A, double *B);

/*
 * Print a real matrix (debug)
 */
void print_matrix_real(const char *name, int rows, int cols, const double *A);

#endif /* MATRIX_OPS_H */
