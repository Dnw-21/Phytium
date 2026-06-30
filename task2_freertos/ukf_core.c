/*
 * ukf_core.c — 泛化 UKF 实现 (支持 2-gen 到 10-gen)
 * ===================================================
 * 核心线性代数 + RK4传播 + 测量函数 + UKF步骤。
 * 通过 n_gen 参数切换系统规模, 无需重新编译。
 */

#include "ukf_core.h"

/* ========================================================================
 * 线性代数 (与 ukf_c.c 相同, 增大缓冲区)
 * ======================================================================== */
void mat_mul(int m, int n, int k, const double *A, const double *B, double *C) {
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            double s = 0;
            for (int l = 0; l < k; l++) s += A[i*k + l] * B[l*n + j];
            C[i*n + j] = s;
        }
}

void mat_transpose(int m, int n, const double *A, double *AT) {
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            AT[j*m + i] = A[i*n + j];
}

int mat_cholesky(int n, const double *A, double *L) {
    memset(L, 0, n * n * sizeof(double));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double s = A[i*n + j];
            for (int k = 0; k < j; k++) s -= L[i*n + k] * L[j*n + k];
            if (i == j) {
                if (s <= 1e-15) return -1;
                L[i*n + i] = sqrt(s);
            } else {
                L[i*n + j] = s / L[j*n + j];
            }
        }
    }
    return 0;
}

static void forward_sub(int n, const double *L, const double *b, double *x) {
    for (int i = 0; i < n; i++) {
        double s = b[i];
        for (int j = 0; j < i; j++) s -= L[i*n + j] * x[j];
        x[i] = s / L[i*n + i];
    }
}
static void back_sub(int n, const double *L, const double *b, double *x) {
    for (int i = n - 1; i >= 0; i--) {
        double s = b[i];
        for (int j = i + 1; j < n; j++) s -= L[j*n + i] * x[j];
        x[i] = s / L[i*n + i];
    }
}

void mat_cholesky_solve(int n, const double *L, const double *b, double *x) {
    double y[UKF_MAX_NM];
    forward_sub(n, L, b, y);
    back_sub(n, L, y, x);
}

int mat_solve(int n, int m, const double *A, const double *B, double *X) {
    double L[UKF_MAX_NM * UKF_MAX_NM];
    if (mat_cholesky(n, A, L) != 0) return -1;
    double b[UKF_MAX_NM], x[UKF_MAX_NM];
    for (int j = 0; j < m; j++) {
        for (int i = 0; i < n; i++) b[i] = B[i*m + j];
        mat_cholesky_solve(n, L, b, x);
        for (int i = 0; i < n; i++) X[i*m + j] = x[i];
    }
    return 0;
}

/* ========================================================================
 * 复数矩阵 × 向量 (泛化, 支持 n_gen×n_gen @ n_gen×1)
 * ======================================================================== */
static void cmat_mul_vec(int ng, const double *Yr, const double *Yi,
                          const double *Er, const double *Ei, double *Ir, double *Ii) {
    for (int i = 0; i < ng; i++) {
        Ir[i] = Ii[i] = 0;
        for (int j = 0; j < ng; j++) {
            double a = Yr[i*ng + j], b = Yi[i*ng + j];
            double c = Er[j], d = Ei[j];
            Ir[i] += a*c - b*d;
            Ii[i] += a*d + b*c;
        }
    }
}

/* RVm @ E: [n_bus × n_gen] @ [n_gen × 1] */
static void cmat_mul_vec_rv(int nb, int ng, const double *Rr, const double *Ri,
                             const double *Er, const double *Ei, double *Vr, double *Vi) {
    for (int i = 0; i < nb; i++) {
        Vr[i] = Vi[i] = 0;
        for (int j = 0; j < ng; j++) {
            double a = Rr[i*ng + j], b = Ri[i*ng + j];
            double c = Er[j], d = Ei[j];
            Vr[i] += a*c - b*d;
            Vi[i] += a*d + b*c;
        }
    }
}

/* ========================================================================
 * RK4 矢量传播 (泛化)
 * ======================================================================== */
static void ukf_rk4_propagate(const UKFParams *sp, int ps,
    const double *X_sigma, double *X_breve, int np) {
    int ng = sp->n_gen, ns = sp->ns;
    double dt = sp->dt;

    /* 提取当前故障状态的 YBUS */
    double Yr[UKF_MAX_GEN][UKF_MAX_GEN], Yi[UKF_MAX_GEN][UKF_MAX_GEN];
    for (int i = 0; i < ng; i++)
        for (int j = 0; j < ng; j++) {
            Yr[i][j] = sp->YBUS_real[i][j][ps];
            Yi[i][j] = sp->YBUS_imag[i][j][ps];
        }

    /* Stage 1 */
    double k1_delta[UKF_MAX_SIGMA][UKF_MAX_GEN], k1_w[UKF_MAX_SIGMA][UKF_MAX_GEN];
    for (int p = 0; p < np; p++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[p*ns + i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[p*ns + i]);
        }
        cmat_mul_vec(ng, (double*)Yr, (double*)Yi, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            k1_w[p][i]     = dt * (sp->PM[i] - PG - sp->D[i] * X_sigma[p*ns + ng + i]) / sp->M[i];
            k1_delta[p][i] = dt * X_sigma[p*ns + ng + i];
        }
    }

    /* Stage 2 */
    double k2_delta[UKF_MAX_SIGMA][UKF_MAX_GEN], k2_w[UKF_MAX_SIGMA][UKF_MAX_GEN];
    for (int p = 0; p < np; p++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[p*ns + i] + 0.5*k1_delta[p][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[p*ns + i] + 0.5*k1_delta[p][i]);
        }
        cmat_mul_vec(ng, (double*)Yr, (double*)Yi, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            k2_w[p][i]     = dt * (sp->PM[i] - PG - sp->D[i]*(X_sigma[p*ns+ng+i]+0.5*k1_w[p][i])) / sp->M[i];
            k2_delta[p][i] = dt * (X_sigma[p*ns + ng + i] + 0.5*k1_w[p][i]);
        }
    }

    /* Stage 3 */
    double k3_delta[UKF_MAX_SIGMA][UKF_MAX_GEN], k3_w[UKF_MAX_SIGMA][UKF_MAX_GEN];
    for (int p = 0; p < np; p++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[p*ns + i] + 0.5*k2_delta[p][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[p*ns + i] + 0.5*k2_delta[p][i]);
        }
        cmat_mul_vec(ng, (double*)Yr, (double*)Yi, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            k3_w[p][i]     = dt * (sp->PM[i] - PG - sp->D[i]*(X_sigma[p*ns+ng+i]+0.5*k2_w[p][i])) / sp->M[i];
            k3_delta[p][i] = dt * (X_sigma[p*ns + ng + i] + 0.5*k2_w[p][i]);
        }
    }

    /* Stage 4 + final */
    for (int p = 0; p < np; p++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[p*ns + i] + k3_delta[p][i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[p*ns + i] + k3_delta[p][i]);
        }
        cmat_mul_vec(ng, (double*)Yr, (double*)Yi, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            double PG = Er[i]*Ir[i] + Ei[i]*Ii[i];
            double k4_w = dt * (sp->PM[i] - PG - sp->D[i]*(X_sigma[p*ns+ng+i]+k3_w[p][i])) / sp->M[i];
            double k4_delta = dt * (X_sigma[p*ns + ng + i] + k3_w[p][i]);
            X_breve[p*ns + i]       = X_sigma[p*ns + i]       + (k1_delta[p][i] + 2*k2_delta[p][i] + 2*k3_delta[p][i] + k4_delta)/6.0;
            X_breve[p*ns + ng + i]  = X_sigma[p*ns + ng + i]   + (k1_w[p][i] + 2*k2_w[p][i] + 2*k3_w[p][i] + k4_w)/6.0;
        }
    }
}

/* ========================================================================
 * 测量函数 (泛化)
 * ======================================================================== */
static void ukf_measurement(const UKFParams *sp, int ps,
    const double *X_sigma, double *Z_breve, int np) {
    int ng = sp->n_gen, nb = sp->n_bus, ns = sp->ns;

    double Yr[UKF_MAX_GEN][UKF_MAX_GEN], Yi[UKF_MAX_GEN][UKF_MAX_GEN];
    double Rr[UKF_MAX_BUS][UKF_MAX_GEN], Ri[UKF_MAX_BUS][UKF_MAX_GEN];
    for (int i = 0; i < ng; i++)
        for (int j = 0; j < ng; j++) {
            Yr[i][j] = sp->YBUS_real[i][j][ps];
            Yi[i][j] = sp->YBUS_imag[i][j][ps];
        }
    for (int i = 0; i < nb; i++)
        for (int j = 0; j < ng; j++) {
            Rr[i][j] = sp->RV_real[i][j][ps];
            Ri[i][j] = sp->RV_imag[i][j][ps];
        }

    for (int p = 0; p < np; p++) {
        double Er[UKF_MAX_GEN], Ei[UKF_MAX_GEN], Ir[UKF_MAX_GEN], Ii[UKF_MAX_GEN];
        double Vr[UKF_MAX_BUS], Vi[UKF_MAX_BUS];
        for (int i = 0; i < ng; i++) {
            Er[i] = sp->E_abs[i] * cos(X_sigma[p*ns + i]);
            Ei[i] = sp->E_abs[i] * sin(X_sigma[p*ns + i]);
        }
        cmat_mul_vec(ng, (double*)Yr, (double*)Yi, Er, Ei, Ir, Ii);
        for (int i = 0; i < ng; i++) {
            Z_breve[p*sp->nm + i]           = Er[i]*Ir[i] + Ei[i]*Ii[i];  /* PG */
            Z_breve[p*sp->nm + ng + i]      = Ei[i]*Ir[i] - Er[i]*Ii[i];  /* QG */
        }
        cmat_mul_vec_rv(nb, ng, (double*)Rr, (double*)Ri, Er, Ei, Vr, Vi);
        if (sp->voltage_format == 1) {
            /* 9bus: Re/Im voltage */
            for (int i = 0; i < nb; i++) {
                Z_breve[p*sp->nm + 2*ng + i]        = Vr[i];
                Z_breve[p*sp->nm + 2*ng + nb + i]    = Vi[i];
            }
        } else {
            /* 5bus/39bus: Vmag + Vangle */
            for (int i = 0; i < nb; i++) {
                Z_breve[p*sp->nm + 2*ng + i]        = sqrt(Vr[i]*Vr[i] + Vi[i]*Vi[i]);  /* Vmag */
                Z_breve[p*sp->nm + 2*ng + nb + i]    = atan2(Vi[i], Vr[i]);             /* Vangle */
            }
        }
    }
}

/* ========================================================================
 * UKF 初始化
 * ======================================================================== */
void ukf_init(UKFState *s, const UKFParams *sp) {
    memset(s, 0, sizeof(*s));
    s->n_gen = sp->n_gen; s->n_bus = sp->n_bus;
    s->ns = sp->ns; s->nm = sp->nm; s->np = sp->np;
    memcpy(s->X_hat, sp->X0, sp->ns * sizeof(double));
    double sig2 = sp->sigma * sp->sigma;
    for (int i = 0; i < sp->ns; i++) s->P[i][i] = sig2;
    for (int i = 0; i < sp->np; i++) s->W[i] = 1.0 / sp->np;
}

/* ========================================================================
 * UKF 单步 (泛化)
 * ======================================================================== */
int ukf_step(UKFState *s, const UKFParams *sp, const double *Z_meas, double t) {
    int ns = s->ns, nm = s->nm, np = s->np, ng = s->n_gen;
    int ps = ukf_fault_state(sp, t);
    double sig2 = sp->sigma * sp->sigma;

    /* ── Prediction ── */
    /* Cholesky: ns * P */
    double P_scaled[UKF_MAX_NS * UKF_MAX_NS], L[UKF_MAX_NS * UKF_MAX_NS];
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            P_scaled[i*ns + j] = ns * s->P[i][j];

    if (mat_cholesky(ns, P_scaled, L) != 0) {
        for (int i = 0; i < ns; i++) P_scaled[i*ns + i] += 1e-8;
        if (mat_cholesky(ns, P_scaled, L) != 0) return -1;
    }

    double root[UKF_MAX_NS * UKF_MAX_NS];
    mat_transpose(ns, ns, L, root);  /* root = L^T (upper triangular) */

    /* Sigma points: ns × np, row-major: X_sigma[p][i] = X_sigma[p*ns + i] */
    double X_sigma[UKF_MAX_SIGMA * UKF_MAX_NS];
    for (int j = 0; j < np; j++) {
        int sign = (j < ns) ? 1 : -1;
        int col  = (j < ns) ? j : (j - ns);
        for (int i = 0; i < ns; i++)
            X_sigma[j*ns + i] = s->X_hat[i] + sign * root[i*ns + col];
    }

    /* Propagate */
    double X_breve[UKF_MAX_SIGMA * UKF_MAX_NS];
    ukf_rk4_propagate(sp, ps, X_sigma, X_breve, np);

    /* Predicted mean */
    for (int i = 0; i < ns; i++) {
        s->X_hat[i] = 0;
        for (int p = 0; p < np; p++) s->X_hat[i] += X_breve[p*ns + i] * s->W[p];
    }

    /* Predicted covariance */
    double X_diff[UKF_MAX_SIGMA * UKF_MAX_NS];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < ns; i++)
            X_diff[p*ns + i] = X_breve[p*ns + i] - s->X_hat[i];

    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++) {
            double sv = 0;
            for (int p = 0; p < np; p++) sv += X_diff[p*ns + i] * X_diff[p*ns + j];
            s->P[i][j] = sv / (2.0*ns) + ((i==j) ? sig2 : 0);
        }

    /* ── Update ── */
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            P_scaled[i*ns + j] = ns * s->P[i][j];

    if (mat_cholesky(ns, P_scaled, L) != 0) {
        for (int i = 0; i < ns; i++) P_scaled[i*ns + i] += 1e-8;
        if (mat_cholesky(ns, P_scaled, L) != 0) return -1;
    }
    mat_transpose(ns, ns, L, root);

    double X_sigma2[UKF_MAX_SIGMA * UKF_MAX_NS];
    for (int j = 0; j < np; j++) {
        int sign = (j < ns) ? 1 : -1, col = (j < ns) ? j : (j - ns);
        for (int i = 0; i < ns; i++)
            X_sigma2[j*ns + i] = s->X_hat[i] + sign * root[i*ns + col];
    }

    /* Measurement prediction */
    double Z_breve[UKF_MAX_NM * UKF_MAX_SIGMA];
    memset(Z_breve, 0, sizeof(Z_breve));
    ukf_measurement(sp, ps, X_sigma2, Z_breve, np);

    double Z_hat[UKF_MAX_NM] = {0};
    for (int i = 0; i < nm; i++)
        for (int p = 0; p < np; p++)
            Z_hat[i] += Z_breve[p*nm + i] * s->W[p];

    double Z_diff[UKF_MAX_NM * UKF_MAX_SIGMA];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < nm; i++)
            Z_diff[p*nm + i] = Z_breve[p*nm + i] - Z_hat[i];

    /* Pz */
    double Pz[UKF_MAX_NM * UKF_MAX_NM];
    for (int i = 0; i < nm; i++)
        for (int j = 0; j < nm; j++) {
            double sv = 0;
            for (int p = 0; p < np; p++) sv += Z_diff[p*nm + i] * Z_diff[p*nm + j];
            Pz[i*nm + j] = sv / (2.0*ns) + ((i==j) ? sig2 : 0);
        }

    /* Pxz */
    double X_diff2[UKF_MAX_SIGMA * UKF_MAX_NS];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < ns; i++)
            X_diff2[p*ns + i] = X_sigma2[p*ns + i] - s->X_hat[i];

    double Pxz[UKF_MAX_NS * UKF_MAX_NM];
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < nm; j++) {
            double sv = 0;
            for (int p = 0; p < np; p++) sv += X_diff2[p*ns + i] * Z_diff[p*nm + j];
            Pxz[i*nm + j] = sv / (2.0*ns);
        }

    /* Kalman gain */
    double PzT[UKF_MAX_NM * UKF_MAX_NM], PxzT[UKF_MAX_NM * UKF_MAX_NS], KT[UKF_MAX_NM * UKF_MAX_NS];
    mat_transpose(nm, nm, Pz, PzT);
    mat_transpose(ns, nm, Pxz, PxzT);
    if (mat_solve(nm, ns, PzT, PxzT, KT) != 0) return -1;

    double K[UKF_MAX_NS * UKF_MAX_NM];
    mat_transpose(nm, ns, KT, K);

    /* State update */
    double innovation[UKF_MAX_NM];
    for (int i = 0; i < nm; i++) innovation[i] = Z_meas[i] - Z_hat[i];
    for (int i = 0; i < ns; i++) {
        double upd = 0;
        for (int j = 0; j < nm; j++) upd += K[i*nm + j] * innovation[j];
        s->X_hat[i] += upd;
    }

    /* Covariance update: P -= K*Pz*K^T */
    double KPz[UKF_MAX_NS * UKF_MAX_NM], Ktrans[UKF_MAX_NM * UKF_MAX_NS], KPzKT[UKF_MAX_NS * UKF_MAX_NS];
    mat_mul(ns, nm, nm, K, Pz, KPz);
    for (int i = 0; i < nm; i++)
        for (int j = 0; j < ns; j++)
            Ktrans[i*ns + j] = K[j*nm + i];
    mat_mul(ns, ns, nm, KPz, Ktrans, KPzKT);
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            s->P[i][j] -= KPzKT[i*ns + j];

    s->step_count++;
    s->current_t = t;
    return 0;
}
