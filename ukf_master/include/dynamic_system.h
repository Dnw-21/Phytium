/*
 * dynamic_system.h
 * ============================================================
 * Swing equation dynamics + vectorized RK4 integrator.
 * ============================================================
 */
#ifndef DYNAMIC_SYSTEM_H
#define DYNAMIC_SYSTEM_H

#include <complex.h>

/*
 * Swing equation: d(delta)/dt = omega, d(omega)/dt = (Pm-Pe)/M - D*omega/M
 * x[0..n-1] = delta, x[n..2n-1] = omega
 * dx[0..n-1] = omega, dx[n..2n-1] = (Pm-Pe)/M - D*omega/M
 */
void dynamic_system(int n, const double *x,
                    const double *M, const double *D,
                    const double complex *Ybusm,
                    const double *E_abs,
                    const double *PM,
                    double *dx);

/*
 * Vectorized RK4 for UKF sigma point propagation.
 * Advances all 2*ns sigma points simultaneously.
 *
 * X_sigma: (ns x 2*ns) input sigma points, column-major
 * xbreve:  (ns x 2*ns) output propagated sigma points, column-major
 */
void rk4_vectorized(int n, double deltt, const double *E_abs,
                    int ns, const double *X_sigma,
                    const double *PM, const double *M, const double *D,
                    const double complex *Ybusm,
                    double *xbreve);

#endif /* DYNAMIC_SYSTEM_H */
