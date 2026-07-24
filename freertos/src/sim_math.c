/*
 * sim_math.c — 电力系统动态仿真数学库实现
 * ==========================================
 * 为 FreeRTOS 移植的轻量级数学函数。
 *
 * 验证: 在主机上用 sim_math_test.c 与 Python 参考输出对比
 */

#include "sim_math.h"
#include <string.h>

/* ========================================================================
 * 2×2 复矩阵 × 复向量
 * ======================================================================== */
void sim_cmat_mul_vec_2x2(
    const float A_real[2][2], const float A_imag[2][2],
    const float x_real[2], const float x_imag[2],
    float y_real[2], float y_imag[2])
{
    y_real[0] = A_real[0][0]*x_real[0] - A_imag[0][0]*x_imag[0]
              + A_real[0][1]*x_real[1] - A_imag[0][1]*x_imag[1];

    y_imag[0] = A_real[0][0]*x_imag[0] + A_imag[0][0]*x_real[0]
              + A_real[0][1]*x_imag[1] + A_imag[0][1]*x_real[1];

    y_real[1] = A_real[1][0]*x_real[0] - A_imag[1][0]*x_imag[0]
              + A_real[1][1]*x_real[1] - A_imag[1][1]*x_imag[1];

    y_imag[1] = A_real[1][0]*x_imag[0] + A_imag[1][0]*x_real[0]
              + A_real[1][1]*x_imag[1] + A_imag[1][1]*x_real[1];
}

/* ========================================================================
 * 5×2 复矩阵 × 复向量 (用于 RVm @ E)
 * ======================================================================== */
void sim_cmat_mul_vec_5x2(
    const float A_real[5][2], const float A_imag[5][2],
    const float x_real[2], const float x_imag[2],
    float y_real[5], float y_imag[5])
{
    for (int i = 0; i < 5; i++) {
        y_real[i] = A_real[i][0]*x_real[0] - A_imag[i][0]*x_imag[0]
                  + A_real[i][1]*x_real[1] - A_imag[i][1]*x_imag[1];

        y_imag[i] = A_real[i][0]*x_imag[0] + A_imag[i][0]*x_real[0]
                  + A_real[i][1]*x_imag[1] + A_imag[i][1]*x_real[1];
    }
}

/* ========================================================================
 * 故障状态判断
 * ======================================================================== */
int sim_get_fault_state(float t, float t_start, float t_end)
{
    if (t < t_start)       return 0;
    else if (t <= t_end)   return 1;
    else                   return 2;
}

/* ========================================================================
 * 转子运动方程导数 dx/dt = f(t, x, params)
 *
 * 状态:  x[0..1] = δ (转子角度 rad)
 *        x[2..3] = ω (转速偏差 rad/s)
 *
 * 计算:
 *   E_i = |E_i| * exp(jδ_i)    (内电势)
 *   I = Ybusm * E               (发电机电流注入)
 *   Pe_i = real(E_i * conj(I_i)) (电磁功率)
 *   dδ_i/dt = ω_i
 *   dω_i/dt = (PM_i - Pe_i - D_i * ω_i) / M_i
 * ======================================================================== */
void sim_dynamic_deriv_5bus(
    const float x[4], float dx[4],
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float E_abs[2], const float PM[2],
    const float M[2], const float D[2])
{
    int n = 2;  /* 发电机数 */

    /* 1. 计算内电势 E = |E| * exp(jδ) */
    float E_real[2], E_imag[2];
    for (int i = 0; i < n; i++) {
        E_real[i] = E_abs[i] * sim_cos(x[i]);   /* |E_i| * cos(δ_i) */
        E_imag[i] = E_abs[i] * sim_sin(x[i]);   /* |E_i| * sin(δ_i) */
    }

    /* 2. 计算电流 I = Ybusm * E */
    float I_real[2], I_imag[2];
    sim_cmat_mul_vec_2x2(ybusm_real, ybusm_imag,
                         E_real, E_imag, I_real, I_imag);

    /* 3. 计算电磁功率 Pe_i = real(E_i * conj(I_i)) = E_real*I_real + E_imag*I_imag */
    float Pe[2];
    for (int i = 0; i < n; i++) {
        Pe[i] = E_real[i] * I_real[i] + E_imag[i] * I_imag[i];
    }

    /* 4. 计算导数 */
    for (int i = 0; i < n; i++) {
        dx[i]     = x[n + i];                                  /* dδ/dt = ω */
        dx[n + i] = (PM[i] - Pe[i] - D[i] * x[n + i]) / M[i]; /* dω/dt */
    }
}

/* ========================================================================
 * 单步 4 阶 Runge-Kutta 积分
 *
 * k1 = dt * f(t,       x)
 * k2 = dt * f(t + dt/2, x + k1/2)
 * k3 = dt * f(t + dt/2, x + k2/2)
 * k4 = dt * f(t + dt,   x + k3)
 * x_next = x + (k1 + 2*k2 + 2*k3 + k4) / 6
 * ======================================================================== */
void sim_rk4_step_5bus(
    float x[4], float dt,
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float E_abs[2], const float PM[2],
    const float M[2], const float D[2])
{
    float k1[4], k2[4], k3[4], k4[4];
    float xtmp[4];

    /* k1 */
    sim_dynamic_deriv_5bus(x, k1,
        ybusm_real, ybusm_imag, E_abs, PM, M, D);
    for (int i = 0; i < 4; i++) {
        k1[i] *= dt;
        xtmp[i] = x[i] + 0.5f * k1[i];
    }

    /* k2 */
    sim_dynamic_deriv_5bus(xtmp, k2,
        ybusm_real, ybusm_imag, E_abs, PM, M, D);
    for (int i = 0; i < 4; i++) {
        k2[i] *= dt;
        xtmp[i] = x[i] + 0.5f * k2[i];
    }

    /* k3 */
    sim_dynamic_deriv_5bus(xtmp, k3,
        ybusm_real, ybusm_imag, E_abs, PM, M, D);
    for (int i = 0; i < 4; i++) {
        k3[i] *= dt;
        xtmp[i] = x[i] + k3[i];
    }

    /* k4 */
    sim_dynamic_deriv_5bus(xtmp, k4,
        ybusm_real, ybusm_imag, E_abs, PM, M, D);
    for (int i = 0; i < 4; i++) {
        k4[i] *= dt;
    }

    /* x_next = x + (k1 + 2*k2 + 2*k3 + k4) / 6 */
    for (int i = 0; i < 4; i++) {
        x[i] += (k1[i] + 2.0f*k2[i] + 2.0f*k3[i] + k4[i]) / 6.0f;
    }
}

/* ========================================================================
 * 测量函数 h(x): 状态 → 14 维测量向量
 *
 * Z[0..1]   = PG₁, PG₂       有功功率 (pu)
 * Z[2..3]   = QG₁, QG₂       无功功率 (pu)
 * Z[4..8]   = V₁~V₅          母线电压幅值 (pu)
 * Z[9..13]  = θ₁~θ₅          母线电压相角 (rad)
 *
 * 计算步骤:
 *   1. E = |E| * exp(jδ)        内电势
 *   2. I = Ybusm * E            发电机电流
 *   3. PG = real(E*conj(I))     有功功率
 *   4. QG = imag(E*conj(I))     无功功率
 *   5. V_bus = RVm * E          母线电压
 *   6. Vmag = |V_bus|           电压幅值
 *   7. Vangle = angle(V_bus)    电压相角
 * ======================================================================== */
void sim_compute_meas_5bus(
    const float x[4], float Z[14],
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float rvm_real[5][2], const float rvm_imag[5][2],
    const float E_abs[2])
{
    int n = 2, s = 5;

    /* 1. 内电势 E = |E| * exp(jδ) */
    float E_real[2], E_imag[2];
    for (int i = 0; i < n; i++) {
        E_real[i] = E_abs[i] * sim_cos(x[i]);
        E_imag[i] = E_abs[i] * sim_sin(x[i]);
    }

    /* 2. 电流 I = Ybusm * E */
    float I_real[2], I_imag[2];
    sim_cmat_mul_vec_2x2(ybusm_real, ybusm_imag,
                         E_real, E_imag, I_real, I_imag);

    /* 3. PG = real(E * conj(I)) = E_real*I_real + E_imag*I_imag */
    /* 4. QG = imag(E * conj(I)) = E_imag*I_real - E_real*I_imag */
    for (int i = 0; i < n; i++) {
        Z[i]       = E_real[i] * I_real[i] + E_imag[i] * I_imag[i];  /* PG */
        Z[n + i]   = E_imag[i] * I_real[i] - E_real[i] * I_imag[i];  /* QG */
    }

    /* 5. V_bus = RVm * E */
    float V_real[5], V_imag[5];
    sim_cmat_mul_vec_5x2(rvm_real, rvm_imag,
                         E_real, E_imag, V_real, V_imag);

    /* 6-7. Vmag / Vangle */
    for (int i = 0; i < s; i++) {
        Z[2*n + i]     = sim_sqrt(V_real[i]*V_real[i] + V_imag[i]*V_imag[i]);  /* Vmag */
        Z[2*n + s + i] = sim_atan2(V_imag[i], V_real[i]);                       /* Vangle */
    }
}

/* ========================================================================
 * 初始化仿真状态
 * ======================================================================== */
void sim_state_init(sim_state_t *s, const float x0[4])
{
    memcpy(s->x, x0, 4 * sizeof(float));
    s->step = 0;
    s->t = 0.0f;
    s->fault_state = 0;
    s->running = 0;
    s->frames_sent = 0;
}
