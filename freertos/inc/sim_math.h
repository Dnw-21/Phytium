/*
 * sim_math.h — 电力系统动态仿真数学库
 * ========================================
 * 为 FreeRTOS 移植的轻量级数学函数：
 * - 复矩阵运算 (实部/虚部分离, 不依赖 complex.h)
 * - 转子运动方程导数
 * - 单步 RK4 积分
 * - 测量函数 h(x): 状态 → 测量向量
 *
 * 设计决策：
 * - 用独立 real/imag 数组存储复数, 避免交叉编译器 complex.h 兼容问题
 * - 2×2 矩阵运算针对 5-bus 系统硬编码, 未来扩展到通用维度
 * - 所有函数用 float (单精度), 匹配嵌入式环境
 */

#ifndef SIM_MATH_H
#define SIM_MATH_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 结构体: 仿真状态 ─── */
typedef struct {
    float x[4];             /* 状态向量: [δ₁, δ₂, ω₁, ω₂] */
    float step;             /* 当前步数 */
    float t;                /* 当前仿真时间 (秒) */
    int   fault_state;      /* 0=pre-fault, 1=during-fault, 2=post-fault */
    int   running;          /* 运行标志 */
    unsigned int frames_sent; /* 已发送帧数 */
} sim_state_t;

/* ─── 三角函数 (包装, 方便替换为查表实现) ─── */
static inline float sim_sin(float x) { return sinf(x); }
static inline float sim_cos(float x) { return cosf(x); }
static inline float sim_atan2(float y, float x) { return atan2f(y, x); }
static inline float sim_sqrt(float x) { return sqrtf(x); }

/* ─── 2×2 复矩阵 × 复向量 ───
 * y = A * x
 * A: 2×2 complex (real_A[2][2], imag_A[2][2])
 * x: 2×1 complex (real_x[2], imag_x[2])
 * y: 2×1 complex (real_y[2], imag_y[2]) — output
 *
 * y_real[i] = Σⱼ (A_real[i][j]*x_real[j] - A_imag[i][j]*x_imag[j])
 * y_imag[i] = Σⱼ (A_real[i][j]*x_imag[j] + A_imag[i][j]*x_real[j])
 */
void sim_cmat_mul_vec_2x2(
    const float A_real[2][2], const float A_imag[2][2],
    const float x_real[2], const float x_imag[2],
    float y_real[2], float y_imag[2]);

/* ─── 5×2 复矩阵 × 复向量 ───
 * y = A * x
 * A: 5×2 complex
 * x: 2×1 complex
 * y: 5×1 complex — output
 */
void sim_cmat_mul_vec_5x2(
    const float A_real[5][2], const float A_imag[5][2],
    const float x_real[2], const float x_imag[2],
    float y_real[5], float y_imag[5]);

/* ─── 故障状态判断 ───
 * 返回: 0=正常, 1=故障中, 2=故障后
 */
int sim_get_fault_state(float t, float t_start, float t_end);

/* ─── 转子运动方程导数 ───
 * dx/dt = f(t, x, params)
 * x[0..1] = δ, x[2..3] = ω
 * dx[0..1] = dδ/dt = ω
 * dx[2..3] = dω/dt = (PM - Pe(δ) - D*ω) / M
 *
 * Pe(δ) = real(E * conj(I)), I = Ybusm * E, E = |E| * exp(jδ)
 *
 * 参数:
 *   x[4]:       当前状态 [δ₁,δ₂, ω₁,ω₂]
 *   dx[4]:      输出导数
 *   ybusm_real, ybusm_imag: 当前故障状态下的降阶导纳矩阵 [2][2]
 *   E_abs[2]:   内电势幅值
 *   PM[2]:      机械功率
 *   M[2]:       惯性常数
 *   D[2]:       阻尼系数
 */
void sim_dynamic_deriv_5bus(
    const float x[4], float dx[4],
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float E_abs[2], const float PM[2],
    const float M[2], const float D[2]);

/* ─── 单步 4 阶 Runge-Kutta 积分 ───
 * x_{n+1} = x_n + (k1 + 2*k2 + 2*k3 + k4) / 6
 *
 * 参数:
 *   x[4]:        输入状态 → 原地更新为下一步状态
 *   dt:          时间步长
 *   ybusm_real, ybusm_imag:  降阶导纳矩阵
 *   E_abs, PM, M, D:         系统参数
 */
void sim_rk4_step_5bus(
    float x[4], float dt,
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float E_abs[2], const float PM[2],
    const float M[2], const float D[2]);

/* ─── 测量函数 h(x): 状态 → 14 维测量向量 ───
 * Z[0..1]   = PG₁, PG₂       (发电机有功功率, pu)
 * Z[2..3]   = QG₁, QG₂       (发电机无功功率, pu)
 * Z[4..8]   = V₁~V₅          (母线电压幅值, pu)
 * Z[9..13]  = θ₁~θ₅          (母线电压相角, rad)
 *
 * 计算:
 *   E = |E| * exp(jδ)
 *   I = Ybusm * E
 *   PG = real(E * conj(I)), QG = imag(E * conj(I))
 *   V_bus = RVm * E
 *   Vmag = |V_bus|, Vangle = angle(V_bus)
 */
void sim_compute_meas_5bus(
    const float x[4], float Z[14],
    const float ybusm_real[2][2], const float ybusm_imag[2][2],
    const float rvm_real[5][2], const float rvm_imag[5][2],
    const float E_abs[2]);

/* ─── 初始化仿真状态 ─── */
void sim_state_init(sim_state_t *s,
    const float x0[4]);

/* ─── 获取当前故障状态的 YBUS/RV 指针 ───
 * 从全局参数数组中根据 fault_state 获取对应切片
 * ps=0 → [0], ps=1 → [1], ps=2 → [2]
 *
 * 用法:
 *   const float (*ybusm_real)[2] = &SIM_5BUS_YBUS_REAL[0][0][ps];
 *   // 即 ybusm_real[i][j] = SIM_5BUS_YBUS_REAL[i][j][ps]
 */
static inline int sim_select_fault_state(float t)
{
    if (t < 5.0f)       return 0;   /* pre-fault */
    else if (t <= 5.3f) return 1;   /* during-fault */
    else                return 2;   /* post-fault */
}

#ifdef __cplusplus
}
#endif

#endif /* SIM_MATH_H */
