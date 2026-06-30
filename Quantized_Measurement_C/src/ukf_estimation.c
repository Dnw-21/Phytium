/*
 * ukf_estimation.c — UKF implementation + data loading.
 */
#include "ukf_estimation.h"
#include "matrix_ops.h"
#include "dynamic_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int load_system_params(const char *filename, SystemParams *sp)
{
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", filename); return -1; }

    char line[256];
    int n = 0, s = 0, fs = 0, ns = 0, nm = 0, num = 0;
    double deltt = 0, tsw = 0, tfc = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%d %d %d %d %d %d %lf %lf %lf",
                   &n, &s, &fs, &ns, &nm, &num, &deltt, &tsw, &tfc) == 9) break;
    }

    sp->n = n; sp->s = s; sp->fs = fs; sp->ns = ns; sp->nm = nm;
    sp->num_samples = num; sp->deltt = deltt; sp->t_SW = tsw; sp->t_FC = tfc;

    int n2 = n * n;
    sp->YBUS = (double complex*)calloc((size_t)n2 * 3, sizeof(double complex));
    sp->RV   = (double complex*)calloc((size_t)s * n * 3, sizeof(double complex));
    sp->E_abs = (double*)calloc((size_t)n, sizeof(double));
    sp->PM    = (double*)calloc((size_t)n, sizeof(double));
    sp->M     = (double*)calloc((size_t)n, sizeof(double));
    sp->D     = (double*)calloc((size_t)n, sizeof(double));
    sp->X_hat_init = (double*)calloc((size_t)ns, sizeof(double));

    for (int stage = 0; stage < 3; stage++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                double re, im;
                while (fgets(line, sizeof(line), f))
                    if (line[0] != '#' && line[0] != '\n') break;
                sscanf(line, "%lf %lf", &re, &im);
                sp->YBUS[stage * n2 + i * n + j] = re + im * I;
            }

    for (int stage = 0; stage < 3; stage++)
        for (int i = 0; i < s; i++)
            for (int j = 0; j < n; j++) {
                double re, im;
                while (fgets(line, sizeof(line), f))
                    if (line[0] != '#' && line[0] != '\n') break;
                sscanf(line, "%lf %lf", &re, &im);
                sp->RV[stage * s * n + i * n + j] = re + im * I;
            }

    for (int i = 0; i < n; i++) {
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && line[0] != '\n') break;
        sscanf(line, "%lf", &sp->E_abs[i]);
    }
    for (int i = 0; i < n; i++) {
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && line[0] != '\n') break;
        sscanf(line, "%lf", &sp->PM[i]);
    }
    for (int i = 0; i < n; i++) {
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && line[0] != '\n') break;
        sscanf(line, "%lf", &sp->M[i]);
    }
    for (int i = 0; i < n; i++) {
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && line[0] != '\n') break;
        sscanf(line, "%lf", &sp->D[i]);
    }
    for (int i = 0; i < ns; i++) {
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && line[0] != '\n') break;
        sscanf(line, "%lf", &sp->X_hat_init[i]);
    }

    fclose(f);
    printf("  Loaded system_params.txt: n=%d s=%d ns=%d nm=%d\n", n, s, ns, nm);
    return 0;
}

void free_system_params(SystemParams *sp)
{
    free(sp->YBUS); free(sp->RV);
    free(sp->E_abs); free(sp->PM); free(sp->M); free(sp->D);
    free(sp->X_hat_init);
}

int load_measurements(const char *filename, int nm, int max_samples, double *Z_mes)
{
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", filename); return 0; }
    char line[4096];
    fgets(line, sizeof(line), f);
    int count = 0;
    while (count < max_samples && fgets(line, sizeof(line), f)) {
        char *tok = strtok(line, ",");
        tok = strtok(NULL, ",");
        for (int j = 0; j < nm && tok; j++) {
            Z_mes[j * max_samples + count] = atof(tok);
            tok = strtok(NULL, ",");
        }
        count++;
    }
    fclose(f);
    printf("  Loaded %d measurement rows\n", count);
    return count;
}

int load_true_states(const char *filename, int ns, int max_samples, double *X_true)
{
    FILE *f = fopen(filename, "r");
    if (!f) return 0;
    char line[4096];
    fgets(line, sizeof(line), f);
    int count = 0;
    while (count < max_samples && fgets(line, sizeof(line), f)) {
        char *tok = strtok(line, ",");
        for (int j = 0; j < ns && tok; j++) {
            X_true[j * max_samples + count] = atof(tok);
            tok = strtok(NULL, ",");
        }
        count++;
    }
    fclose(f);
    printf("  Loaded %d true state rows\n", count);
    return count;
}

static int get_system_state(double k, double t_SW, double t_FC)
{
    if (k < t_SW) return 0;
    else if (k <= t_FC) return 1;
    else return 2;
}

void ukf_estimation(const SystemParams *sp,
                    const double *Z_mes, int num_samples,
                    double *X_est, double *RMSE)
{
    int n = sp->n, s = sp->s, ns = sp->ns, nm = sp->nm;
    double sig = 1e-2, sig2 = sig * sig;
    int ns2 = ns * ns, nm2 = nm * nm;
    int nums = 2 * ns;

    /* Covariance matrices */
    double *P  = (double*)calloc((size_t)ns2, sizeof(double));
    double *Q  = (double*)calloc((size_t)ns2, sizeof(double));
    double *Rc = (double*)calloc((size_t)nm2, sizeof(double));
    for (int i = 0; i < ns; i++) { P[i*ns+i] = sig2;  Q[i*ns+i] = sig2; }
    for (int i = 0; i < nm; i++)   Rc[i*nm+i] = sig2;

    double *X_hat = (double*)malloc((size_t)ns * sizeof(double));
    memcpy(X_hat, sp->X_hat_init, (size_t)ns * sizeof(double));

    /* Workspace — allocate for max of ns^2 and nm^2 */
    int max_n2 = (ns2 > nm2) ? ns2 : nm2;
    double *L       = (double*)malloc((size_t)ns2 * sizeof(double));
    double *X_tilde = (double*)malloc((size_t)ns * nums * sizeof(double));
    double *X_sigma = (double*)malloc((size_t)ns * nums * sizeof(double));
    double *xbreve  = (double*)malloc((size_t)ns * nums * sizeof(double));
    double *X_rep   = (double*)malloc((size_t)ns * nums * sizeof(double));
    double *P_temp  = (double*)malloc((size_t)max_n2 * sizeof(double));
    double *zbreve  = (double*)malloc((size_t)nm * nums * sizeof(double));
    double *zhat    = (double*)malloc((size_t)nm * sizeof(double));
    double *Pz      = (double*)malloc((size_t)nm2 * sizeof(double));
    double *Pxz     = (double*)malloc((size_t)ns * nm * sizeof(double));
    double *Kmat    = (double*)malloc((size_t)ns * nm * sizeof(double));
    double *innov   = (double*)malloc((size_t)nm * sizeof(double));
    double *tmp_ns  = (double*)malloc((size_t)ns * sizeof(double));
    double *tmp_nm_nums = (double*)malloc((size_t)nm * nums * sizeof(double));

    printf("  UKF start: %d steps, ns=%d, nm=%d\n", num_samples, ns, nm);

    for (int idx = 0; idx < num_samples; idx++) {
        double k_time = (double)idx / sp->fs;
        int ps = get_system_state(k_time, sp->t_SW, sp->t_FC);
        const double complex *Ybusm = sp->YBUS + ps * n * n;
        const double complex *RVm   = sp->RV   + ps * s * n;

        /* Prediction: generate sigma points from P */
        for (int i = 0; i < ns2; i++) P_temp[i] = (double)ns * P[i];
        if (cholesky_decomp(ns, P_temp, L) != 0) {
            for (int i = 0; i < ns; i++) P_temp[i*ns+i] += 1e-10;
            cholesky_decomp(ns, P_temp, L);
        }

        for (int i = 0; i < ns; i++) {
            for (int k = 0; k < ns; k++) {
                X_tilde[i*nums + k]      =  L[i*ns + k];
                X_tilde[i*nums + ns + k] = -L[i*ns + k];
            }
        }
        for (int k = 0; k < nums; k++)
            for (int i = 0; i < ns; i++)
                X_sigma[i*nums + k] = X_hat[i] + X_tilde[i*nums + k];

        /* Propagate sigma points */
        rk4_vectorized(n, sp->deltt, sp->E_abs, ns, X_sigma,
                       sp->PM, sp->M, sp->D, Ybusm, xbreve);

        /* Predicted mean */
        for (int i = 0; i < ns; i++) {
            double s = 0.0;
            for (int k = 0; k < nums; k++) s += xbreve[i*nums + k];
            X_hat[i] = s / (double)nums;
        }

        /* Predicted covariance */
        for (int k = 0; k < nums; k++)
            for (int i = 0; i < ns; i++)
                X_rep[i*nums + k] = xbreve[i*nums + k] - X_hat[i];
        matmul_real_AT(ns, nums, X_rep, P_temp);
        for (int i = 0; i < ns2; i++) P[i] = P_temp[i] / (double)nums + Q[i];

        /* Update: new sigma points from updated P */
        for (int i = 0; i < ns2; i++) P_temp[i] = (double)ns * P[i];
        if (cholesky_decomp(ns, P_temp, L) != 0) {
            for (int i = 0; i < ns; i++) P_temp[i*ns+i] += 1e-10;
            cholesky_decomp(ns, P_temp, L);
        }
        for (int i = 0; i < ns; i++) {
            for (int k = 0; k < ns; k++) {
                X_tilde[i*nums + k]      =  L[i*ns + k];
                X_tilde[i*nums + ns + k] = -L[i*ns + k];
            }
        }
        for (int k = 0; k < nums; k++)
            for (int i = 0; i < ns; i++)
                X_sigma[i*nums + k] = X_hat[i] + X_tilde[i*nums + k];

        /* Predicted measurements from sigma points */
        for (int k = 0; k < nums; k++) {
            double complex E[10], Ibus[10], Vbus[20];
            for (int i = 0; i < n; i++)
                E[i] = sp->E_abs[i] * cexp(I * X_sigma[i*nums + k]);
            for (int i = 0; i < n; i++) {
                Ibus[i] = 0.0 + 0.0*I;
                for (int j = 0; j < n; j++)
                    Ibus[i] += Ybusm[i*n + j] * E[j];
            }
            for (int i = 0; i < s; i++) {
                Vbus[i] = 0.0 + 0.0*I;
                for (int j = 0; j < n; j++)
                    Vbus[i] += RVm[i*n + j] * E[j];
            }
            for (int i = 0; i < n; i++) {
                double complex S = E[i] * conj(Ibus[i]);
                zbreve[i*nums + k] = creal(S);
                zbreve[(n+i)*nums + k] = cimag(S);
            }
            for (int i = 0; i < s; i++) {
                zbreve[(2*n+i)*nums + k] = cabs(Vbus[i]);
                zbreve[(2*n+s+i)*nums + k] = carg(Vbus[i]);
            }
        }

        /* Measurement mean */
        for (int i = 0; i < nm; i++) {
            double s = 0.0;
            for (int k = 0; k < nums; k++) s += zbreve[i*nums + k];
            zhat[i] = s / (double)nums;
        }

        /* Innovation covariance Pz */
        for (int k = 0; k < nums; k++)
            for (int i = 0; i < nm; i++)
                tmp_nm_nums[i*nums + k] = zbreve[i*nums + k] - zhat[i];
        matmul_real_AT(nm, nums, tmp_nm_nums, P_temp);
        for (int i = 0; i < nm2; i++) Pz[i] = P_temp[i] / (double)nums + Rc[i];

        /* Cross-covariance Pxz */
        for (int k = 0; k < nums; k++)
            for (int i = 0; i < ns; i++)
                X_rep[i*nums + k] = X_sigma[i*nums + k] - X_hat[i];
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < nm; j++) {
                double s_v = 0.0;
                for (int k = 0; k < nums; k++)
                    s_v += X_rep[i*nums + k] * tmp_nm_nums[j*nums + k];
                Pxz[i*nm + j] = s_v / (double)nums;
            }

        /* Kalman gain K = Pxz * inv(Pz) */
        if (solve_real_spd(nm, ns, Pz, Pxz, Kmat) != 0) {
            for (int i = 0; i < ns*nm; i++) Kmat[i] = 0.0;
        }

        /* Innovation */
        for (int i = 0; i < nm; i++)
            innov[i] = Z_mes[i * num_samples + idx] - zhat[i];

        /* State update */
        for (int i = 0; i < ns; i++) {
            double dx = 0.0;
            for (int j = 0; j < nm; j++)
                dx += Kmat[i*nm + j] * innov[j];
            X_hat[i] += dx;
        }

        /* Covariance update: P = P - K*Pz*K^T */
        double *KPz = (double*)calloc((size_t)ns * nm, sizeof(double));
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < nm; j++)
                for (int k = 0; k < nm; k++)
                    KPz[i*nm + j] += Kmat[i*nm + k] * Pz[k*nm + j];
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < ns; j++) {
                double s_v = 0.0;
                for (int k = 0; k < nm; k++)
                    s_v += KPz[i*nm + k] * Kmat[j*nm + k];
                P[i*ns + j] -= s_v;
            }
        free(KPz);

        /* Store */
        for (int i = 0; i < ns; i++)
            X_est[i * num_samples + idx] = X_hat[i];
        double tr = 0.0;
        for (int i = 0; i < ns; i++) tr += P[i*ns + i];
        RMSE[idx] = sqrt(tr);

        if ((idx+1) % 20 == 0)
            printf("    Step %d/%d (t=%.4fs)\n", idx+1, num_samples, k_time);
    }

    printf("  UKF done.\n");

    free(P); free(Q); free(Rc);
    free(L); free(X_tilde); free(X_sigma); free(xbreve);
    free(X_rep); free(P_temp); free(zbreve); free(zhat);
    free(Pz); free(Pxz); free(Kmat); free(innov);
    free(tmp_ns); free(tmp_nm_nums); free(X_hat);
}
