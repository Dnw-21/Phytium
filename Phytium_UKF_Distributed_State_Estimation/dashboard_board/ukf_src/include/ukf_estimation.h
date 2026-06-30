/*
 * ukf_estimation.h
 * ============================================================
 * Unscented Kalman Filter for power system DSE.
 * 9-bus, 3-generator system.
 * ============================================================
 */
#ifndef UKF_ESTIMATION_H
#define UKF_ESTIMATION_H

#include <complex.h>

/*
 * System parameters structure (read from system_params.txt).
 */
typedef struct {
    int n;          /* number of generators (3) */
    int s;          /* number of buses (9) */
    int ns;         /* state dimension (6) */
    int nm;         /* measurement dimension (24) */
    int fs;         /* sampling frequency (1000 Hz) */
    int num_samples;/* number of time steps */
    double deltt;   /* time step */
    double t_SW;    /* fault start time */
    double t_FC;    /* fault clear time */

    double complex *YBUS;  /* (n, n, 3) reduced admittance matrices */
    double complex *RV;    /* (s, n, 3) voltage reconstruction matrices */
    double *E_abs;         /* (n,) internal voltage magnitudes */
    double *PM;            /* (n,) mechanical power */
    double *M;             /* (n,) inertia */
    double *D;             /* (n,) damping */
    double *X_hat_init;    /* (ns,) initial state estimate */
} SystemParams;

/*
 * Load system parameters from text file.
 * Returns 0 on success.
 */
int load_system_params(const char *filename, SystemParams *sp);

/*
 * Free system parameters.
 */
void free_system_params(SystemParams *sp);

/*
 * Load measurements from CSV file.
 * Z_mes: (nm x num_samples) column-major, allocated by caller.
 * Returns number of samples actually loaded.
 */
int load_measurements(const char *filename, int nm, int max_samples, double *Z_mes);

/*
 * Load true states from CSV file.
 * X_true: (ns x num_samples) column-major.
 * Returns number of samples loaded.
 */
int load_true_states(const char *filename, int ns, int max_samples, double *X_true);

/*
 * Run UKF estimation.
 * X_est: (ns x num_samples) output, column-major, pre-allocated.
 * RMSE:  (num_samples,) output.
 */
void ukf_estimation(const SystemParams *sp,
                    const double *Z_mes, int num_samples,
                    double *X_est, double *RMSE);

#endif /* UKF_ESTIMATION_H */
