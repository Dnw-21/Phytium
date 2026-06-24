/**
 * controller_online_39bus_ft.c — FT-optimized UKF for 39-Bus (direct SHM)
 * Uses: BLAS-FT, LAPACK-FT, VML-FT
 */
#include "ukf_core_39_ft.h"
#include "../shm_direct.h"
#include <time.h>

#define MAX_FRAMES 2000

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <5bus|39bus|9bus>\n", argv[0]);
        return 1;
    }
    const char *node_name = argv[1];

    setbuf(stderr, NULL);  /* unbuffered stderr for reliable timing output */

    SystemParams sp;
    if (load_system_params(&sp, "system_params_39bus.bin") != 0) {
        fprintf(stderr, "ERROR: system_params_39bus.bin not found!\n");
        return 1;
    }

    size_t shm_size;
    int fsz, cap;
    volatile uint8_t *mem = shm_map(node_name, &shm_size, &fsz, &cap);
    if (!mem) return 1;
    fprintf(stderr, "[Init] SHM mapped: %s, cap=%d frames, fsz=%d\n", node_name, cap, fsz);

    UKFState st;
    ukf_init(&sp, &st);

    printf("# time,delta1..delta10,omega1..omega10,RMSE\n");
    fflush(stdout);

    int timeout = 5000;
    while (*(volatile uint32_t *)(mem + 8) == 0 && timeout-- > 0) usleep(1000);
    if (timeout <= 0) {
        fprintf(stderr, "ERROR: simulation not started (timeout)\n");
        shm_unmap(mem, shm_size);
        return 1;
    }

    uint32_t init_cnt = *(volatile uint32_t *)(mem + 8);
    int read_idx = 0;
    if ((int)init_cnt > cap) read_idx = init_cnt - cap;
    fprintf(stderr, "[Init] SHM cnt=%u, starting from read_idx=%d\n", init_cnt, read_idx);

    int count = 0;
    int skipped_errors = 0;
    uint32_t prev_cnt = init_cnt;
    int idle_count = 0;

    double t_total = 0.0, t_min = 1e9, t_max = 0.0;
    struct timespec t0, t1;

    while (1) {
        double z_k[NM];
        double k_time;

        if (shm_read_frame(mem, cap, fsz, read_idx, z_k, NM, &k_time)) {
            double x_out[NS];
            double rmse_val;

            clock_gettime(CLOCK_MONOTONIC, &t0);
            int ret = ukf_step(&sp, &st, z_k, k_time, x_out, &rmse_val);
            clock_gettime(CLOCK_MONOTONIC, &t1);

            if (ret != 0) {
                read_idx++;
                skipped_errors++;
                continue;
            }

            double dt = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
            t_total += dt;
            if (dt < t_min) t_min = dt;
            if (dt > t_max) t_max = dt;

            printf("%.6f", k_time);
            for (int i = 0; i < NS; i++) printf(",%.8f", x_out[i]);
            printf(",%.8f\n", rmse_val);
            fflush(stdout);

            read_idx++;
            count++;
            idle_count = 0;
            prev_cnt = *(volatile uint32_t *)(mem + 8);
            if (count >= MAX_FRAMES) {
                fprintf(stderr, "[Done] Reached MAX_FRAMES=%d\n", count);
                break;
            }
        } else {
            usleep(100);
            idle_count++;
            uint32_t curr_cnt = *(volatile uint32_t *)(mem + 8);
            /* Check if simulation ended */
            if (curr_cnt >= NUM_SAMPLES && curr_cnt == prev_cnt && idle_count >= 5) {
                fprintf(stderr, "\n[Done] Simulation ended at cnt=%u, processed %d frames\n",
                        curr_cnt, count);
                break;
            }
            if (curr_cnt != prev_cnt) {
                prev_cnt = curr_cnt;
                idle_count = 0;
            }
            /* Fallback: if we processed enough frames, break */
            if (count >= MAX_FRAMES) {
                fprintf(stderr, "[Done] Reached MAX_FRAMES=%d\n", count);
                break;
            }
            if (count >= NUM_SAMPLES) {
                fprintf(stderr, "[Done] Processed >= NUM_SAMPLES frames (%d)\n", count);
                break;
            }
        }
    }

    double t_avg = count > 0 ? t_total / count : 0.0;
    fprintf(stderr, "[Timing] frames=%d | avg=%.1fus | min=%.1fus | max=%.1fus | total=%.3fs | fps=%.1f | skipped_errors=%d\n",
            count, t_avg, t_min, t_max, t_total / 1e6, count > 0 ? 1e6 / t_avg : 0.0, skipped_errors);

    shm_unmap(mem, shm_size);
    return 0;
}