/**
 * controller_online_opt.c — Optimized Online UKF State Estimation
 * ============================================================
 * IEEE 9-Bus 3-Generator System
 *
 * Optimizations:
 *   - All heap allocations replaced with fixed-size stack arrays
 *   - NEON intrinsics for vector operations (via ukf_core_opt.h)
 *   - All helpers marked static inline in header
 */

#include "ukf_core_opt.h"

/* ================================================================
 * Persistent UKF state (retained between ukf_step calls)
 * ================================================================ */
typedef struct {
    double P[NS * NS];        /* error covariance */
    double X_hat[NS];         /* state estimate */
    double Q_mat[NS * NS];    /* process noise */
    double R_meas[NM * NM];   /* measurement noise */
    double W[N_SIGMA];        /* sigma weights */
    int initialized;
} UKFState;

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
 * Topology selector
 * ================================================================ */
static int get_ps(double k, double tsw, double tfc) {
    if (k < tsw) return 0;
    if (k <= tfc) return 1;
    return 2;
}

/* ================================================================
 * Vectorized RK4 (stack-allocated only)
 * ================================================================ */
static void rk4_sigma(int n, double deltt, const double *E_abs, int ns,
                      const double *X_sigma, int n_sigma,
                      const double *PM, const double *M, const double *D,
                      const double complex *Ybusm, double *xbreve) {
    double k1_w[N_GEN * N_SIGMA];
    double k1_d[N_GEN * N_SIGMA];
    double PG[N_GEN * N_SIGMA];

    /* --- k1 --- */
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

    double tmpX[NS * N_SIGMA];
    double k2_w[N_GEN * N_SIGMA], k2_d[N_GEN * N_SIGMA];
    double k3_w[N_GEN * N_SIGMA], k3_d[N_GEN * N_SIGMA];
    double k4_w[N_GEN * N_SIGMA], k4_d[N_GEN * N_SIGMA];

    /* --- k2 --- */
    for (int i = 0; i < ns * n_sigma; i++) tmpX[i] = X_sigma[i];
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] += 0.5 * k1_d[si * n + i];
            tmpX[si * ns + n + i] += 0.5 * k1_w[si * n + i];
        }
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
            k2_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* --- k3 --- */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + 0.5 * k2_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + 0.5 * k2_w[si * n + i];
        }
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
            k3_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* --- k4 --- */
    for (int si = 0; si < n_sigma; si++)
        for (int i = 0; i < n; i++) {
            tmpX[si * ns + i] = X_sigma[si * ns + i] + k3_d[si * n + i];
            tmpX[si * ns + n + i] = X_sigma[si * ns + n + i] + k3_w[si * n + i];
        }
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
            k4_w[si * n + i] = deltt * ((PM[i] - Pe - D[i] * tmpX[si * ns + n + i]) / M[i]);
        }
    }

    /* --- Assemble --- */
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

/* ================================================================
 * ukf_init — Initialize persistent UKF state (call once)
 * ================================================================ */
static void ukf_init(const SystemParams *sp, UKFState *st) {
    int ns = sp->ns, nm = sp->nm, n = sp->n, n_sigma = N_SIGMA;

    /* P */
    memset(st->P, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->P[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        st->P[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }

    /* Q */
    memset(st->Q_mat, 0, ns * ns * sizeof(double));
    for (int i = 0; i < n; i++) {
        st->Q_mat[i * ns + i] = SIG_ANGLE * SIG_ANGLE;
        st->Q_mat[(n + i) * ns + (n + i)] = SIG_SPEED * SIG_SPEED;
    }

    /* R */
    memset(st->R_meas, 0, nm * nm * sizeof(double));
    for (int i = 0; i < nm; i++)
        st->R_meas[i * nm + i] = SIG_MEAS * SIG_MEAS;

    /* Weights */
    double w_val = 1.0 / (2.0 * ns);
    for (int i = 0; i < n_sigma; i++) st->W[i] = w_val;

    /* Initial state */
    memcpy(st->X_hat, sp->X_0, ns * sizeof(double));

    st->initialized = 1;
}

/* ================================================================
 * ukf_step — Process ONE measurement vector (stack-optimized)
 * ================================================================ */
static int ukf_step(const SystemParams *sp, UKFState *st,
                    const double *z_k, double k_time,
                    double *x_out, double *rmse_out) {
    if (!st->initialized) return -1;

    int n = sp->n, s = sp->s, ns = sp->ns, nm = sp->nm, n_sigma = N_SIGMA;
    double w_val = st->W[0];
    int ps = get_ps(k_time, sp->t_SW, sp->t_FC);

    double complex Ybusm[9];
    memcpy(Ybusm, sp->YBUS[ps], n * n * sizeof(double complex));

    /* ---- Sigma points from P ---- */
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

    /* ---- Prediction (RK4) ---- */
    double xbreve[NS * N_SIGMA];
    rk4_sigma(n, sp->deltt, sp->E_abs, ns, X_sigma, n_sigma,
              sp->PM, sp->M, sp->D, Ybusm, xbreve);

    /* Weighted mean */
    double X_hat_new[NS];
    memset(X_hat_new, 0, ns * sizeof(double));
    double xtmp[NS];
    for (int si = 0; si < n_sigma; si++) {
        vec_scale_neon(ns, &xbreve[si * ns], w_val, xtmp);
        vec_add_neon(ns, X_hat_new, xtmp, X_hat_new);
    }

    /* Predicted covariance */
    double P_pred[NS * NS];
    memset(P_pred, 0, ns * ns * sizeof(double));
    for (int si = 0; si < n_sigma; si++) {
        double dev[NS];
        vec_sub_neon(ns, &xbreve[si * ns], X_hat_new, dev);
        for (int ii = 0; ii < ns; ii++)
            for (int jj = 0; jj < ns; jj++)
                P_pred[ii * ns + jj] += w_val * dev[ii] * dev[jj];
    }
    for (int i = 0; i < ns * ns; i++) st->P[i] = P_pred[i] + st->Q_mat[i];
    /* Symmetrize P */
    for (int i = 0; i < ns; i++)
        for (int j = i+1; j < ns; j++) {
            double avg = (st->P[i*ns+j] + st->P[j*ns+i]) / 2.0;
            st->P[i*ns+j] = st->P[j*ns+i] = avg;
        }

    /* ---- Measurement update: new sigma points ---- */
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

    /* ---- Predicted measurements ---- */
    double zbreve[NM * N_SIGMA];
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

    double zhat[NM];
    memset(zhat, 0, nm * sizeof(double));
    double ztmp[NM];
    for (int si = 0; si < n_sigma; si++) {
        vec_scale_neon(nm, &zbreve[si * nm], w_val, ztmp);
        vec_add_neon(nm, zhat, ztmp, zhat);
    }

    /* ---- Pz, Pxz ---- */
    double Pz[NM * NM];
    double Pxz[NS * NM];
    memset(Pz, 0, nm * nm * sizeof(double));
    memset(Pxz, 0, ns * nm * sizeof(double));
    for (int si = 0; si < n_sigma; si++) {
        double dz[NM], dx[NS];
        vec_sub_neon(nm, &zbreve[si * nm], zhat, dz);
        vec_sub_neon(ns, &X_sigma[si * ns], X_hat_new, dx);
        for (int ii = 0; ii < nm; ii++)
            for (int jj = 0; jj < nm; jj++)
                Pz[ii * nm + jj] += w_val * dz[ii] * dz[jj];
        for (int ii = 0; ii < ns; ii++)
            for (int jj = 0; jj < nm; jj++)
                Pxz[ii * nm + jj] += w_val * dx[ii] * dz[jj];
    }
    for (int i = 0; i < nm * nm; i++) Pz[i] += st->R_meas[i];

    /* ---- Kalman gain K = Pxz * inv(Pz) ---- */
    double K[NS * NM];
    double Pz_inv[NM * NM];
    memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
    if (mat_inv_real(nm, Pz_inv) != 0) {
        for (int i = 0; i < nm; i++) Pz[i * nm + i] += 1e-8;
        memcpy(Pz_inv, Pz, nm * nm * sizeof(double));
        mat_inv_real(nm, Pz_inv);
    }
    mmul_real(ns, nm, nm, Pxz, Pz_inv, K);

    /* ---- State update ---- */
    double innov[NM], dx_update[NS];
    vec_sub_neon(nm, z_k, zhat, innov);
    memset(dx_update, 0, ns * sizeof(double));
    for (int ii = 0; ii < ns; ii++)
        for (int jj = 0; jj < nm; jj++)
            dx_update[ii] += K[ii * nm + jj] * innov[jj];
    vec_add_neon(ns, X_hat_new, dx_update, X_hat_new);

    /* ---- P = P - K*Pz*K' ---- */
    double KPz[NS * NM];
    double KPzK[NS * NS];
    mmul_real(ns, nm, nm, K, Pz, KPz);
    mmul_real_bt(ns, ns, nm, KPz, K, KPzK);
    for (int i = 0; i < ns * ns; i++) st->P[i] -= KPzK[i];
    /* Symmetrize P */
    for (int i = 0; i < ns; i++)
        for (int j = i+1; j < ns; j++) {
            double avg = (st->P[i*ns+j] + st->P[j*ns+i]) / 2.0;
            st->P[i*ns+j] = st->P[j*ns+i] = avg;
        }

    /* Store updated state */
    memcpy(st->X_hat, X_hat_new, ns * sizeof(double));

    /* Output */
    if (x_out) memcpy(x_out, st->X_hat, ns * sizeof(double));
    if (rmse_out) {
        double tr = 0;
        for (int i = 0; i < ns; i++) tr += st->P[i * ns + i];
        *rmse_out = sqrt(tr);
    }

    return 0;
}

/* ================================================================
 * Main — online streaming mode
 * ================================================================ */
int main(int argc, char **argv) {
    /* ---- Determine input source ---- */
    FILE *input = stdin;
    if (argc > 1) {
        input = fopen(argv[1], "r");
        if (!input) { fprintf(stderr, "ERROR: cannot open %s\n", argv[1]); return 1; }
        fprintf(stderr, "[Init] Reading from file: %s\n", argv[1]);
    } else {
        fprintf(stderr, "[Init] Reading from stdin (pipe). Ctrl+D to end.\n");
    }

    /* ---- Load system params (once) ---- */
    SystemParams sp;
    if (load_system_params(&sp, "system_params.bin") != 0) {
        fprintf(stderr, "ERROR: system_params.bin not found!\n");
        fprintf(stderr, "Run: python convert_params.py first.\n");
        return 1;
    }
    fprintf(stderr, "[Init] Loaded system_params.bin (n=%d, s=%d, nm=%d)\n",
            sp.n, sp.s, sp.nm);

    /* ---- Initialize UKF state (once) ---- */
    UKFState st;
    ukf_init(&sp, &st);
    fprintf(stderr, "[Init] UKF initialized. Processing measurements one-by-one...\n\n");

    /* ---- Print CSV header ---- */
    printf("# time,delta1,delta2,delta3,omega1,omega2,omega3,RMSE\n");
    fflush(stdout);

    /* ---- Process measurements one at a time ---- */
    char line[8192];
    int count = 0;

    while (fgets(line, sizeof(line), input)) {
        /* Skip comments, empty lines, and CSV headers */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (strncmp(line, "time", 4) == 0 || strncmp(line, "step", 4) == 0) continue;

        /* Check for special commands */
        if (strncmp(line, "QUIT", 4) == 0 || strncmp(line, "EXIT", 4) == 0) break;

        /* Parse: timestamp,PG1,PG2,PG3,QG1,QG2,QG3, then measurements */
        char *tok = strtok(line, ",");
        double z_k[NM];
        int has_ts = 0;
        double k_time = count / (double)sp.fs;

        /* If first token is numeric with a decimal point, treat as timestamp */
        char *endptr;
        double first_val = strtod(tok, &endptr);
        if (endptr != tok && *endptr == '\0' && first_val < 10000.0) {
            has_ts = 1;
            k_time = first_val;
            tok = strtok(NULL, ",");
        }

        /* Read measurement values */
        for (int j = 0; j < NM && tok; j++) {
            z_k[j] = atof(tok);
            tok = strtok(NULL, ",");
        }

        /* ---- Run one UKF step ---- */
        double x_out[NS];
        double rmse_val;
        if (ukf_step(&sp, &st, z_k, k_time, x_out, &rmse_val) != 0) {
            fprintf(stderr, "ERROR: UKF step failed at count=%d\n", count);
            continue;
        }

        /* ---- Output estimate ---- */
        printf("%.6f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
               k_time,
               x_out[0], x_out[1], x_out[2],
               x_out[3], x_out[4], x_out[5],
               rmse_val);
        fflush(stdout);

        count++;

        if (count % 10000 == 0)
            fprintf(stderr, "[Stream] Processed %d measurements (t=%.1fs)\n", count, k_time);
    }

    fprintf(stderr, "\n[Done] Processed %d measurements total.\n", count);
    return 0;
}
