/*
 * dynamic_system.c — Swing equation + vectorized RK4.
 */
#include "dynamic_system.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void dynamic_system(int n, const double *x,
                    const double *M, const double *D,
                    const double complex *Ybusm,
                    const double *E_abs,
                    const double *PM, double *dx)
{
    for (int i = 0; i < n; i++) {
        double complex E_i = E_abs[i] * cexp(I * x[i]);
        double complex Ib_i = 0.0 + 0.0*I;
        for (int j = 0; j < n; j++)
            Ib_i += Ybusm[i*n + j] * E_abs[j] * cexp(I * x[j]);
        double Pe_i = creal(E_i * conj(Ib_i));
        dx[i]     = x[n + i];
        dx[n + i] = (PM[i] - Pe_i) / M[i] - D[i] * x[n + i] / M[i];
    }
}

/*
 * Compute PG for one sigma point given delta angles.
 * Helper for rk4_vectorized.
 */
static void compute_pg_one(int n, int nums, int s_idx,
                           const double *delta_col,
                           const double *E_abs,
                           const double complex *Ybusm,
                           double *pg_out)
{
    for (int i = 0; i < n; i++) {
        double complex E_i = E_abs[i] * cexp(I * delta_col[i*nums + s_idx]);
        double complex Ib_i = 0.0 + 0.0*I;
        for (int j = 0; j < n; j++) {
            double complex E_j = E_abs[j] * cexp(I * delta_col[j*nums + s_idx]);
            Ib_i += Ybusm[i*n + j] * E_j;
        }
        pg_out[i*nums + s_idx] = creal(E_i * conj(Ib_i));
    }
}

void rk4_vectorized(int n, double deltt, const double *E_abs,
                    int ns, const double *X_sigma,
                    const double *PM, const double *M, const double *D,
                    const double complex *Ybusm, double *xbreve)
{
    int nums = 2 * ns;
    int n_nums = n * nums;

    /* k1, k2, k3, k4 workspace */
    double *kd1 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kw1 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kd2 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kw2 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kd3 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kw3 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kd4 = (double*)calloc((size_t)n_nums, sizeof(double));
    double *kw4 = (double*)calloc((size_t)n_nums, sizeof(double));

    double *x_tmp = (double*)calloc((size_t)ns * nums, sizeof(double));
    double *pg_tmp = (double*)calloc((size_t)n_nums, sizeof(double));

    /* ---- Stage 1: k1 at X_sigma ---- */
    for (int s = 0; s < nums; s++)
        compute_pg_one(n, nums, s, X_sigma, E_abs, Ybusm, pg_tmp);
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            double om = X_sigma[(n+i)*nums + s];
            kd1[i*nums + s] = deltt * om;
            kw1[i*nums + s] = deltt * ((PM[i] - pg_tmp[i*nums + s] - D[i]*om) / M[i]);
        }
    }

    /* ---- Stage 2: k2 at X_sigma + k1/2 ---- */
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            x_tmp[i*nums + s]      = X_sigma[i*nums + s]      + kd1[i*nums + s] * 0.5;
            x_tmp[(n+i)*nums + s]  = X_sigma[(n+i)*nums + s]  + kw1[i*nums + s] * 0.5;
        }
    }
    for (int s = 0; s < nums; s++)
        compute_pg_one(n, nums, s, x_tmp, E_abs, Ybusm, pg_tmp);
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            double om = x_tmp[(n+i)*nums + s];
            kd2[i*nums + s] = deltt * om;
            kw2[i*nums + s] = deltt * ((PM[i] - pg_tmp[i*nums + s] - D[i]*om) / M[i]);
        }
    }

    /* ---- Stage 3: k3 at X_sigma + k2/2 ---- */
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            x_tmp[i*nums + s]      = X_sigma[i*nums + s]      + kd2[i*nums + s] * 0.5;
            x_tmp[(n+i)*nums + s]  = X_sigma[(n+i)*nums + s]  + kw2[i*nums + s] * 0.5;
        }
    }
    for (int s = 0; s < nums; s++)
        compute_pg_one(n, nums, s, x_tmp, E_abs, Ybusm, pg_tmp);
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            double om = x_tmp[(n+i)*nums + s];
            kd3[i*nums + s] = deltt * om;
            kw3[i*nums + s] = deltt * ((PM[i] - pg_tmp[i*nums + s] - D[i]*om) / M[i]);
        }
    }

    /* ---- Stage 4: k4 at X_sigma + k3 ---- */
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            x_tmp[i*nums + s]      = X_sigma[i*nums + s]      + kd3[i*nums + s];
            x_tmp[(n+i)*nums + s]  = X_sigma[(n+i)*nums + s]  + kw3[i*nums + s];
        }
    }
    for (int s = 0; s < nums; s++)
        compute_pg_one(n, nums, s, x_tmp, E_abs, Ybusm, pg_tmp);
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++) {
            double om = x_tmp[(n+i)*nums + s];
            kd4[i*nums + s] = deltt * om;
            kw4[i*nums + s] = deltt * ((PM[i] - pg_tmp[i*nums + s] - D[i]*om) / M[i]);
        }
    }

    /* ---- Assemble xbreve = X_sigma + (k1 + 2k2 + 2k3 + k4)/6 ---- */
    for (int s = 0; s < nums; s++) {
        for (int i = 0; i < n; i++)
            xbreve[i*nums + s] = X_sigma[i*nums + s]
                + (kd1[i*nums+s] + 2.0*kd2[i*nums+s] + 2.0*kd3[i*nums+s] + kd4[i*nums+s]) / 6.0;
        for (int i = 0; i < n; i++)
            xbreve[(n+i)*nums + s] = X_sigma[(n+i)*nums + s]
                + (kw1[i*nums+s] + 2.0*kw2[i*nums+s] + 2.0*kw3[i*nums+s] + kw4[i*nums+s]) / 6.0;
    }

    free(kd1); free(kw1); free(kd2); free(kw2);
    free(kd3); free(kw3); free(kd4); free(kw4);
    free(x_tmp); free(pg_tmp);
}
