/*
 * ukf_c.c — C语言 UKF 状态估计器实现
 * =====================================
 * 与 Python ukf_estimation_5.py + RK4.py + dynamic_system.py 逐行对应。
 * 双精度 (double), 行主序存储, 与 Python float64 精度一致。
 */

#include "ukf_c.h"

/* ========================================================================
 * 线性代数基础
 * ======================================================================== */

/* C = A * B,  A: m×k, B: k×n, C: m×n (全部行主序) */
void mat_mul(int m, int n, int k, const double *A, const double *B, double *C) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            double s = 0;
            for (int l = 0; l < k; l++) s += A[i*k + l] * B[l*n + j];
            C[i*n + j] = s;
        }
    }
}

/* 矩阵转置: AT = A^T, A: m×n, AT: n×m */
void mat_transpose(int m, int n, const double *A, double *AT) {
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            AT[j*m + i] = A[i*n + j];
}

/* Cholesky LL^T 分解, A: n×n 正定对称 (行主序), 下三角存入 L (仅左下, 行主序) */
int mat_cholesky(int n, const double *A, double *L) {
    memset(L, 0, n * n * sizeof(double));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double s = A[i*n + j];
            for (int k = 0; k < j; k++) s -= L[i*n + k] * L[j*n + k];
            if (i == j) {
                if (s <= 1e-15) return -1;  /* 非正定 */
                L[i*n + i] = sqrt(s);
            } else {
                L[i*n + j] = s / L[j*n + j];
            }
        }
    }
    return 0;
}

/* 求解 L * x = b (L: n×n 下三角), x 覆盖 b */
static void forward_sub(int n, const double *L, const double *b, double *x) {
    for (int i = 0; i < n; i++) {
        double s = b[i];
        for (int j = 0; j < i; j++) s -= L[i*n + j] * x[j];
        x[i] = s / L[i*n + i];
    }
}

/* 求解 L^T * x = b (L: n×n 下三角), x 覆盖 b */
static void back_sub(int n, const double *L, const double *b, double *x) {
    for (int i = n - 1; i >= 0; i--) {
        double s = b[i];
        for (int j = i + 1; j < n; j++) s -= L[j*n + i] * x[j];
        x[i] = s / L[i*n + i];
    }
}

/* 求解 L*L^T * x = b */
void mat_cholesky_solve(int n, const double *L, const double *b, double *x) {
    double y[20];  /* max n=14 for measurement Pz */
    forward_sub(n, L, b, y);
    back_sub(n, L, y, x);
}

/* 求解 A * X = B (A: n×n, B: n×m), 结果在 X (n×m) */
int mat_solve(int n, int m, const double *A, const double *B, double *X) {
    double L[256];  /* max n=14, so 14*14=196 < 256 */
    if (mat_cholesky(n, A, L) != 0) return -1;
    double b[20], x[20];
    for (int j = 0; j < m; j++) {
        for (int i = 0; i < n; i++) b[i] = B[i*m + j];
        mat_cholesky_solve(n, L, b, x);
        for (int i = 0; i < n; i++) X[i*m + j] = x[i];
    }
    return 0;
}

/* ========================================================================
 * 复数矩阵运算 (2-gen 系统专用, 与 dynamic_system.py 对应)
 * ======================================================================== */

/* I = Ybusm * E  (2×2 @ 2×1 复矩阵乘向量) */
static void cmat_mul_vec_2x2(const CMat2x2 *Y,
    const double Er[2], const double Ei[2],
    double Ir[2], double Ii[2]) {
    for (int i = 0; i < 2; i++) {
        Ir[i] = Ii[i] = 0;
        for (int j = 0; j < 2; j++) {
            Ir[i] += Y->real[i][j] * Er[j] - Y->imag[i][j] * Ei[j];
            Ii[i] += Y->real[i][j] * Ei[j] + Y->imag[i][j] * Er[j];
        }
    }
}

/* Vbus = RVm * E  (5×2 @ 2×1 复矩阵乘向量) */
static void cmat_mul_vec_5x2(const CMat5x2 *RV,
    const double Er[2], const double Ei[2],
    double Vr[5], double Vi[5]) {
    for (int i = 0; i < 5; i++) {
        Vr[i] = Vi[i] = 0;
        for (int j = 0; j < 2; j++) {
            Vr[i] += RV->real[i][j] * Er[j] - RV->imag[i][j] * Ei[j];
            Vi[i] += RV->real[i][j] * Ei[j] + RV->imag[i][j] * Er[j];
        }
    }
}

/* ========================================================================
 * RK4 矢量传播 — 与 Python RK4.py rk4() 逐行对应
 *
 * 输入: X_sigma[8][4]  (NS × 2*NS sigma points, 按列主序: 点索引×状态)
 * 输出: X_breve[8][4]  (传播后的 sigma points)
 * ======================================================================== */
void ukf_rk4_propagate(const UKFParams *sp, int ps,
    const double X_sigma[8][4], double X_breve[8][4]) {
    const CMat2x2 *Ym = &sp->YBUS[ps];
    const double *Ea = sp->E_abs;
    const double *PM = sp->PM, *M = sp->M, *D = sp->D;
    double dt = UKF_DT;
    int ns = UKF_NS, np = UKF_N_SIGMA;
    int n  = UKF_N_GEN;

    /* Stage 1 */
    double k1_delta[8][2], k1_w[8][2];
    for (int p = 0; p < np; p++) {
        double Er[2], Ei[2], Ir[2], Ii[2];
        for (int i = 0; i < n; i++) {
            Er[i] = Ea[i] * cos(X_sigma[p][i]);
            Ei[i] = Ea[i] * sin(X_sigma[p][i]);
        }
        cmat_mul_vec_2x2(Ym, Er, Ei, Ir, Ii);
        for (int i = 0; i < n; i++) {
            double PG = Er[i] * Ir[i] + Ei[i] * Ii[i];
            k1_w[p][i]   = dt * (PM[i] - PG - D[i] * X_sigma[p][n+i]) / M[i];
            k1_delta[p][i] = dt * X_sigma[p][n+i];
        }
    }

    /* Stage 2 */
    double k2_delta[8][2], k2_w[8][2];
    for (int p = 0; p < np; p++) {
        double Er[2], Ei[2], Ir[2], Ii[2];
        for (int i = 0; i < n; i++) {
            Er[i] = Ea[i] * cos(X_sigma[p][i] + 0.5 * k1_delta[p][i]);
            Ei[i] = Ea[i] * sin(X_sigma[p][i] + 0.5 * k1_delta[p][i]);
        }
        cmat_mul_vec_2x2(Ym, Er, Ei, Ir, Ii);
        for (int i = 0; i < n; i++) {
            double PG = Er[i] * Ir[i] + Ei[i] * Ii[i];
            k2_w[p][i]   = dt * (PM[i] - PG - D[i] * (X_sigma[p][n+i] + 0.5*k1_w[p][i])) / M[i];
            k2_delta[p][i] = dt * (X_sigma[p][n+i] + 0.5 * k1_w[p][i]);
        }
    }

    /* Stage 3 */
    double k3_delta[8][2], k3_w[8][2];
    for (int p = 0; p < np; p++) {
        double Er[2], Ei[2], Ir[2], Ii[2];
        for (int i = 0; i < n; i++) {
            Er[i] = Ea[i] * cos(X_sigma[p][i] + 0.5 * k2_delta[p][i]);
            Ei[i] = Ea[i] * sin(X_sigma[p][i] + 0.5 * k2_delta[p][i]);
        }
        cmat_mul_vec_2x2(Ym, Er, Ei, Ir, Ii);
        for (int i = 0; i < n; i++) {
            double PG = Er[i] * Ir[i] + Ei[i] * Ii[i];
            k3_w[p][i]   = dt * (PM[i] - PG - D[i] * (X_sigma[p][n+i] + 0.5*k2_w[p][i])) / M[i];
            k3_delta[p][i] = dt * (X_sigma[p][n+i] + 0.5 * k2_w[p][i]);
        }
    }

    /* Stage 4 */
    for (int p = 0; p < np; p++) {
        double Er[2], Ei[2], Ir[2], Ii[2];
        for (int i = 0; i < n; i++) {
            Er[i] = Ea[i] * cos(X_sigma[p][i] + k3_delta[p][i]);
            Ei[i] = Ea[i] * sin(X_sigma[p][i] + k3_delta[p][i]);
        }
        cmat_mul_vec_2x2(Ym, Er, Ei, Ir, Ii);
        for (int i = 0; i < n; i++) {
            double PG = Er[i] * Ir[i] + Ei[i] * Ii[i];
            double k4_w = dt * (PM[i] - PG - D[i] * (X_sigma[p][n+i] + k3_w[p][i])) / M[i];
            double k4_delta = dt * (X_sigma[p][n+i] + k3_w[p][i]);
            X_breve[p][i]     = X_sigma[p][i]     + (k1_delta[p][i] + 2*k2_delta[p][i] + 2*k3_delta[p][i] + k4_delta) / 6.0;
            X_breve[p][n+i]   = X_sigma[p][n+i]   + (k1_w[p][i]     + 2*k2_w[p][i]     + 2*k3_w[p][i]     + k4_w) / 6.0;
        }
    }
}

/* ========================================================================
 * 测量函数 — 与 Python h(x) 对应
 *
 * Sigma points [np × ns] → 测量预测 [nm × np]
 * ======================================================================== */
void ukf_measurement(const UKFParams *sp, int ps,
    const double X_sigma[8][4], double Z_breve[14][8]) {
    const CMat2x2 *Ym = &sp->YBUS[ps];
    const CMat5x2 *Rm = &sp->RV[ps];
    const double *Ea = sp->E_abs;
    int n = UKF_N_GEN, s = UKF_N_BUS, np = UKF_N_SIGMA;

    for (int p = 0; p < np; p++) {
        double Er[2], Ei[2], Ir[2], Ii[2], Vr[5], Vi[5];
        for (int i = 0; i < n; i++) {
            Er[i] = Ea[i] * cos(X_sigma[p][i]);
            Ei[i] = Ea[i] * sin(X_sigma[p][i]);
        }
        cmat_mul_vec_2x2(Ym, Er, Ei, Ir, Ii);
        for (int i = 0; i < n; i++) {
            Z_breve[i][p]      = Er[i] * Ir[i] + Ei[i] * Ii[i];  /* PG */
            Z_breve[n+i][p]    = Ei[i] * Ir[i] - Er[i] * Ii[i];  /* QG */
        }
        cmat_mul_vec_5x2(Rm, Er, Ei, Vr, Vi);
        for (int i = 0; i < s; i++) {
            Z_breve[2*n+i][p]     = sqrt(Vr[i]*Vr[i] + Vi[i]*Vi[i]);  /* Vmag */
            Z_breve[2*n+s+i][p]   = atan2(Vi[i], Vr[i]);              /* Vangle */
        }
    }
}

/* ========================================================================
 * UKF 单步 — 与 Python ukf_estimation_5.py 逐行对应
 * ======================================================================== */
int ukf_step(UKFState *s, const UKFParams *sp, const double Z_meas[14], double t) {
    int ns = UKF_NS, nm = UKF_NM, np = UKF_N_SIGMA, n = UKF_N_GEN;
    int ps = ukf_fault_state(t);

    /* ── Prediction Step ── */
    /* 1. Cholesky: ns*P = R^T * R, 需要 R = chol(ns*P)^T (上三角的转置=下三角) */
    double P_scaled[16], L[16];
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            P_scaled[i*ns + j] = ns * s->P[i][j];

    if (mat_cholesky(ns, P_scaled, L) != 0) {
        /* 正则化 */
        double reg[16];
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < ns; j++)
                reg[i*ns + j] = (i == j) ? (ns * s->P[i][j] + 1e-8) : (ns * s->P[i][j]);
        if (mat_cholesky(ns, reg, L) != 0) return -1;
    }

    /* L 是 chol(P_scaled) 下三角, Python: root = chol(ns*P).T = upper tri
     * 我们用 L^T 代替 root (对 sigma 点构建效果等价, 因为只用 root 的列) */
    double root[16];
    mat_transpose(ns, ns, L, root);  /* root = L^T, 上三角 */

    /* 2. Sigma 点: X_sigma = [X_hat + root, X_hat - root]  (ns × 2*ns) */
    double X_sigma[8][4];  /* np × ns */
    for (int j = 0; j < np; j++) {
        int sign = (j < ns) ? 1 : -1;
        int col  = (j < ns) ? j : (j - ns);
        for (int i = 0; i < ns; i++) {
            X_sigma[j][i] = s->X_hat[i] + sign * root[i*ns + col];  /* column col of root */
        }
    }

    /* 3. 传播 sigma 点 */
    double X_breve[8][4];
    ukf_rk4_propagate(sp, ps, X_sigma, X_breve);

    /* 4. 预测均值 X_hat = sum(X_breve * W) */
    for (int i = 0; i < ns; i++) {
        s->X_hat[i] = 0;
        for (int p = 0; p < np; p++) s->X_hat[i] += X_breve[p][i] * s->W[p];
    }

    /* 5. 预测协方差 P = (X_breve - X_hat)*(X_breve - X_hat)^T / (2*ns) + Q */
    double X_diff[8][4];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < ns; i++)
            X_diff[p][i] = X_breve[p][i] - s->X_hat[i];

    /* P = X_diff^T @ X_diff * (1/(2*ns)) + Q */
    for (int i = 0; i < ns; i++) {
        for (int j = 0; j < ns; j++) {
            double s_val = 0;
            for (int p = 0; p < np; p++) s_val += X_diff[p][i] * X_diff[p][j];
            s->P[i][j] = s_val / (2.0 * ns) + ((i == j) ? UKF_SIGMA*UKF_SIGMA : 0);
        }
    }

    /* ── Update Step ── */
    /* 6. Cholesky of updated P */
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            P_scaled[i*ns + j] = ns * s->P[i][j];

    if (mat_cholesky(ns, P_scaled, L) != 0) {
        for (int i = 0; i < ns; i++)
            for (int j = 0; j < ns; j++)
                P_scaled[i*ns + j] = ns * s->P[i][j] + ((i == j) ? 1e-8 : 0);
        if (mat_cholesky(ns, P_scaled, L) != 0) return -1;
    }
    mat_transpose(ns, ns, L, root);

    /* 7. New sigma points */
    double X_sigma2[8][4];
    for (int j = 0; j < np; j++) {
        int sign = (j < ns) ? 1 : -1, col = (j < ns) ? j : (j - ns);
        for (int i = 0; i < ns; i++)
            X_sigma2[j][i] = s->X_hat[i] + sign * root[col*ns + i];
    }

    /* 8. 测量预测 */
    double Z_breve[14][8];
    ukf_measurement(sp, ps, X_sigma2, Z_breve);

    /* 9. 测量均值 */
    double Z_hat[14] = {0};
    for (int i = 0; i < nm; i++)
        for (int p = 0; p < np; p++)
            Z_hat[i] += Z_breve[i][p] * s->W[p];

    /* 10. Innovation covariance Pz */
    double Z_diff[14][8];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < nm; i++)
            Z_diff[i][p] = Z_breve[i][p] - Z_hat[i];

    double Pz[14][14] = {{0}};
    for (int i = 0; i < nm; i++)
        for (int j = 0; j < nm; j++) {
            double s_val = 0;
            for (int p = 0; p < np; p++) s_val += Z_diff[i][p] * Z_diff[j][p];
            Pz[i][j] = s_val / (2.0 * ns) + ((i == j) ? UKF_SIGMA*UKF_SIGMA : 0);
        }

    /* 11. Cross-covariance Pxz */
    double X_diff2[8][4];
    for (int p = 0; p < np; p++)
        for (int i = 0; i < ns; i++)
            X_diff2[p][i] = X_sigma2[p][i] - s->X_hat[i];

    double Pxz[4][14] = {{0}};
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < nm; j++) {
            double s_val = 0;
            for (int p = 0; p < np; p++) s_val += X_diff2[p][i] * Z_diff[j][p];
            Pxz[i][j] = s_val / (2.0 * ns);
        }

    /* 12. Kalman gain: K = Pxz @ inv(Pz)  → solve Pz^T * K^T = Pxz^T */
    double PzT[14*14], PxzT[14*4], KT[14*4];
    mat_transpose(nm, nm, (double*)Pz,  PzT);
    mat_transpose(ns, nm, (double*)Pxz, PxzT);
    if (mat_solve(nm, ns, PzT, PxzT, KT) != 0) return -1;

    double K[4][14];
    mat_transpose(nm, ns, KT, (double*)K);  /* K = KT^T */

    /* 13. State update: X_hat += K @ (Z_meas - Z_hat) */
    double innovation[14];
    for (int i = 0; i < nm; i++) innovation[i] = Z_meas[i] - Z_hat[i];

    for (int i = 0; i < ns; i++) {
        double update = 0;
        for (int j = 0; j < nm; j++) update += K[i][j] * innovation[j];
        s->X_hat[i] += update;
    }

    /* 14. Covariance update: P = P - K * Pz * K^T */
    /* K: ns×nm, Pz: nm×nm */
    /* Step 1: KPz = K * Pz  (ns×nm) @ (nm×nm) = (ns×nm) */
    double KPz[4][14];
    mat_mul(ns, nm, nm, (double*)K, (double*)Pz, (double*)KPz);
    /* Step 2: K^T = transpose(K), (nm×ns) */
    double Ktrans[14][4];
    for (int i = 0; i < nm; i++)
        for (int j = 0; j < ns; j++)
            Ktrans[i][j] = K[j][i];
    /* Step 3: KPzKT = KPz * K^T  (ns×nm) @ (nm×ns) = (ns×ns) */
    double KPzKT[4][4];
    mat_mul(ns, ns, nm, (double*)KPz, (double*)Ktrans, (double*)KPzKT);

    for (int i = 0; i < ns; i++)
        for (int j = 0; j < ns; j++)
            s->P[i][j] -= KPzKT[i][j];

    s->step_count++;
    s->current_t = t;
    return 0;
}

/* ========================================================================
 * 初始化 UKF
 * ======================================================================== */
void ukf_init(UKFState *s, const UKFParams *sp) {
    double sig2 = UKF_SIGMA * UKF_SIGMA;
    memset(s, 0, sizeof(*s));
    memcpy(s->X_hat, sp->X0, 4 * sizeof(double));
    for (int i = 0; i < UKF_NS; i++) s->P[i][i] = sig2;
    for (int i = 0; i < UKF_NS; i++) s->Q[i][i] = sig2;
    for (int i = 0; i < UKF_NM; i++) s->R[i][i] = sig2;
    for (int i = 0; i < UKF_N_SIGMA; i++) s->W[i] = 1.0 / UKF_N_SIGMA;
}
