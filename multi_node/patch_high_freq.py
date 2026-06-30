#!/usr/bin/env python3
"""
Patch controller_online_*.c to remove the usleep(100) bottleneck
and add CPU affinity, configurable polling, busy-wait mode, and
SHM drop-frame statistics.
"""
import os, re

files = [
    "DSE_Case5_Overbye_3min_Online_C/controller_online_5bus_opt.c",
    "DSE_Case5_Overbye_3min_Online_C/controller_online_5bus_ft.c",
    "DSE_Calculation_UKF_9case_3minc_implementation/controller_online_9bus_opt.c",
    "DSE_Calculation_UKF_9case_3minc_implementation/controller_online_9bus_ft.c",
    "DSE_Case39_3min_Online_C/controller_online_39bus_opt.c",
    "DSE_Case39_3min_Online_C/controller_online_39bus_ft.c",
]

base = "/home/alientek/Phytium/multi_node"

for rel in files:
    path = os.path.join(base, rel)
    with open(path, "r") as f:
        src = f.read()

    # 1) Add #include <sched.h> after #include <time.h>
    if "#include <sched.h>" not in src:
        src = src.replace("#include <time.h>", "#include <time.h>\n#include <sched.h>")

    # 2) Add CPU affinity + poll config after setbuf(stderr, NULL);
    config_block = '''    setbuf(stderr, NULL);  /* unbuffered stderr for reliable timing output */

    /* ---- CPU affinity & polling config ---- */
    const char *env_cpu = getenv("UKF_CPU");
    if (env_cpu) {
        int cpu = atoi(env_cpu);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0)
            fprintf(stderr, "[Init] Bound to CPU %d\\n", cpu);
        else
            perror("[Init] sched_setaffinity failed");
    }
    int poll_us = 1;
    const char *env_poll = getenv("UKF_POLL_US");
    if (env_poll) poll_us = atoi(env_poll);
    int busy_wait = 0;
    const char *env_busy = getenv("UKF_BUSY_WAIT");
    if (env_busy) busy_wait = atoi(env_busy);
    fprintf(stderr, "[Init] poll_us=%d busy_wait=%d\\n", poll_us, busy_wait);
'''
    src = src.replace("    setbuf(stderr, NULL);  /* unbuffered stderr for reliable timing output */", config_block.strip())

    # 3) Add max_cnt_seen and wall-clock timer after prev_cnt = init_cnt;
    if "max_cnt_seen" not in src:
        src = src.replace(
            "    uint32_t prev_cnt = init_cnt;",
            "    uint32_t prev_cnt = init_cnt;\n    uint32_t max_cnt_seen = init_cnt;\n    struct timespec t_start, t_end;\n    clock_gettime(CLOCK_MONOTONIC, &t_start);"
        )

    # 4) In the success branch, update max_cnt_seen after prev_cnt update
    # Look for pattern: prev_cnt = *(volatile uint32_t *)(mem + 8); (inside success if)
    src = src.replace(
        "            prev_cnt = *(volatile uint32_t *)(mem + 8);\n            if (count >= max_frames)",
        "            prev_cnt = *(volatile uint32_t *)(mem + 8);\n            if (prev_cnt > max_cnt_seen) max_cnt_seen = prev_cnt;\n            if (count >= max_frames)"
    )

    # 5) In the else branch, update max_cnt_seen and replace usleep(100)
    old_else = """        } else {
            usleep(100);
            idle_count++;
            uint32_t curr_cnt = *(volatile uint32_t *)(mem + 8);"""

    new_else = """        } else {
            if (!busy_wait) usleep(poll_us);
            else sched_yield();
            idle_count++;
            uint32_t curr_cnt = *(volatile uint32_t *)(mem + 8);
            if (curr_cnt > max_cnt_seen) max_cnt_seen = curr_cnt;"""

    src = src.replace(old_else, new_else)

    # 6) Add wall-clock end and enhanced Timing output
    old_timing = """    double t_avg = count > 0 ? t_total / count : 0.0;
    fprintf(stderr, "[Timing] frames=%d | avg=%.1fus | min=%.1fus | max=%.1fus | total=%.3fs | fps=%.1f | skipped_errors=%d\\n",
            count, t_avg, t_min, t_max, t_total / 1e6, count > 0 ? 1e6 / t_avg : 0.0, skipped_errors);"""

    new_timing = """    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1e3 + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    double t_avg = count > 0 ? t_total / count : 0.0;
    int shm_produced = max_cnt_seen > init_cnt ? (max_cnt_seen - init_cnt) : 0;
    int dropped = shm_produced > count ? (shm_produced - count) : 0;
    double shm_fps = elapsed_ms > 0 ? shm_produced * 1000.0 / elapsed_ms : 0.0;
    fprintf(stderr, "[Timing] frames=%d | avg=%.1fus | min=%.1fus | max=%.1fus | total_ukf=%.3fs | wall_ms=%.1f | fps=%.1f | shm_fps=%.1f | shm_produced=%d | dropped=%d | skipped_errors=%d\\n",
            count, t_avg, t_min, t_max, t_total / 1e6, elapsed_ms, count > 0 ? 1e6 / t_avg : 0.0, shm_fps, shm_produced, dropped, skipped_errors);"""

    src = src.replace(old_timing, new_timing)

    with open(path, "w") as f:
        f.write(src)

    print("Patched", rel)
