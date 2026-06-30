/*
 * sim_math_test.c — C 数学库单元测试 (v3)
 * =========================================
 * 编译: gcc -Wall -O2 -o sim_math_test sim_math.c sim_math_test.c -I../../freertos/inc -lm
 * 运行: ./sim_math_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sim_math.h"
#include "sim_params_5bus.h"

/* ─── 抽取 3D→2D 的 helper (手动内联, 避免宏展开问题) ─── */
#define SLICE2_REAL_2x2(name, ps) { \
    {name[0][0][ps], name[0][1][ps]}, \
    {name[1][0][ps], name[1][1][ps]} }
#define SLICE2_IMAG_2x2(name, ps) SLICE2_REAL_2x2(name, ps)
#define SLICE2_REAL_5x2(name, ps) { \
    {name[0][0][ps], name[0][1][ps]}, \
    {name[1][0][ps], name[1][1][ps]}, \
    {name[2][0][ps], name[2][1][ps]}, \
    {name[3][0][ps], name[3][1][ps]}, \
    {name[4][0][ps], name[4][1][ps]} }
#define SLICE2_IMAG_5x2(name, ps) SLICE2_REAL_5x2(name, ps)

/* ─── 预构建的 2D 切片 ─── */
static const float YR_PRE[2][2] = SLICE2_REAL_2x2(SIM_5BUS_YBUS_REAL, 0);
static const float YI_PRE[2][2] = SLICE2_IMAG_2x2(SIM_5BUS_YBUS_IMAG, 0);
static const float YR_DUR[2][2] = SLICE2_REAL_2x2(SIM_5BUS_YBUS_REAL, 1);
static const float YI_DUR[2][2] = SLICE2_IMAG_2x2(SIM_5BUS_YBUS_IMAG, 1);
static const float YR_POST[2][2] = SLICE2_REAL_2x2(SIM_5BUS_YBUS_REAL, 2);
static const float YI_POST[2][2] = SLICE2_IMAG_2x2(SIM_5BUS_YBUS_IMAG, 2);
static const float RR_PRE[5][2] = SLICE2_REAL_5x2(SIM_5BUS_RV_REAL, 0);
static const float RI_PRE[5][2] = SLICE2_IMAG_5x2(SIM_5BUS_RV_IMAG, 0);

/* ─── 辅助函数 ─── */
static int float_close(float a, float b, float rtol, float atol)
{
    float diff = fabsf(a - b);
    float tol = atol + rtol * fmaxf(fabsf(a), fabsf(b));
    return diff <= tol;
}

static void print_vec4(const char *label, const float v[4])
{
    printf("%s [% .6f, % .6f, % .6f, % .6f]\n", label, v[0], v[1], v[2], v[3]);
}

static void print_vec14(const char *label, const float v[14])
{
    printf("%s\n", label);
    printf("  PG:  [% .6f, % .6f]\n", v[0], v[1]);
    printf("  QG:  [% .6f, % .6f]\n", v[2], v[3]);
    printf("  V:   [% .6f, % .6f, % .6f, % .6f, % .6f]\n", v[4], v[5], v[6], v[7], v[8]);
    printf("  ang: [% .6f, % .6f, % .6f, % .6f, % .6f]\n", v[9], v[10], v[11], v[12], v[13]);
}

/* ======================================================================== */
int main(void)
{
    printf("============================================================\n");
    printf("sim_math_test — C vs Python 交叉验证\n");
    printf("System: %d-gen, %d-bus, dt=%.4fs\n",
           SIM_5BUS_N_GEN, SIM_5BUS_N_BUS, SIM_5BUS_DT);
    printf("============================================================\n");
    int failures = 0;

    /* ─── Test 1: 复矩阵乘向量 ─── */
    printf("\n=== T1: 复矩阵乘向量 ===\n");
    {
        float Ar[2][2] = {{1,0},{0,2}}, Ai[2][2] = {{0,0},{0,0}};
        float xr[2] = {3,4}, xi[2] = {0,0}, yr[2], yi[2];
        sim_cmat_mul_vec_2x2(Ar, Ai, xr, xi, yr, yi);
        int ok = (yr[0]==3 && yr[1]==8 && yi[0]==0 && yi[1]==0);
        printf("  y=[%.0f,%.0f] %s\n", yr[0], yr[1], ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    /* ─── Test 2: 测量函数 h(X0) vs Python ─── */
    printf("\n=== T2: h(X0) vs Python ===\n");
    {
        float Z[14];
        sim_compute_meas_5bus(SIM_5BUS_X0, Z, YR_PRE, YI_PRE,
                               RR_PRE, RI_PRE, SIM_5BUS_E_ABS);
        print_vec14("  C:", Z);

        int ok = 1;
        /* Python ref: PG=[8.2021, 3.5318], QG=[6.7638, 6.2463],
           V=[1.0692, 0.8517, 0.9883, 0.8737, 0.8757],
           A=[0.0115, -0.3715, -0.7647, -0.7842, -0.3581] */
        float ref[14] = {8.202125f, 3.531796f, 6.763816f, 6.246298f,
                         1.069181f, 0.851695f, 0.988285f, 0.873714f, 0.875659f,
                         0.011484f, -0.371461f, -0.764668f, -0.784228f, -0.358090f};
        const char *labels[] = {"PG1","PG2","QG1","QG2",
                                "V1","V2","V3","V4","V5",
                                "A1","A2","A3","A4","A5"};
        for (int i = 0; i < 14; i++) {
            if (!float_close(Z[i], ref[i], 1e-4f, 1e-4f)) {
                printf("  FAIL: %s C=%.6f ref=%.6f\n", labels[i], Z[i], ref[i]);
                ok = 0;
            }
        }
        printf("  %s\n", ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    /* ─── Test 3: 稳态导数 ≈ 0 ─── */
    printf("\n=== T3: 稳态导数验证 ===\n");
    {
        float dx[4];
        sim_dynamic_deriv_5bus(SIM_5BUS_X0, dx, YR_PRE, YI_PRE,
                                SIM_5BUS_E_ABS, SIM_5BUS_PM, SIM_5BUS_M, SIM_5BUS_D);
        print_vec4("  dx =", dx);
        int ok = float_close(dx[2], 0, 0.01f, 0.1f)
              && float_close(dx[3], 0, 0.01f, 0.1f);
        printf("  (dω≈0 at steady state) %s\n", ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    /* ─── Test 4: 10 步 RK4 ─── */
    printf("\n=== T4: 10 步 RK4 ===\n");
    {
        float x[4];
        for (int i = 0; i < 4; i++) x[i] = SIM_5BUS_X0[i];
        printf("  Step  |  δ₁       |  δ₂       |  ω₁       |  ω₂\n");
        printf("  ------+-----------+-----------+-----------+----------\n");
        printf("  0     |% 10.6f |% 10.6f |% 10.6f |% 10.6f\n",
               x[0], x[1], x[2], x[3]);
        for (int s = 0; s < 10; s++) {
            sim_rk4_step_5bus(x, 0.0005f, YR_PRE, YI_PRE,
                               SIM_5BUS_E_ABS, SIM_5BUS_PM, SIM_5BUS_M, SIM_5BUS_D);
            printf("  %-5d |% 10.6f |% 10.6f |% 10.6f |% 10.6f\n",
                   s+1, x[0], x[1], x[2], x[3]);
        }
        int ok = 1;
        for (int i = 0; i < 4; i++)
            if (isnan(x[i]) || isinf(x[i]) || fabsf(x[i]) > 100) ok = 0;
        printf("  %s\n", ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    /* ─── Test 5: 故障状态切换 ─── */
    printf("\n=== T5: 故障切换 ===\n");
    {
        int ok = 1;
        ok &= (sim_get_fault_state(0.0f, 5.0f, 5.3f) == 0);
        ok &= (sim_get_fault_state(5.0f, 5.0f, 5.3f) == 1);
        ok &= (sim_get_fault_state(5.3f, 5.0f, 5.3f) == 1);
        ok &= (sim_get_fault_state(5.31f, 5.0f, 5.3f) == 2);
        printf("  pre/during/post: %d %d %d %d %s\n",
               sim_get_fault_state(0.0f,5.0f,5.3f),
               sim_get_fault_state(5.0f,5.0f,5.3f),
               sim_get_fault_state(5.3f,5.0f,5.3f),
               sim_get_fault_state(5.31f,5.0f,5.3f),
               ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    /* ─── Test 6: 跨故障仿真 ─── */
    printf("\n=== T6: 跨故障仿真 (t=4.99s→5.00s→5.30s→5.31s) ===\n");
    {
        float x[4];
        for (int i = 0; i < 4; i++) x[i] = SIM_5BUS_X0[i];

        /* 跑到 4.99s = 9980 步 */
        for (int s = 0; s < 9980; s++)
            sim_rk4_step_5bus(x, 0.0005f, YR_PRE, YI_PRE,
                               SIM_5BUS_E_ABS, SIM_5BUS_PM, SIM_5BUS_M, SIM_5BUS_D);
        printf("  t=4.99s (pre):  ");
        print_vec4("", x);

        /* 故障中 600 步 */
        for (int s = 0; s < 600; s++)
            sim_rk4_step_5bus(x, 0.0005f, YR_DUR, YI_DUR,
                               SIM_5BUS_E_ABS, SIM_5BUS_PM, SIM_5BUS_M, SIM_5BUS_D);
        printf("  t=5.30s (dur):  ");
        print_vec4("", x);

        /* 故障后 20 步 */
        for (int s = 0; s < 20; s++)
            sim_rk4_step_5bus(x, 0.0005f, YR_POST, YI_POST,
                               SIM_5BUS_E_ABS, SIM_5BUS_PM, SIM_5BUS_M, SIM_5BUS_D);
        printf("  t=5.31s (post): ");
        print_vec4("", x);

        int ok = 1;
        for (int i = 0; i < 4; i++)
            if (isnan(x[i]) || isinf(x[i]) || fabsf(x[i]) > 100) ok = 0;
        printf("  %s\n", ok?"PASS":"FAIL");
        if (!ok) failures++;
    }

    printf("\n============================================================\n");
    printf("%s (%d failures)\n", failures?"SOME TESTS FAILED ❌":"ALL TESTS PASSED ✅", failures);
    printf("============================================================\n");
    return failures;
}
