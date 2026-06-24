/*
 * main.c — Master Controller (C version)
 * ============================================================
 * Reads system_params.txt, measurements.txt, true_states.csv
 * Runs UKF estimation, outputs results to CSV files.
 *
 * Usage:
 *   ./ukf_controller <data_dir>
 *
 * Input files (in data_dir):
 *   system_params.txt   — system parameters
 *   measurements.txt    — 4dp measurement data
 *   true_states.csv     — optional, for comparison
 *
 * Output files:
 *   ukf_estimation_output.csv  — [time, delta1..3, omega1..3, rmse]
 *   ukf_comparison_output.csv  — [time, true_delta1..3, est_delta1..3, ...]
 */
#include "ukf_estimation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_PATH 512

int main(int argc, char **argv)
{
    const char *data_dir = (argc > 1) ? argv[1] : ".";
    char path[MAX_PATH];

    printf("============================================================\n");
    printf("Master Controller - UKF State Estimation (C version)\n");
    printf("IEEE 9-Bus, 3-Generator System\n");
    printf("Measurements: 4 decimal places (via LoRa)\n");
    printf("============================================================\n\n");

    /* ---- Load system parameters ---- */
    snprintf(path, MAX_PATH, "%s/system_params.txt", data_dir);
    SystemParams sp;
    if (load_system_params(path, &sp) != 0) return 1;

    printf("  n=%d s=%d ns=%d nm=%d fs=%d\n", sp.n, sp.s, sp.ns, sp.nm, sp.fs);
    printf("  t_SW=%.4f t_FC=%.4f deltt=%.6f\n", sp.t_SW, sp.t_FC, sp.deltt);

    int ns = sp.ns, nm = sp.nm, max_samples = sp.num_samples;

    /* Allocate measurement & estimate arrays */
    double *Z_mes = (double*)calloc((size_t)nm * max_samples, sizeof(double));
    double *X_est = (double*)calloc((size_t)ns * max_samples, sizeof(double));
    double *RMSE  = (double*)calloc((size_t)max_samples, sizeof(double));

    /* ---- Load measurements ---- */
    snprintf(path, MAX_PATH, "%s/measurements.txt", data_dir);
    int actual = load_measurements(path, nm, max_samples, Z_mes);
    if (actual == 0) { fprintf(stderr, "ERROR: no measurements loaded.\n"); return 1; }

    /* ---- Load true states (optional) ---- */
    double *X_true = (double*)calloc((size_t)ns * max_samples, sizeof(double));
    snprintf(path, MAX_PATH, "%s/true_states.csv", data_dir);
    int has_true = load_true_states(path, ns, max_samples, X_true);

    /* ---- Run UKF ---- */
    printf("\n[UKF] Starting estimation...\n");
    ukf_estimation(&sp, Z_mes, actual, X_est, RMSE);

    /* ---- Output CSV: estimates ---- */
    snprintf(path, MAX_PATH, "%s/ukf_estimation_output.csv", data_dir);
    FILE *fout = fopen(path, "w");
    fprintf(fout, "time,delta1,delta2,delta3,omega1,omega2,omega3,RMSE\n");
    for (int i = 0; i < actual; i++) {
        double t = (double)i / sp.fs;
        fprintf(fout, "%.6f", t);
        for (int j = 0; j < ns; j++)
            fprintf(fout, ",%.10f", X_est[j * max_samples + i]);
        fprintf(fout, ",%.10f\n", RMSE[i]);
    }
    fclose(fout);
    printf("  Saved ukf_estimation_output.csv\n");

    /* ---- Output CSV: comparison (if true states available) ---- */
    if (has_true > 0) {
        snprintf(path, MAX_PATH, "%s/ukf_comparison_output.csv", data_dir);
        fout = fopen(path, "w");
        fprintf(fout, "time");
        for (int j = 0; j < ns; j++) fprintf(fout, ",true_delta%d", j+1);
        for (int j = 0; j < ns; j++) fprintf(fout, ",est_delta%d", j+1);
        fprintf(fout, "\n");
        for (int i = 0; i < actual; i++) {
            double t = (double)i / sp.fs;
            fprintf(fout, "%.6f", t);
            for (int j = 0; j < ns; j++)
                fprintf(fout, ",%.10f", X_true[j * max_samples + i]);
            for (int j = 0; j < ns; j++)
                fprintf(fout, ",%.10f", X_est[j * max_samples + i]);
            fprintf(fout, "\n");
        }
        fclose(fout);
        printf("  Saved ukf_comparison_output.csv\n");

        /* ---- RMS errors ---- */
        double rms[12] = {0};
        for (int j = 0; j < ns; j++) {
            double sum = 0.0;
            for (int i = 0; i < actual; i++) {
                double e = X_est[j * max_samples + i] - X_true[j * max_samples + i];
                sum += e * e;
            }
            rms[j] = sqrt(sum / actual);
        }
        printf("\n============================================================\n");
        printf("Estimation Complete! RMS Errors:\n");
        for (int j = 0; j < sp.n; j++)
            printf("  delta%d: %.6f deg\n", j+1, rms[j] * 180.0 / M_PI);
        for (int j = 0; j < sp.n; j++)
            printf("  omega%d: %.6f rad/s\n", j+1, rms[sp.n + j]);
        printf("============================================================\n");
    }

    /* Cleanup */
    free(Z_mes); free(X_est); free(RMSE); free(X_true);
    free_system_params(&sp);

    return 0;
}
