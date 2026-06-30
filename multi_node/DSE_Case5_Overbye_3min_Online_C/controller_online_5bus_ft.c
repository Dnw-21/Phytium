/**
 * controller_online_5bus_ft.c — FT-optimized UKF for 5-Bus (direct SHM)
 * Uses: BLAS-FT, LAPACK-FT, VML-FT
 */
#include "ukf_core_5_ft.h"
#include "../shm_direct.h"
#include <time.h>
#include <sched.h>

#define DEFAULT_max_frames 2000

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <5bus|39bus|9bus>\n", argv[0]);
        return 1;
    }
    const char *node_name = argv[1];

    int max_frames = DEFAULT_max_frames;
    const char *env_max = getenv("UKF_max_frames");
    if (env_max) max_frames = atoi(env_max);

    setbuf(stderr, NULL);  /* unbuffered stderr for reliable timing output */

    /* ---- CPU affinity & polling config ---- */
    const char *env_cpu = getenv("UKF_CPU");
    if (env_cpu) {
        int cpu = atoi(env_cpu);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0)
            fprintf(stderr, "[Init] Bound to CPU %d\n", cpu);
        else
            perror("[Init] sched_setaffinity failed");
    }
    int poll_us = 1;
    const char *env_poll = getenv("UKF_POLL_US");
    if (env_poll) poll_us = atoi(env_poll);
    int busy_wait = 0;
    const char *env_busy = getenv("UKF_BUSY_WAIT");
    if (env_busy) busy_wait = atoi(env_busy);
    fprintf(stderr, "[Init] poll_us=%d busy_wait=%d\n", poll_us, busy_wait);

    SystemParams sp;
    char params_file[64];
    snprintf(params_file, sizeof(params_file), "system_params_%s.bin", node_name);
    if (load_system_params(&sp, params_file) != 0) {
        fprintf(stderr, "ERROR: %s not found!\n", params_file);
        return 1;
    }

    size_t shm_size;
    int fsz, cap;
    volatile uint8_t *mem = shm_map(node_name, &shm_size, &fsz, &cap);
    if (!mem) return 1;
    fprintf(stderr, "[Init] SHM mapped: %s, cap=%d frames, fsz=%d\n", node_name, cap, fsz);

    UKFState st;
    ukf_init(&sp, &st);

    printf("# time,delta1,delta2,omega1,omega2,RMSE\n");
    fflush(stdout);

    int timeout = 30000;
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
    uint32_t max_cnt_seen = init_cnt;
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
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
            if (prev_cnt > max_cnt_seen) max_cnt_seen = prev_cnt;
            if (count >= max_frames) {
                fprintf(stderr, "[Done] Reached max_frames=%d\n", count);
                break;
            }
        } else {
            if (!busy_wait) usleep(poll_us);
            else sched_yield();
            idle_count++;
            uint32_t curr_cnt = *(volatile uint32_t *)(mem + 8);
            if (curr_cnt > max_cnt_seen) max_cnt_seen = curr_cnt;
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
            if (count >= max_frames) {
                fprintf(stderr, "[Done] Reached max_frames=%d\n", count);
                break;
            }
            if (count >= NUM_SAMPLES) {
                fprintf(stderr, "[Done] Processed >= NUM_SAMPLES frames (%d)\n", count);
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1e3 + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    double t_avg = count > 0 ? t_total / count : 0.0;
    int shm_produced = max_cnt_seen > init_cnt ? (max_cnt_seen - init_cnt) : 0;
    int dropped = shm_produced > count ? (shm_produced - count) : 0;
    double shm_fps = elapsed_ms > 0 ? shm_produced * 1000.0 / elapsed_ms : 0.0;
    fprintf(stderr, "[Timing] frames=%d | avg=%.1fus | min=%.1fus | max=%.1fus | total_ukf=%.3fs | wall_ms=%.1f | fps=%.1f | shm_fps=%.1f | shm_produced=%d | dropped=%d | skipped_errors=%d\n",
            count, t_avg, t_min, t_max, t_total / 1e6, elapsed_ms, count > 0 ? 1e6 / t_avg : 0.0, shm_fps, shm_produced, dropped, skipped_errors);

    shm_unmap(mem, shm_size);
    return 0;
}