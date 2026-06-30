/**
 * controller.c — UKF State Estimation (C version)
 * IEEE 9-Bus 3-Generator System, 3-minute simulation
 *
 * Reads:  system_params.bin, measurements.txt, true_states.csv (optional)
 * Writes: ukf_est.csv, ukf_rmse.csv (for external plotting)
 *
 * Compile: gcc -O2 -o controller controller.c -llapack -lblas -lm
 * Run:     ./controller
 */

#include "ukf_core.h"

/* ================================================================
 * Load binary system_params file
 * ================================================================ */
static int load_system_params(SystemParams *sp, const char *fname) {
    FILE *fp = fopen(fname, "rb");
    if (!fp) return -1;

    int dims[6];
    fread(dims, sizeof(int), 6, fp);
    sp->n = dims[0]; sp->s = dims[1]; sp->ns = dims[2];
    sp->nm = dims[3]; sp->fs = dims[4]; sp->num_samples = dims[5];

    double scalars[4];
    fread(scalars, sizeof(double), 4, fp);
    sp->deltt = scalars[0]; sp->t_SW = scalars[1]; sp->t_FC = scalars[2];

    for (int ps = 0; ps < 3; ps++)
        fread(sp->YBUS[ps], sizeof(double complex), sp->n * sp->n, fp);
    for (int ps = 0; ps < 3; ps++)
        fread(sp->RV[ps], sizeof(double complex), sp->s * sp->n, fp);

    fread(sp->E_abs, sizeof(double), sp->n, fp);
    fread(sp->PM, sizeof(double), sp->n, fp);
    fread(sp->M, sizeof(double), sp->n, fp);
    fread(sp->D, sizeof(double), sp->n, fp);
    fread(sp->X_0, sizeof(double), sp->ns, fp);

    fclose(fp);
    return 0;
}

/* ================================================================
 * System state selector
 * ================================================================ */
static int get_ps(double k, double tsw, double tfc) {
    if (k < tsw) return 0;
    if (k <= tfc) return 1;
    return 2;
}

/* ================================================================
 * Vectorized RK4 for sigma point propagation
 *   xbreve[ns x n_sigma] = rk4(X_sigma[ns x n_sigma])
 * ================================================================ */
static void rk4_sigma(int n, double deltt, const double *E_abs, int ns,
                      const double *X_sigma, int n_sigma,
                      const double *PM, const double *M, const double *D,
                      const double complex *Ybusm, double *xbreve) {
    int sz_sigma = ns * n_sigma;

    /* k1 */
    double *k1_w = vec_real(n * n_sigma);
    double *k1_d = vec_real(n * n_sigma);

    /* Compute PG for all sigma points */
    double *PG = vec_real(n * n_sigma);
    for (int si = 0; si < n_sigma; si++) {
        double complex E[3], Ibus[3];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp(I * X_sigma[si * ns + i]);
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

    /* k2 */
    double *tmpX = vec_real(sz_sigma);
    for (int i = 0; i < sz_sigma; i++) tmpX[i] = X_sigma[i];
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] += 0.5 * k1_d[si * n + i];
            tmpX[si * ns + n + i] += 0.5 * k1_w[si * n + i];
        }
    double *k2_w = vec_real(n * n_sigma);
    double *k2_d = vec_real(n * n_sigma);
    for (int si = 0; si < n_sigma; si++) {
        double complex E[3], Ibus[3];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp(I * tmpX[si * ns + i]);
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k2_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k2_w[si * n + i] = deltt * ((PM[i] - Pe
                             - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* k3 */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + 0.5 * k2_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + 0.5 * k2_w[si * n + i];
        }
    double *k3_w = vec_real(n * n_sigma);
    double *k3_d = vec_real(n * n_sigma);
    for (int si = 0; si < n_sigma; si++) {
        double complex E[3], Ibus[3];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp(I * tmpX[si * ns + i]);
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k3_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k3_w[si * n + i] = deltt * ((PM[i] - Pe
                             - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* k4 */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + k3_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + k3_w[si * n + i];
        }
    double *k4_w = vec_real(n * n_sigma);
    double *k4_d = vec_real(n * n_sigma);
    for (int si = 0; si < n_sigma; si++) {
        double complex E[3], Ibus[3];
        for (int i = 0; i < n; i++)
            E[i] = E_abs[i] * cexp(I * tmpX[si * ns + i]);
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];
        for (int i = 0; i < n; i++) {
            double Pe = creal(E[i] * conj(Ibus[i]));
            k4_d[si * n + i] = deltt * tmpX[si * ns + n + i];
            k4_w[si * n + i] = deltt * ((PM[i] - Pe
                             - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* Assemble xbreve */
    for (int si = 0; si < n_sigma; si++) {
        for (int i = 0; i < n; i++) {
            int idx_a = si * ns + i;
            int idx_d = si * n + i;
            xbreve[idx_a] = X_sigma[idx_a] + (k1_d[idx_d] + 2*k2_d[idx_d]
                            + 2*k3_d[idx_d] + k4_d[idx_d]) / 6.0;
        }
        for (int i = 0; i < n; i++) {
            int idx_w = si * ns + n + i;
            int idx_k = si * n + i;
            xbreve[idx_w] = X_sigma[idx_w] + (k1_w[idx_k] + 2*k2_w[idx_k]
                            + 2*k3_w[idx_k] + k4_w[idx_k]) / 6.0;
        }
    }

    free(k1_w); free(k1_d); free(k2_w); free(k2_d);
    free(k3_w); free(k3_d); free(k4_w); free(k4_d);
    free(PG); free(tmpX);
}

/* ================================================================
 * UKF Estimation
 * ================================================================ */
static void ukf_estimate(const SystemParams *sp,
                         const double *Z_mes, int num_meas,
                         double *X_est, double *RMSE) {
    int n = sp->n, s = sp->s, ns = sp->ns, nm = sp->nm;
    int n_sigma = N_SIGMA;

    /* Init P */
    double *P = mat_real(ns);
    for (int i = 0; i < n; i++) {
        P[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        P[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }

    /* Q */
    double *Q_mat = mat_real(ns);
    for (int i = 0; i < n; i++) {
        Q_mat[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        Q_mat[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }

    /* R */
    double *R_meas = mat_real(nm);
    for (int i = 0; i < nm; i++) R_meas[i * nm + i] = SIG_MEAS * SIG_MEAS;

    /* Weights */
    double *W = vec_real(n_sigma);
    double w_val = 1.0 / (2.0 * ns);
    for (int i = 0; i < n_sigma; i++) W[i] = w_val;

    double X_hat[NS];
    memcpy(X_hat, sp->X_0, ns * sizeof(double));

    printf("Starting UKF estimation for %d samples...\n", num_meas);

    for (int idx = 0; idx < num_meas; idx++) {
        double k = idx / (double)sp->fs;
        int ps = get_ps(k, sp->t_SW, sp->t_FC);

        double complex Ybusm[9];
        memcpy(Ybusm, sp->YBUS[ps], n * n * sizeof(double complex));

        /* ---- Sigma points: Cholesky with regularization ---- */
        double *P_scaled = mat_real(ns);
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * P[i];

        int chol_ok = 0;
        double *L_root = mat_real(ns);
        memcpy(L_root, P_scaled, ns * ns * sizeof(double));
        if (chol_real_lower(ns, L_root) == 0) {
            chol_ok = 1;
        } else {
            /* Regularize and retry */
            for (int i = 0; i < ns; i++) P[i * ns + i] += 1e-8;
            for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * P[i];
            memcpy(L_root, P_scaled, ns * ns * sizeof(double));
            if (chol_real_lower(ns, L_root) != 0) {
                /* Fallback: identity sqrt */
                memset(L_root, 0, ns * ns * sizeof(double));
                for (int i = 0; i < ns; i++) L_root[i * ns + i] = 1e-6;
            }
        }

        /* X_sigma = X_hat + [L, -L] (L is lower, columns are perturbations) */
        double *X_sigma = vec_real(ns * n_sigma);
        for (int si = 0; si < ns; si++) {
            for (int ii = 0; ii < ns; ii++) {
                X_sigma[si * ns + ii] = X_hat[ii] + L_root[ii * ns + si];    /* +col */
                X_sigma[(si + ns) * ns + ii] = X_hat[ii] - L_root[ii * ns + si]; /* -col */
            }
        }

        /* ---- Prediction via RK4 ---- */
        double *xbreve = vec_real(ns * n_sigma);
        rk4_sigma(n, sp->deltt, sp->E_abs, ns, X_sigma, n_sigma,
                  sp->PM, sp->M, sp->D, Ybusm, xbreve);

        /* Weighted mean */
        memset(X_hat, 0, ns * sizeof(double));
        for (int si = 0; si < n_sigma; si++)
            for (int ii = 0; ii < ns; ii++)
                X_hat[ii] += W[si] * xbreve[si * ns + ii];

        /* Predicted covariance */
        double *P_pred = mat_real(ns);
        for (int si = 0; si < n_sigma; si++) {
            double dev[NS];
            for (int ii = 0; ii < ns; ii++) dev[ii] = xbreve[si * ns + ii] - X_hat[ii];
            for (int ii = 0; ii < ns; ii++)
                for (int jj = 0; jj < ns; jj++)
                    P_pred[ii * ns + jj] += w_val * dev[ii] * dev[jj];
        }
        for (int i = 0; i < ns * ns; i++) P[i] = P_pred[i] + Q_mat[i];
        /* Symmetrize */
        for (int i = 0; i < ns; i++)
            for (int j = i+1; j < ns; j++) {
                double avg = (P[i*ns+j] + P[j*ns+i]) / 2.0;
                P[i*ns+j] = P[j*ns+i] = avg;
            }

        /* ---- New sigma points for measurement update ---- */
        for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * P[i];
        memcpy(L_root, P_scaled, ns * ns * sizeof(double));
        if (chol_real_lower(ns, L_root) != 0) {
            for (int i = 0; i < ns; i++) P[i * ns + i] += 1e-8;
            for (int i = 0; i < ns * ns; i++) P_scaled[i] = ns * P[i];
            memcpy(L_root, P_scaled, ns * ns * sizeof(double));
            if (chol_real_lower(ns, L_root) != 0) {
                memset(L_root, 0, ns * ns * sizeof(double));
                for (int i = 0; i < ns; i++) L_root[i * ns + i] = 1e-6;
            }
        }

        for (int si = 0; si < ns; si++) {
            for (int ii = 0; ii < ns; ii++) {
                X_sigma[si * ns + ii] = X_hat[ii] + L_root[ii * ns + si];
                X_sigma[(si + ns) * ns + ii] = X_hat[ii] - L_root[ii * ns + si];
            }
        }

        /* ---- Measurement prediction (complex voltage: real/imag) ---- */
        double *zbreve = vec_real(nm * n_sigma);
        for (int si = 0; si < n_sigma; si++) {
            double complex E[3], Ibus[3], Vbus[9];
            for (int i = 0; i < n; i++)
                E[i] = sp->E_abs[i] * cexp(I * X_sigma[si * ns + i]);
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
            for (int i = 0; i < s; i++) zbreve[si * nm + 2*n + i] = creal(Vbus[i]);
            for (int i = 0; i < s; i++) zbreve[si * nm + 2*n + s + i] = cimag(Vbus[i]);
        }

        /* Predicted measurement */
        double zhat[NM];
        memset(zhat, 0, nm * sizeof(double));
        for (int si = 0; si < n_sigma; si++)
            for (int ii = 0; ii < nm; ii++)
                zhat[ii] += W[si] * zbreve[si * nm + ii];

        /* Pz and Pxz */
        double *Pz = mat_real(nm);
        double *Pxz = ALLOC(ns * nm, double);
        for (int si = 0; si < n_sigma; si++) {
            double dz[NM], dx[NS];
            for (int ii = 0; ii < nm; ii++) dz[ii] = zbreve[si * nm + ii] - zhat[ii];
            for (int ii = 0; ii < ns; ii++) dx[ii] = X_sigma[si * ns + ii] - X_hat[ii];
            for (int ii = 0; ii < nm; ii++)
                for (int jj = 0; jj < nm; jj++)
                    Pz[ii * nm + jj] += w_val * dz[ii] * dz[jj];
            for (int ii = 0; ii < ns; ii++)
                for (int jj = 0; jj < nm; jj++)
                    Pxz[ii * nm + jj] += w_val * dx[ii] * dz[jj];
        }
        for (int i = 0; i < nm * nm; i++) Pz[i] += R_meas[i];

        /* ---- Kalman gain: K = Pxz * inv(Pz) ---- */
        double *K = ALLOC(ns * nm, double);
        double *Pz_inv = mat_real(nm);
        memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
        if (mat_inv_real(nm, Pz_inv) != 0) {
            /* Regularize and retry */
            for (int i = 0; i < nm; i++) Pz[i * nm + i] += 1e-8;
            memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
            mat_inv_real(nm, Pz_inv);
        }

        /* K = Pxz * Pz_inv */
        mmul_real(ns, nm, nm, Pxz, Pz_inv, K);

        /* ---- Measurement update ---- */
        const double *z_k = &Z_mes[idx * nm];
        double innov[NM], dx_update[NS];
        for (int ii = 0; ii < nm; ii++) innov[ii] = z_k[ii] - zhat[ii];
        memset(dx_update, 0, ns * sizeof(double));
        for (int ii = 0; ii < ns; ii++)
            for (int jj = 0; jj < nm; jj++)
                dx_update[ii] += K[ii * nm + jj] * innov[jj];
        for (int ii = 0; ii < ns; ii++) X_hat[ii] += dx_update[ii];

        /* P = P - K * Pz * K' */
        {
            double *KPz = ALLOC(ns * nm, double);
            double *KPzK = mat_real(ns);
            mmul_real(ns, nm, nm, K, Pz, KPz);
            mmul_real_bt(ns, ns, nm, KPz, K, KPzK);
            for (int i = 0; i < ns * ns; i++) P[i] -= KPzK[i];
            free(KPz); free(KPzK);
        }
        /* Symmetrize */
        for (int i = 0; i < ns; i++)
            for (int j = i+1; j < ns; j++) {
                double avg = (P[i*ns+j] + P[j*ns+i]) / 2.0;
                P[i*ns+j] = P[j*ns+i] = avg;
            }

        /* Store */
        memcpy(&X_est[idx * ns], X_hat, ns * sizeof(double));
        double tr = 0;
        for (int i = 0; i < ns; i++) tr += P[i * ns + i];
        RMSE[idx] = sqrt(tr);

        /* Progress */
        if ((idx + 1) % 50000 == 0)
            printf("  %.0f%% complete (%.1fs)\n", (idx+1)*100.0/num_meas, (idx+1)/sp->fs);

        free(P_scaled); free(L_root); free(X_sigma); free(xbreve);
        free(P_pred); free(zbreve); free(Pz); free(Pxz); free(K); free(Pz_inv);
    }

    free(P); free(Q_mat); free(R_meas); free(W);
    printf("UKF estimation complete.\n");
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    printf("============================================================\n");
    printf("Master Controller - UKF State Estimation (C)\n");
    printf("IEEE 9-Bus 3-Generator, 3min\n");
    printf("============================================================\n\n");

    /* Load system params */
    SystemParams sp;
    if (load_system_params(&sp, "system_params.bin") != 0) {
        fprintf(stderr, "ERROR: system_params.bin not found!\n");
        return 1;
    }
    printf("[Step 1/4] Loaded system_params.bin\n");
    printf("  Buses: %d, Generators: %d, Samples: %d\n", sp.s, sp.n, sp.num_samples);

    /* Load measurements */
    int nm = sp.nm, ns = sp.ns, n_samp = sp.num_samples;
    double *Z_mes = ALLOC(nm * n_samp, double);

    printf("[Step 2/4] Loading measurements.txt...\n");
    {
        FILE *fp = fopen("measurements.txt", "r");
        if (!fp) { fprintf(stderr, "ERROR: measurements.txt not found!\n"); return 1; }
        char line[8192];
        fgets(line, sizeof(line), fp); /* skip header */
        for (int i = 0; i < n_samp; i++) {
            fgets(line, sizeof(line), fp);
            char *tok = strtok(line, ",");
            tok = strtok(NULL, ","); /* skip timestamp */
            for (int j = 0; j < nm; j++) {
                Z_mes[i * nm + j] = atof(tok);
                tok = strtok(NULL, ",");
            }
            if ((i + 1) % 50000 == 0) printf("  Parsing: %.0f%%\n", (i+1)*100.0/n_samp);
        }
        fclose(fp);
    }

    /* Load true states (optional) */
    double *X_true = ALLOC(ns * n_samp, double);
    printf("[Step 3/4] Loading true_states.csv...\n");
    {
        FILE *fp = fopen("true_states.csv", "r");
        if (fp) {
            char line[4096];
            fgets(line, sizeof(line), fp);
            for (int i = 0; i < n_samp; i++) {
                fgets(line, sizeof(line), fp);
                char *tok = strtok(line, ",");
                for (int j = 0; j < ns; j++) {
                    X_true[i * ns + j] = atof(tok);
                    tok = strtok(NULL, ",");
                }
            }
            fclose(fp);
            printf("  Loaded true_states.csv\n");
        } else {
            printf("  true_states.csv not found, zero fill.\n");
            memset(X_true, 0, ns * n_samp * sizeof(double));
        }
    }

    /* Run UKF */
    printf("[Step 4/4] Running UKF Estimation...\n");
    double *X_est = ALLOC(ns * n_samp, double);
    double *RMSE = ALLOC(n_samp, double);
    ukf_estimate(&sp, Z_mes, n_samp, X_est, RMSE);

    /* Save results */
    printf("\n[Post Process] Saving results...\n");

    {
        FILE *fp = fopen("ukf_est.csv", "w");
        fprintf(fp, "delta1,delta2,delta3,omega1,omega2,omega3\n");
        for (int i = 0; i < n_samp; i++)
            fprintf(fp, "%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
                    X_est[i*ns], X_est[i*ns+1], X_est[i*ns+2],
                    X_est[i*ns+3], X_est[i*ns+4], X_est[i*ns+5]);
        fclose(fp);
        printf("  Saved ukf_est.csv\n");
    }

    {
        FILE *fp = fopen("ukf_rmse.csv", "w");
        fprintf(fp, "rmse\n");
        for (int i = 0; i < n_samp; i++) fprintf(fp, "%.8f\n", RMSE[i]);
        fclose(fp);
        printf("  Saved ukf_rmse.csv\n");
    }

    {
        FILE *fp = fopen("ukf_compare.csv", "w");
        fprintf(fp, "time,delta1_true,delta1_est,delta2_true,delta2_est,delta3_true,delta3_est,"
                "omega1_true,omega1_est,omega2_true,omega2_est,omega3_true,omega3_est\n");
        for (int i = 0; i < n_samp; i += 100) {  /* decimated for file size */
            double t = i / (double)sp.fs;
            fprintf(fp, "%.4f", t);
            for (int j = 0; j < ns; j++)
                fprintf(fp, ",%.6f,%.6f", X_true[i*ns+j], X_est[i*ns+j]);
            fprintf(fp, "\n");
        }
        fclose(fp);
        printf("  Saved ukf_compare.csv (decimated every 100 samples for plotting)\n");
    }

    free(Z_mes); free(X_true); free(X_est); free(RMSE);

    printf("\n============================================================\n");
    printf("Controller complete!\n");
    printf("Output: ukf_est.csv, ukf_rmse.csv, ukf_compare.csv\n");
    printf("============================================================\n");
    return 0;
}
