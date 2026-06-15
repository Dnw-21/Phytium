/**
 * ukf_core_39_ft.h — FT-optimized C UKF for 39-Bus 10-Generator
 * Uses: BLAS-FT (dgemm/dgemv/dsyrk), VML-FT (vsincos_d)
 */
#ifndef UKF_CORE_39_FT_H
#define UKF_CORE_39_FT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#include <cblas.h>
#include <vml-ft.h>

#define N_GEN       10
#define N_BUS       39
#define NS          (2 * N_GEN)
#define NM          (2 * N_GEN + 2 * N_BUS)
#define N_SIGMA     (2 * NS)
#define FS          2000.0
#define DELTT       0.0005
#define TOTAL_TIME  180.0
#define NUM_SAMPLES 360000
#define T_SW        5.0
#define T_FC        5.3

#define SIG_ANGLE   0.01
#define SIG_SPEED   0.03
#define SIG_MEAS    0.01

/* ---- SystemParams ---- */
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

/* ---- Load binary system_params ---- */
static inline int load_system_params(SystemParams *sp, const char *fname) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) return -1;
    int dims[6];
    (void)fread(dims, sizeof(int), 6, fp);
    sp->n = dims[0]; sp->s = dims[1]; sp->ns = dims[2];
    sp->nm = dims[3]; sp->fs = dims[4]; sp->num_samples = dims[5];
    double scalars[4];
    (void)fread(scalars, sizeof(double), 4, fp);
    sp->deltt = scalars[0]; sp->t_SW = scalars[1]; sp->t_FC = scalars[2];
    for (int ps = 0; ps < 3; ps++)
        (void)fread(sp->YBUS[ps], sizeof(double complex), sp->n * sp->n, fp);
    for (int ps = 0; ps < 3; ps++)
        (void)fread(sp->RV[ps], sizeof(double complex), sp->s * sp->n, fp);
    (void)fread(sp->E_abs, sizeof(double), sp->n, fp);
    (void)fread(sp->PM, sizeof(double), sp->n, fp);
    (void)fread(sp->M, sizeof(double), sp->n, fp);
    (void)fread(sp->D, sizeof(double), sp->n, fp);
    (void)fread(sp->X_0, sizeof(double), sp->ns, fp);
    fclose(fp);
    return 0;
}

static inline int get_ps(double k, double tsw, double tfc) {
    if (k < tsw) return 0;
    if (k <= tfc) return 1;
    return 2;
}

/* ---- Cholesky: L*L' = A, lower triangle (hand-written) ---- */
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

/* ---- Matrix inverse: A = inv(A) (self-written Gauss-Jordan, row-major) ---- */
static inline int mat_inv_real(int n, double *A) {
    double B[NM * NM];
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

/* ---- Matrix multiply: C = A * B (BLAS-FT dgemm, row-major) ---- */
static inline void mmul_real(int m, int n_, int k,
    const double *A, const double *B, double *C) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                m, n_, k, 1.0, A, k, B, n_, 0.0, C, n_);
}

static inline void mmul_real_bt(int m, int n_, int k,
    const double *A, const double *B, double *C) {
    /* C = A * B^T where B is n_ x k */
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                m, n_, k, 1.0, A, k, B, k, 0.0, C, n_);
}

/* ---- Vectorized RK4 with VML-FT batched cexp ---- */
static inline void rk4_sigma(int n, double deltt, const double *E_abs, int ns,
                      const double *X_sigma, int n_sigma,
                      const double *PM, const double *M, const double *D,
                      const double complex *Ybusm, double *xbreve) {
    double k1_w[10 * N_SIGMA];
    double k1_d[10 * N_SIGMA];
    double PG[10 * N_SIGMA];

    double complex cexp_buf[10 * N_SIGMA];
    double angles[10 * N_SIGMA];
    double sin_buf[10 * N_SIGMA];
    double cos_buf[10 * N_SIGMA];

    /* k1: batch vsincos for all sigma points */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++)
            angles[si * n + i] = X_sigma[si * ns + i];
    vsincos_d(n * n_sigma, angles, sin_buf, cos_buf);
    for (int j = 0; j < n * n_sigma; j++)
        cexp_buf[j] = cos_buf[j] + I * sin_buf[j];

    for (int si = 0; si < n_sigma; si++) {
        double complex E[10], Ibus[10];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp_buf[si * n + i];
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            PG[si * n + i] = creal(E[i] * conj(Ibus[i]));
            k1_d[si * n + i] = deltt * X_sigma[si * ns + n + i];
            k1_w[si * n + i] = deltt * ((PM[i] - PG[si * n + i]
                             - D[i] * X_sigma[si * ns + n + i]) / M[i]);
        }
    }

    double tmpX[NS * N_SIGMA];
    double k2_w[10 * N_SIGMA], k2_d[10 * N_SIGMA];
    double k3_w[10 * N_SIGMA], k3_d[10 * N_SIGMA];
    double k4_w[10 * N_SIGMA], k4_d[10 * N_SIGMA];

    /* k2 */
    for (int i = 0; i < ns * n_sigma; i++) tmpX[i] = X_sigma[i];
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] += 0.5 * k1_d[si * n + i];
            tmpX[si * ns + n + i] += 0.5 * k1_w[si * n + i];
        }
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++)
            angles[si * n + i] = tmpX[si * ns + i];
    vsincos_d(n * n_sigma, angles, sin_buf, cos_buf);
    for (int j = 0; j < n * n_sigma; j++)
        cexp_buf[j] = cos_buf[j] + I * sin_buf[j];
    for (int si = 0; si < n_sigma; si++) {
        double complex E[10], Ibus[10];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp_buf[si * n + i];
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k2_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k2_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* k3 */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + 0.5 * k2_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + 0.5 * k2_w[si * n + i];
        }
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++)
            angles[si * n + i] = tmpX[si * ns + i];
    vsincos_d(n * n_sigma, angles, sin_buf, cos_buf);
    for (int j = 0; j < n * n_sigma; j++)
        cexp_buf[j] = cos_buf[j] + I * sin_buf[j];
    for (int si = 0; si < n_sigma; si++) {
        double complex E[10], Ibus[10];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp_buf[si * n + i];
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k3_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k3_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* k4 */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + k3_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + k3_w[si * n + i];
        }
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++)
            angles[si * n + i] = tmpX[si * ns + i];
    vsincos_d(n * n_sigma, angles, sin_buf, cos_buf);
    for (int j = 0; j < n * n_sigma; j++)
        cexp_buf[j] = cos_buf[j] + I * sin_buf[j];
    for (int si = 0; si < n_sigma; si++) {
        double complex E[10], Ibus[10];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp_buf[si * n + i];
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k4_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k4_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* Assemble */
    for (int si = 0; si < n_sigma; si++) {
        for (int i = 0; i < n; i++) {
            int idx_a = si * ns + i, idx_d = si * n + i;
            xbreve[idx_a] = X_sigma[idx_a] + (k1_d[idx_d] + 2*k2_d[idx_d]
                            + 2*k3_d[idx_d] + k4_d[idx_d]) / 6.0;
        }
        for (int i = 0; i < n; i++) {
            int idx_w = si * ns + n + i, idx_k = si * n + i;
            xbreve[idx_w] = X_sigma[idx_w] + (k1_w[idx_k] + 2*k2_w[idx_k]
                            + 2*k3_w[idx_k] + k4_w[idx_k]) / 6.0;
        }
    }
}

/* ---- Persistent UKF state ---- */
typedef struct {
    double P[NS * NS];
    double X_hat[NS];
    double Q_mat[NS * NS];
    double R_meas[NM * NM];
    double W[N_SIGMA];
    int initialized;
} UKFState;

static inline void ukf_init(const SystemParams *sp, UKFState *st) {
    int ns = sp->ns, nm = sp->nm, n = sp->n, n_sigma = N_SIGMA;
    memset(st->P, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->P[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        st->P[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }
    memset(st->Q_mat, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->Q_mat[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        st->Q_mat[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }
    memset(st->R_meas, 0, nm * nm * sizeof(double));
    for (int i = 0; i < nm; i++)
        st->R_meas[i * nm + i] = SIG_MEAS * SIG_MEAS;
    double w_val = 1.0 / (2.0 * ns);
    for (int i = 0; i < n_sigma; i++) st->W[i] = w_val;
    memcpy(st->X_hat, sp->X_0, ns * sizeof(double));
    st->initialized = 1;
}

/* ---- UKF step (FT-optimized) ---- */
static inline int ukf_step(const SystemParams *sp, UKFState *st,
                    const double *z_k, double k_time,
                    double *x_out, double *rmse_out) {
    if (!st->initialized) return -1;

    int n = sp->n, s = sp->s, ns = sp->ns, nm = sp->nm, n_sigma = N_SIGMA;
    double w_val = st->W[0];
    int ps = get_ps(k_time, sp->t_SW, sp->t_FC);

    double complex Ybusm[100];
    memcpy(Ybusm, sp->YBUS[ps], n * n * sizeof(double complex));

    /* Sigma points from P (Cholesky via LAPACK-FT) */
    double P_scaled[NS * NS];
    for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
    double L_root[NS * NS];
    memcpy(L_root, P_scaled, ns * ns * sizeof(double));
    if (chol_real_lower(ns, L_root) != 0) {
        for (int i = 0; i < ns; i++) st->P[i * ns + i] += 1e-8;
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
        memcpy(L_root, P_scaled, ns * ns * sizeof(double));
        if (chol_real_lower(ns, L_root) != 0) {
            memset(L_root, 0, ns * ns * sizeof(double));
            for (int i = 0; i < ns; i++) L_root[i * ns + i] = 1e-6;
        }
    }
    double X_sigma[NS * N_SIGMA];
    for (int si = 0; si < ns; si++)
        for (int ii = 0; ii < ns; ii++) {
            X_sigma[si * ns + ii] = st->X_hat[ii] + L_root[ii * ns + si];
            X_sigma[(si + ns) * ns + ii] = st->X_hat[ii] - L_root[ii * ns + si];
        }

    /* Prediction (RK4 with VML-FT cexp) */
    double xbreve[NS * N_SIGMA];
    rk4_sigma(n, sp->deltt, sp->E_abs, ns, X_sigma, n_sigma,
              sp->PM, sp->M, sp->D, Ybusm, xbreve);

    double X_hat_new[NS];
    double ones_sigma[N_SIGMA];
    for (int i = 0; i < n_sigma; i++) ones_sigma[i] = 1.0;
    cblas_dgemv(CblasRowMajor, CblasTrans, n_sigma, ns,
                w_val, xbreve, ns, ones_sigma, 1, 0.0, X_hat_new, 1);

    double Dev[NS * N_SIGMA];
    for (int si = 0; si < n_sigma; si++)
        for (int ii = 0; ii < ns; ii++)
            Dev[ii * n_sigma + si] = xbreve[si * ns + ii] - X_hat_new[ii];

    double P_pred[NS * NS];
    memset(P_pred, 0, ns * ns * sizeof(double));
    cblas_dsyrk(CblasRowMajor, CblasUpper, CblasNoTrans,
                ns, n_sigma, w_val, Dev, n_sigma, 0.0, P_pred, ns);
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < i; j++)
            P_pred[i * ns + j] = P_pred[j * ns + i];
    for (int i = 0; i < ns * ns; i++) st->P[i] = P_pred[i] + st->Q_mat[i];
    for (int i = 0; i < ns; i++)
        for (int j = i+1; j < ns; j++) {
            double avg = (st->P[i*ns+j] + st->P[j*ns+i]) / 2.0;
            st->P[i*ns+j] = st->P[j*ns+i] = avg;
        }

    /* New sigma points */
    for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
    memcpy(L_root, P_scaled, ns * ns * sizeof(double));
    if (chol_real_lower(ns, L_root) != 0) {
        for (int i = 0; i < ns; i++) st->P[i * ns + i] += 1e-8;
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * st->P[i];
        memcpy(L_root, P_scaled, ns * ns * sizeof(double));
        if (chol_real_lower(ns, L_root) != 0) {
            memset(L_root, 0, ns * ns * sizeof(double));
            for (int i = 0; i < ns; i++) L_root[i * ns + i] = 1e-6;
        }
    }
    for (int si = 0; si < ns; si++)
        for (int ii = 0; ii < ns; ii++) {
            X_sigma[si * ns + ii] = X_hat_new[ii] + L_root[ii * ns + si];
            X_sigma[(si + ns) * ns + ii] = X_hat_new[ii] - L_root[ii * ns + si];
        }

    /* Measurement prediction (VML-FT batched vsincos) */
    double complex cexp_buf[N_SIGMA * N_GEN];
    double angles_mp[N_SIGMA * N_GEN];
    double sin_mp[N_SIGMA * N_GEN];
    double cos_mp[N_SIGMA * N_GEN];
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++)
            angles_mp[si * n + i] = X_sigma[si * ns + i];
    vsincos_d(n * n_sigma, angles_mp, sin_mp, cos_mp);
    for (int j = 0; j < n * n_sigma; j++)
        cexp_buf[j] = cos_mp[j] + I * sin_mp[j];

    double zbreve[NM * N_SIGMA];
    for (int si = 0; si < n_sigma; si++) {
        double complex E[10], Ibus[10], Vbus[39];
        for (int i = 0; i < n; i++)
            E[i] = sp->E_abs[i] * cexp_buf[si * n + i];
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        memset(Vbus, 0, s * sizeof(double complex));
        for (int i = 0; i < s; i++)
            for (int j = 0; j < n; j++)
                Vbus[i] += sp->RV[ps][i * n + j] * E[j];
        for (int i = 0; i < n; i++) zbreve[si * nm + i] = creal(E[i] * conj(Ibus[i]));
        for (int i = 0; i < n; i++) zbreve[si * nm + n + i] = cimag(E[i] * conj(Ibus[i]));
        for (int i = 0; i < s; i++) zbreve[si * nm + 2*n + i] = cabs(Vbus[i]);
        for (int i = 0; i < s; i++) zbreve[si * nm + 2*n + s + i] = carg(Vbus[i]);
    }

    double zhat[NM];
    cblas_dgemv(CblasRowMajor, CblasTrans, n_sigma, nm,
                w_val, zbreve, nm, ones_sigma, 1, 0.0, zhat, 1);

    double Dz[NM * N_SIGMA];
    for (int si = 0; si < n_sigma; si++)
        for (int ii = 0; ii < nm; ii++)
            Dz[ii * n_sigma + si] = zbreve[si * nm + ii] - zhat[ii];

    double Pz[NM * NM];
    memcpy(Pz, st->R_meas, nm * nm * sizeof(double));
    cblas_dsyrk(CblasRowMajor, CblasUpper, CblasNoTrans,
                nm, n_sigma, w_val, Dz, n_sigma, 1.0, Pz, nm);
    for (int i = 0; i < nm; i++)
        for (int j = 0; j < i; j++)
            Pz[i * nm + j] = Pz[j * nm + i];

    double Pxz[NS * NM];
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                ns, nm, n_sigma, w_val, Dev, n_sigma, Dz, n_sigma, 0.0, Pxz, nm);

    /* Kalman gain K = Pxz * inv(Pz) (LAPACK-FT + BLAS-FT) */
    double K[NS * NM];
    double Pz_inv[NM * NM];
    memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
    if (mat_inv_real(nm, Pz_inv) != 0) {
        for (int i = 0; i < nm; i++) Pz[i * nm + i] += 1e-8;
        memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
        mat_inv_real(nm, Pz_inv);
    }
    mmul_real(ns, nm, nm, Pxz, Pz_inv, K);

    double innov[NM], dx_update[NS];
    for (int ii = 0; ii < nm; ii++) innov[ii] = z_k[ii] - zhat[ii];
    cblas_dgemv(CblasRowMajor, CblasNoTrans, ns, nm, 1.0, K, nm, innov, 1, 0.0, dx_update, 1);
    for (int ii = 0; ii < ns; ii++) X_hat_new[ii] += dx_update[ii];

    /* P = P - K*Pz*K' */
    double KPz[NS * NM];
    double KPzK[NS * NS];
    mmul_real(ns, nm, nm, K, Pz, KPz);
    mmul_real_bt(ns, ns, nm, KPz, K, KPzK);
    for (int i = 0; i < ns * ns; i++) st->P[i] -= KPzK[i];
    for (int i = 0; i < ns; i++)
        for (int j = i+1; j < ns; j++) {
            double avg = (st->P[i*ns+j] + st->P[j*ns+i]) / 2.0;
            st->P[i*ns+j] = st->P[j*ns+i] = avg;
        }

    memcpy(st->X_hat, X_hat_new, ns * sizeof(double));
    if (x_out) memcpy(x_out, st->X_hat, ns * sizeof(double));
    if (rmse_out) {
        double tr = 0;
        for (int i = 0; i < ns; i++) tr += st->P[i * ns + i];
        *rmse_out = sqrt(tr);
    }
    return 0;
}

#endif