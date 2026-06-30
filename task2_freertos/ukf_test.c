/* ukf_test.c — C UKF vs Python 交叉验证 */
#include "ukf_c.h"
#include "/tmp/ukf_test_params.h"

/* Build UKFParams from defines */
static void build_params(UKFParams *sp) {
    memset(sp, 0, sizeof(*sp));
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                /* Need macro magic: use preprocessor token pasting */
                /* This gets complex, use direct indexing from defines */
            }
        }
    }
    sp->E_abs[0] = E_ABS_0; sp->E_abs[1] = E_ABS_1;
    sp->PM[0] = PM_0; sp->PM[1] = PM_1;
    sp->M[0] = M_0; sp->M[1] = M_1;
    sp->D[0] = D_0; sp->D[1] = D_1;
    sp->X0[0] = X0_0; sp->X0[1] = X0_1; sp->X0[2] = X0_2; sp->X0[3] = X0_3;
}

#include <stdio.h>

/* Use include tricks to initialize YBUS/RV from defines */
#define YR(i,j,k) YBUS_REAL_##i##_##j##_##k
#define YI(i,j,k) YBUS_IMAG_##i##_##j##_##k
#define RR(i,j,k) RV_REAL_##i##_##j##_##k
#define RI(i,j,k) RV_IMAG_##i##_##j##_##k

int main(void) {
    UKFParams sp;
    UKFState state;
    memset(&sp, 0, sizeof(sp));

    /* Init YBUS */
    for (int k = 0; k < 3; k++) {
        sp.YBUS[k].real[0][0] = YR(0,0,k); sp.YBUS[k].imag[0][0] = YI(0,0,k);
        sp.YBUS[k].real[0][1] = YR(0,1,k); sp.YBUS[k].imag[0][1] = YI(0,1,k);
        sp.YBUS[k].real[1][0] = YR(1,0,k); sp.YBUS[k].imag[1][0] = YI(1,0,k);
        sp.YBUS[k].real[1][1] = YR(1,1,k); sp.YBUS[k].imag[1][1] = YI(1,1,k);
    }
    /* Init RV */
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 5; i++) {
            sp.RV[k].real[i][0] = RR(i,0,k); sp.RV[k].imag[i][0] = RI(i,0,k);
            sp.RV[k].real[i][1] = RR(i,1,k); sp.RV[k].imag[i][1] = RI(i,1,k);
        }
    }

    sp.E_abs[0]=E_ABS_0; sp.E_abs[1]=E_ABS_1;
    sp.PM[0]=PM_0; sp.PM[1]=PM_1;
    sp.M[0]=M_0; sp.M[1]=M_1;
    sp.D[0]=D_0; sp.D[1]=D_1;
    sp.X0[0]=X0_0; sp.X0[1]=X0_1; sp.X0[2]=X0_2; sp.X0[3]=X0_3;

    ukf_init(&state, &sp);

    /* Run UKF with first measurement 200 times (all same Z for quick test) */
    double Z[14];
    memcpy(Z, Z_FIRST, 14*sizeof(double));

    printf("C UKF test: %d steps\n", UKF_TEST_STEPS);
    printf("Parameters loaded OK\n");
    printf("Initial X: [%.6f, %.6f, %.6f, %.6f]\n", state.X_hat[0], state.X_hat[1], state.X_hat[2], state.X_hat[3]);

    int ret = 0;
    for (int i = 0; i < UKF_TEST_STEPS; i++) {
        double t = i * UKF_DT;
        ret = ukf_step(&state, &sp, Z, t);
        if (ret != 0) {
            printf("UKF step %d FAILED (ret=%d)\n", i, ret);
            return 1;
        }
        if (i < 5 || i % 50 == 0) {
            printf("  Step %3d t=%.4f: X=[%.8f, %.8f, %.8f, %.8f]\n",
                   i, t, state.X_hat[0], state.X_hat[1], state.X_hat[2], state.X_hat[3]);
        }
    }

    double trace_P = 0;
    for (int i = 0; i < 4; i++) trace_P += state.P[i][i];
    double rmse = sqrt(trace_P);

    printf("\nC final X:  [%.10f, %.10f, %.10f, %.10f]\n",
           state.X_hat[0], state.X_hat[1], state.X_hat[2], state.X_hat[3]);
    printf("C final RMSE: %.10f\n", rmse);
    printf("Python final X:  [%.10f, %.10f, %.10f, %.10f]\n",
           PY_X_FINAL[0], PY_X_FINAL[1], PY_X_FINAL[2], PY_X_FINAL[3]);
    printf("Python final RMSE: %.10f\n", PY_RMSE_FINAL);

    double max_err = 0;
    for (int i = 0; i < 4; i++) {
        double err = fabs(state.X_hat[i] - PY_X_FINAL[i]);
        if (err > max_err) max_err = err;
    }
    double rmse_err = fabs(rmse - PY_RMSE_FINAL);

    printf("\nMax state error: %.2e\n", max_err);
    printf("RMSE error: %.2e\n", rmse_err);

    if (max_err < 0.01 && rmse_err < 0.01) {
        printf("✅ PASS: C UKF matches Python reference\n");
        return 0;
    } else {
        printf("❌ FAIL: deviation too large\n");
        return 1;
    }
}
