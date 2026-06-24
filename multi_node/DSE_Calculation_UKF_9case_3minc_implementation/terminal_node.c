/**
 * terminal_node.c — Measurement Data Generation (C version)
 * IEEE 9-Bus 3-Generator System, 3-minute simulation
 *
 * Generates: system_params.bin, measurements.txt, true_states.csv
 * Compile:  gcc -O2 -o terminal_node terminal_node.c -llapack -lblas -lm
 * Run:      ./terminal_node
 */

#include "ukf_core.h"

/* ================================================================
 * Hard-coded 9-bus system data (Peter Sauer / Chow)
 * ================================================================ */

/* Branch data: fbus, tbus, r, x, b (9 branches) */
static const double branch[9][5] = {
    {1, 4, 0.0000, 0.0576, 0.000},
    {4, 6, 0.0170, 0.0920, 0.158},
    {6, 9, 0.0390, 0.1700, 0.358},
    {3, 9, 0.0000, 0.0586, 0.000},
    {9, 8, 0.0119, 0.1008, 0.209},
    {8, 7, 0.0085, 0.0720, 0.149},
    {7, 2, 0.0000, 0.0625, 0.000},
    {7, 5, 0.0320, 0.1610, 0.306},
    {5, 4, 0.0100, 0.0850, 0.176},
};

/* Bus load: Pd(MW), Qd(MVAr) */
static const double bus_load[9][2] = {
    {  0,  0}, {  0,  0}, {  0,  0},
    {  0,  0}, {125, 50}, { 90, 30},
    {  0,  0}, {100, 35}, {  0,  0},
};

/* Generator data: bus(1-idx), Pg, Qg, Vsp */
static const double gen_data[3][4] = {
    {1,  71.64,  27.05, 1.040},
    {2, 163.00,   6.65, 1.025},
    {3,  85.00, -10.86, 1.025},
};

/* Power flow solution: |V|, angle(deg) */
static const double pf_vmag[9]   = {1.040, 1.025, 1.025, 1.026, 0.996, 1.013, 1.026, 1.016, 1.032};
static const double pf_vang[9]   = {0.000, 9.280, 4.665, -2.217, -3.989, -3.687, 3.720, 0.728, 1.967};

/* Generator parameters */
static const double Xd[3] = {0.06080, 0.11980, 0.18130};
static const double H[3]  = {23.64,   6.40,    3.01};
static const double Damp[3] = {0.0255, 0.00663, 0.00265};
static const double Rgen[3] = {0.0, 0.0, 0.0};

static const double baseMVA = 100.0;
static const double f0 = 60.0;
static const double w_syn = 376.99111843077516; /* 2*pi*60 */

/* ================================================================
 * Build full Ybus matrix (n_bus x n_bus)
 * ================================================================ */
static void build_ybus(double complex *Y, int nb) {
    memset(Y, 0, nb * nb * sizeof(double complex));
    for (int i = 0; i < 9; i++) {
        int fb = (int)branch[i][0] - 1;
        int tb = (int)branch[i][1] - 1;
        double r = branch[i][2], x = branch[i][3], bc = branch[i][4];
        double complex ys = 1.0 / (r + I * x);
        double complex ysh = I * bc / 2.0;

        Y[fb * nb + fb] += ys + ysh;
        Y[tb * nb + tb] += ys + ysh;
        Y[fb * nb + tb] -= ys;
        Y[tb * nb + fb] -= ys;
    }
}

/* ================================================================
 * Dynamic system function: dx/dt = f(x)
 * ================================================================ */
static void dyn_system(const double *x, const double *M, const double *D,
                       const double complex *Ybusm, const double *E_abs,
                       const double *PM, int n, double *dx) {
    for (int i = 0; i < n; i++) {
        double complex Vg = E_abs[i] * cexp(I * x[i]);
        double complex Ibus = 0;
        for (int j = 0; j < n; j++)
            Ibus += Ybusm[i * n + j] * (E_abs[j] * cexp(I * x[j]));
        double complex S = conj(Ibus) * Vg;
        double Pe = creal(S);

        dx[i] = x[n + i];                  /* dδ/dt = ω */
        dx[n + i] = (PM[i] - Pe) / M[i] - D[i] * x[n + i] / M[i];
    }
}

/* ================================================================
 * Single RK4 step for true state propagation
 * ================================================================ */
static void rk4_step(const double *x, double dt, const double *M, const double *D,
                     const double complex *Ybusm, const double *E_abs,
                     const double *PM, int n, double *x_next) {
    int ns2 = 2 * n;
    double *k1 = vec_real(ns2), *k2 = vec_real(ns2);
    double *k3 = vec_real(ns2), *k4 = vec_real(ns2);
    double *tmp = vec_real(ns2);

    dyn_system(x, M, D, Ybusm, E_abs, PM, n, k1);

    for (int i = 0; i < ns2; i++) tmp[i] = x[i] + 0.5 * dt * k1[i];
    dyn_system(tmp, M, D, Ybusm, E_abs, PM, n, k2);

    for (int i = 0; i < ns2; i++) tmp[i] = x[i] + 0.5 * dt * k2[i];
    dyn_system(tmp, M, D, Ybusm, E_abs, PM, n, k3);

    for (int i = 0; i < ns2; i++) tmp[i] = x[i] + dt * k3[i];
    dyn_system(tmp, M, D, Ybusm, E_abs, PM, n, k4);

    for (int i = 0; i < ns2; i++)
        x_next[i] = x[i] + dt * (k1[i] + 2*k2[i] + 2*k3[i] + k4[i]) / 6.0;

    free(k1); free(k2); free(k3); free(k4); free(tmp);
}

/* ================================================================
 * Determine system state (pre-fault / during-fault / post-fault)
 * ================================================================ */
static int get_ps(double k, double tsw, double tfc) {
    if (k < tsw) return 0;
    if (k <= tfc) return 1;
    return 2;
}

/* ================================================================
 * Write binary system_params file
 * ================================================================ */
static void save_system_params_bin(const SystemParams *sp, const char *fname) {
    FILE *fp = fopen(fname, "wb");
    if (!fp) { fprintf(stderr, "ERROR: Cannot open %s\n", fname); return; }

    /* Write dimensions first */
    int dims[6] = {sp->n, sp->s, sp->ns, sp->nm, sp->fs, sp->num_samples};
    fwrite(dims, sizeof(int), 6, fp);

    double scalars[4] = {sp->deltt, sp->t_SW, sp->t_FC, (double)TOTAL_TIME};
    fwrite(scalars, sizeof(double), 4, fp);

    /* YBUS[3][n*n] */
    for (int ps = 0; ps < 3; ps++)
        fwrite(sp->YBUS[ps], sizeof(double complex), sp->n * sp->n, fp);

    /* RV[3][s*n] */
    for (int ps = 0; ps < 3; ps++)
        fwrite(sp->RV[ps], sizeof(double complex), sp->s * sp->n, fp);

    fwrite(sp->E_abs, sizeof(double), sp->n, fp);
    fwrite(sp->PM, sizeof(double), sp->n, fp);
    fwrite(sp->M, sizeof(double), sp->n, fp);
    fwrite(sp->D, sizeof(double), sp->n, fp);
    fwrite(sp->X_0, sizeof(double), sp->ns, fp);

    fclose(fp);
    printf("  Saved %s\n", fname);
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    printf("============================================================\n");
    printf("Terminal Node - Measurement Data Generation (C)\n");
    printf("IEEE 9-Bus 3-Generator, 3min\n");
    printf("============================================================\n\n");

    /* ---- Step 1: Build system matrices ---- */
    printf("[Step 1/3] Initializing System...\n");

    int n = N_GEN, s = N_BUS, ns = NS, nm = NM;
    double complex *Yfull = mat_cplx(s);
    build_ybus(Yfull, s);

    /* V = |V| * exp(j*θ) */
    double complex V[9];
    for (int i = 0; i < s; i++)
        V[i] = pf_vmag[i] * cexp(I * pf_vang[i] * M_PI / 180.0);

    /* M = 2H / w_syn */
    double M_g[3];
    for (int i = 0; i < n; i++) M_g[i] = 2.0 * H[i] / w_syn;

    /* Sg = (Pg + jQg) / baseMVA */
    double complex Sg[3];
    for (int i = 0; i < n; i++)
        Sg[i] = (gen_data[i][1] + I * gen_data[i][2]) / baseMVA;

    /* Y22 = diag(1/(j*Xd)) */
    double complex Y22[9] = {0}; /* 3x3 */
    for (int i = 0; i < n; i++) Y22[i * n + i] = 1.0 / (I * Xd[i]);

    /* YL = conj(SL) / |V|^2 */
    double complex YL[9] = {0};
    for (int i = 0; i < s; i++) {
        double complex SL = (bus_load[i][0] + I * bus_load[i][1]) / baseMVA;
        if (cabs(V[i]) > 1e-10)
            YL[i] = conj(SL) / (cabs(V[i]) * cabs(V[i]));
    }

    /* Y11 = Y + diag(YL), then add Y22 at generator buses */
    double complex *Y11 = mat_cplx(s);
    memcpy(Y11, Yfull, s * s * sizeof(double complex));
    for (int i = 0; i < s; i++) Y11[i * s + i] += YL[i];
    int gb[3];
    for (int i = 0; i < n; i++) {
        gb[i] = (int)gen_data[i][0] - 1;
        Y11[gb[i] * s + gb[i]] += Y22[i * n + i];
    }

    /* Y12 */
    double complex *Y12 = mat_cplx(s * n); /* s x n, allocated as s*n */
    memset(Y12, 0, s * n * sizeof(double complex));
    for (int i = 0; i < n; i++) {
        int q = gb[i];
        Y12[q * n + i] = -1.0 / (Rgen[i] + I * Xd[i]);
    }

    /* Y21 = Y12' */
    double complex *Y21 = mat_cplx(n * s);
    for (int i = 0; i < s; i++)
        for (int j = 0; j < n; j++)
            Y21[j * s + i] = conj(Y12[i * n + j]);

    /* Pre-fault: Ybf = Y22 - Y21 * inv(Y11) * Y12 */
    /* T = inv(Y11) * Y12 */
    double complex *Y11_copy = mat_cplx(s);
    memcpy(Y11_copy, Y11, s * s * sizeof(double complex));
    mat_inv_cplx(s, Y11_copy);
    double complex *T = mat_cplx(s * n);
    mmul_cplx(s, n, s, Y11_copy, Y12, T);

    /* RV(:,:,0) = -inv(Y11)*Y12 */
    double complex RV[3][N_BUS * N_GEN];
    memset(RV, 0, sizeof(RV));
    for (int i = 0; i < s * n; i++) RV[0][i] = -T[i];

    /* Ybf = Y22 - Y21 * T */
    double complex Ybf[9] = {0};
    {
        double complex *Y21T = mat_cplx(n * n);
        mmul_cplx(n, n, s, Y21, T, Y21T);
        for (int i = 0; i < n * n; i++) Ybf[i] = Y22[i] - Y21T[i];
        free(Y21T);
    }

    /* Fault config: Bus 8 fault, line 8-9 */
    int f11 = 7; /* 0-indexed bus 8 */
    int f1 = 7, f2 = 8; /* buses 8, 9 */

    /* During fault: remove bus 8 */
    double complex *Y11df = mat_cplx(s - 1);
    for (int i = 0, ii = 0; i < s; i++) {
        if (i == f11) continue;
        for (int j = 0, jj = 0; j < s; j++) {
            if (j == f11) continue;
            Y11df[ii * (s - 1) + jj] = Y11[i * s + j];
            jj++;
        }
        ii++;
    }
    double complex *Y12df = mat_cplx((s - 1) * n);
    for (int i = 0, ii = 0; i < s; i++) {
        if (i == f11) continue;
        for (int j = 0; j < n; j++)
            Y12df[ii * n + j] = Y12[i * n + j];
        ii++;
    }

    /* inv(Y11df) */
    double complex *Y11df_inv = mat_cplx(s - 1);
    memcpy(Y11df_inv, Y11df, (s-1)*(s-1)*sizeof(double complex));
    mat_inv_cplx(s - 1, Y11df_inv);

    /* RV_during = -inv(Y11df)*Y12df, mapped to correct rows */
    double complex *Tdf = mat_cplx((s - 1) * n);
    mmul_cplx(s - 1, n, s - 1, Y11df_inv, Y12df, Tdf);
    for (int i = 0, ri = 0; i < s; i++) {
        if (i == f11) continue;
        for (int j = 0; j < n; j++) RV[1][i * n + j] = -Tdf[ri * n + j];
        ri++;
    }

    /* Ydf = Y22 - Y21df * inv(Y11df) * Y12df */
    double complex *Y21df = mat_cplx(n * (s - 1));
    for (int i = 0, ii = 0; i < s; i++) {
        if (i == f11) continue;
        for (int j = 0; j < n; j++) Y21df[j * (s - 1) + ii] = conj(Y12[i * n + j]);
        ii++;
    }
    double complex Ydf[9] = {0};
    {
        double complex *tmp_nn = mat_cplx(n * n);
        mmul_cplx(n, n, s - 1, Y21df, Tdf, tmp_nn);
        for (int i = 0; i < n * n; i++) Ydf[i] = Y22[i] - tmp_nn[i];
        free(tmp_nn);
    }

    /* Post-fault: remove line 8-9 */
    double complex *Y11af = mat_cplx(s);
    memcpy(Y11af, Y11, s * s * sizeof(double complex));
    Y11af[f1 * s + f2] = 0;
    Y11af[f2 * s + f1] = 0;
    /* Find branch 8-9 and subtract its admittance */
    for (int i = 0; i < 9; i++) {
        int fb = (int)branch[i][0] - 1, tb = (int)branch[i][1] - 1;
        if ((fb == f1 && tb == f2) || (fb == f2 && tb == f1)) {
            double r = branch[i][2], x = branch[i][3], bc = branch[i][4];
            double complex y = 1.0 / (r + I * x);
            Y11af[f1 * s + f1] -= I * bc / 2.0 + y;
            Y11af[f2 * s + f2] -= I * bc / 2.0 + y;
            break;
        }
    }

    double complex *Y11af_inv = mat_cplx(s);
    memcpy(Y11af_inv, Y11af, s * s * sizeof(double complex));
    mat_inv_cplx(s, Y11af_inv);

    double complex *Taf = mat_cplx(s * n);
    mmul_cplx(s, n, s, Y11af_inv, Y12, Taf);
    for (int i = 0; i < s * n; i++) RV[2][i] = -Taf[i];

    double complex Yaf[9] = {0};
    {
        double complex *tmp_nn = mat_cplx(n * n);
        mmul_cplx(n, n, s, Y21, Taf, tmp_nn);
        for (int i = 0; i < n * n; i++) Yaf[i] = Y22[i] - tmp_nn[i];
        free(tmp_nn);
    }

    /* Initial conditions */
    double complex Ig[3], E0[3];
    double E_abs[3];
    for (int i = 0; i < n; i++) {
        Ig[i] = conj(Sg[i] / V[gb[i]]);
        E0[i] = V[gb[i]] + Ig[i] * (Rgen[i] + I * Xd[i]);
        E_abs[i] = cabs(E0[i]);
    }

    double PM[3];
    {
        /* I0 = Ybf * E0 (matrix-vector multiply) */
        for (int i = 0; i < n; i++) {
            double complex sum = 0;
            for (int j = 0; j < n; j++) sum += Ybf[i * n + j] * E0[j];
            PM[i] = creal(E0[i] * conj(sum));
        }
    }

    double X_0[NS];
    for (int i = 0; i < n; i++) {
        X_0[i] = carg(E0[i]);
        X_0[n + i] = 0.0;
    }

    printf("  Buses: %d, Generators: %d\n", s, n);
    printf("  E_abs: [%.4f, %.4f, %.4f]\n", E_abs[0], E_abs[1], E_abs[2]);
    printf("  delta0 (deg): [%.3f, %.3f, %.3f]\n",
           X_0[0]*180/M_PI, X_0[1]*180/M_PI, X_0[2]*180/M_PI);

    /* Pack SystemParams */
    SystemParams sp;
    sp.n = n; sp.s = s; sp.ns = ns; sp.nm = nm;
    sp.fs = FS; sp.num_samples = NUM_SAMPLES;
    sp.deltt = DELTT; sp.t_SW = T_SW; sp.t_FC = T_FC;
    memcpy(sp.E_abs, E_abs, n * sizeof(double));
    memcpy(sp.PM, PM, n * sizeof(double));
    memcpy(sp.M, M_g, n * sizeof(double));
    memcpy(sp.D, Damp, n * sizeof(double));
    memcpy(sp.X_0, X_0, ns * sizeof(double));

    /* Copy YBUS: ps=0→Ybf, ps=1→Ydf, ps=2→Yaf */
    memcpy(sp.YBUS[0], Ybf, n * n * sizeof(double complex));
    memcpy(sp.YBUS[1], Ydf, n * n * sizeof(double complex));
    memcpy(sp.YBUS[2], Yaf, n * n * sizeof(double complex));

    /* Copy RV */
    memcpy(sp.RV[0], RV[0], s * n * sizeof(double complex));
    memcpy(sp.RV[1], RV[1], s * n * sizeof(double complex));
    memcpy(sp.RV[2], RV[2], s * n * sizeof(double complex));

    /* ---- Step 2: Generate measurements ---- */
    printf("\n[Step 2/3] Generating True Values and Measurements...\n");
    printf("  deltt=%.4fs, fs=%.0fHz, total=%.0fs, samples=%d\n",
           DELTT, FS, TOTAL_TIME, NUM_SAMPLES);

    double *X_true = ALLOC(ns * NUM_SAMPLES, double);
    double *Z_mes  = ALLOC(nm * NUM_SAMPLES, double);
    double X_sim[NS];
    memcpy(X_sim, X_0, ns * sizeof(double));

    for (int idx = 0; idx < NUM_SAMPLES; idx++) {
        double k = idx / FS;
        int ps = get_ps(k, T_SW, T_FC);

        /* Get reduced Ybus for current system state */
        double complex Ybusm[9];
        memcpy(Ybusm, sp.YBUS[ps], n * n * sizeof(double complex));

        /* True state propagation (RK4) */
        rk4_step(X_sim, DELTT, M_g, Damp, Ybusm, E_abs, PM, n, X_sim);
        memcpy(&X_true[idx * ns], X_sim, ns * sizeof(double));

        /* Compute measurements */
        double complex E[3], Ibus[3], Vbus[9];
        for (int i = 0; i < n; i++) E[i] = E_abs[i] * cexp(I * X_sim[i]);

        /* Ibus = Ybusm * E */
        memset(Ibus, 0, n * sizeof(double complex));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                Ibus[i] += Ybusm[i * n + j] * E[j];

        /* Vbus = RVm * E */
        memset(Vbus, 0, s * sizeof(double complex));
        for (int i = 0; i < s; i++)
            for (int j = 0; j < n; j++)
                Vbus[i] += sp.RV[ps][i * n + j] * E[j];

        /* PG */
        for (int i = 0; i < n; i++)
            Z_mes[idx * nm + i] = creal(E[i] * conj(Ibus[i]));
        /* QG */
        for (int i = 0; i < n; i++)
            Z_mes[idx * nm + n + i] = cimag(E[i] * conj(Ibus[i]));
        /* Re(V) */
        for (int i = 0; i < s; i++)
            Z_mes[idx * nm + 2*n + i] = creal(Vbus[i]);
        /* Im(V) */
        for (int i = 0; i < s; i++)
            Z_mes[idx * nm + 2*n + s + i] = cimag(Vbus[i]);

        if ((idx + 1) % 50000 == 0)
            printf("  %.0f%% complete (%.1fs)\n", (idx+1)*100.0/NUM_SAMPLES, (idx+1)/FS);
    }

    /* ---- Step 3: Save files ---- */
    printf("\n[Step 3/3] Saving Files...\n");

    save_system_params_bin(&sp, "system_params.bin");

    /* true_states.csv */
    {
        FILE *fp = fopen("true_states.csv", "w");
        fprintf(fp, "delta1,delta2,delta3,omega1,omega2,omega3\n");
        for (int i = 0; i < NUM_SAMPLES; i++) {
            double *row = &X_true[i * ns];
            fprintf(fp, "%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
                    row[0], row[1], row[2], row[3], row[4], row[5]);
        }
        fclose(fp);
        printf("  Saved true_states.csv\n");
    }

    /* measurements.txt (rounded to 4dp) */
    {
        FILE *fp = fopen("measurements.txt", "w");
        fprintf(fp, "timestamp,PG1,PG2,PG3,QG1,QG2,QG3,"
                "Vreal1,Vreal2,Vreal3,Vreal4,Vreal5,Vreal6,Vreal7,Vreal8,Vreal9,"
                "Vimag1,Vimag2,Vimag3,Vimag4,Vimag5,Vimag6,Vimag7,Vimag8,Vimag9\n");
        for (int i = 0; i < NUM_SAMPLES; i++) {
            double t = i / FS;
            fprintf(fp, "%.6f", t);
            double *row = &Z_mes[i * nm];
            for (int j = 0; j < nm; j++)
                fprintf(fp, ",%.4f", round(row[j] * 10000.0) / 10000.0);
            fprintf(fp, "\n");
            if ((i + 1) % 50000 == 0)
                printf("  Writing: %.0f%%\n", (i+1)*100.0/NUM_SAMPLES);
        }
        fclose(fp);
        printf("  Saved measurements.txt (%d rows, 4dp)\n", NUM_SAMPLES);
    }

    /* Cleanup */
    free(Yfull); free(Y11); free(Y12); free(Y21);
    free(Y11_copy); free(T); free(Y11df); free(Y12df);
    free(Y11df_inv); free(Tdf); free(Y21df);
    free(Y11af); free(Y11af_inv); free(Taf);
    free(X_true); free(Z_mes);

    printf("\n============================================================\n");
    printf("Terminal node complete!\n");
    printf("Files: system_params.bin, measurements.txt, true_states.csv\n");
    printf("============================================================\n");
    return 0;
}
