/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ukf_monitor_kprobe.c — eBPF kprobe 用户态加载器
 *
 * 加载 ukf_monitor_kprobe.bpf.o，读取 BPF maps 并实时显示:
 *   - UKF 进程调度间隔直方图
 *   - IRQ 处理延迟统计
 *   - Softirq 处理延迟统计
 *
 * 编译 (VM交叉编译):
 *   aarch64-linux-gnu-gcc -O2 -o ukf_monitor_kprobe ukf_monitor_kprobe.c \
 *       -I/usr/include/aarch64-linux-gnu -lbpf -lelf -lz
 *
 * 用法: sudo ./ukf_monitor_kprobe [interval_sec]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#define TASK_COMM_LEN    16
#define MAX_SLOTS        32

struct sched_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
    char comm[TASK_COMM_LEN];
};

struct irq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
};

struct softirq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
};

static volatile int running = 1;

static void sigint_handler(int sig)
{
    running = 0;
}

static void print_hist(const char *title, __u32 *slots, int count,
                       __u64 total_ns, const char *tag)
{
    if (count == 0) return;
    printf("%-20s [%s] count=%d avg=%.1fus\n",
           title, tag, count, (double)total_ns / count / 1000.0);
    printf("  usec  : count     distribution\n");
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (slots[i] == 0) continue;
        __u64 usec = (1ULL << i);
        printf("  %-6llu: %-8d |", usec, slots[i]);
        int bar = slots[i] * 60 / (count + 1);
        if (bar > 60) bar = 60;
        for (int j = 0; j < bar; j++) putchar('*');
        printf("\n");
    }
}

static int attach_kprobes(struct bpf_object *obj)
{
    struct bpf_program *prog;
    struct bpf_link *link;
    int attached = 0;

    bpf_object__for_each_program(prog, obj) {
        const char *name = bpf_program__name(prog);
        link = bpf_program__attach(prog);
        if (!link) {
            fprintf(stderr, "WARN: cannot attach %s: %s\n", name, strerror(errno));
            continue;
        }
        printf("attached: %s\n", name);
        attached++;
    }
    return attached;
}

int main(int argc, char *argv[])
{
    int interval = (argc > 1) ? atoi(argv[1]) : 2;
    if (interval < 1) interval = 1;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    const char *bpf_obj = "ukf_monitor_kprobe.bpf.o";
    if (argc > 2) bpf_obj = argv[2];

    struct bpf_object *obj = bpf_object__open(bpf_obj);
    if (!obj) {
        fprintf(stderr, "ERROR: cannot open %s\n", bpf_obj);
        return 1;
    }

    if (bpf_object__load(obj)) {
        fprintf(stderr, "ERROR: bpf_object__load failed: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    if (attach_kprobes(obj) == 0) {
        fprintf(stderr, "ERROR: no kprobes attached\n");
        bpf_object__close(obj);
        return 1;
    }

    printf("\n=== UKF eBPF Kprobe Monitor ===\n");
    printf("Collecting: scheduling interval, IRQ latency, softirq latency\n");
    printf("Interval: %ds | Press Ctrl+C to stop\n\n", interval);

    struct bpf_map *sched_map = bpf_object__find_map_by_name(obj, "sched_hists");
    struct bpf_map *irq_map   = bpf_object__find_map_by_name(obj, "irq_hists");
    struct bpf_map *softirq_map = bpf_object__find_map_by_name(obj, "softirq_hists");

    if (!sched_map || !irq_map || !softirq_map) {
        fprintf(stderr, "ERROR: cannot find BPF maps\n");
        bpf_object__close(obj);
        return 1;
    }

    int sched_fd = bpf_map__fd(sched_map);
    int irq_fd   = bpf_map__fd(irq_map);
    int softirq_fd = bpf_map__fd(softirq_map);

    while (running) {
        sleep(interval);
        printf("\033[2J\033[H");

        printf("========== UKF eBPF Kprobe Monitor | Interval: %ds ==========\n\n", interval);

        /* Scheduling interval */
        printf("--- UKF Scheduling Interval (time between consecutive runs) ---\n");
        __u32 sched_key = 0, next_key;
        int sched_total = 0;
        while (bpf_map_get_next_key(sched_fd, &sched_key, &next_key) == 0) {
            struct sched_hist hist = {0};
            bpf_map_lookup_elem(sched_fd, &next_key, &hist);
            if (hist.count > 0) {
                print_hist("Sched Interval", hist.slots, hist.count,
                           hist.total_latency_ns, hist.comm);
                sched_total++;
            }
            sched_key = next_key;
        }
        if (sched_total == 0) printf("  (no UKF scheduling events yet)\n\n");

        /* IRQ latency */
        printf("\n--- IRQ Handler Latency (__handle_irq_event_percpu) ---\n");
        __u32 irq_key = 0, irq_next;
        int irq_total = 0;
        while (bpf_map_get_next_key(irq_fd, &irq_key, &irq_next) == 0) {
            struct irq_hist hist = {0};
            bpf_map_lookup_elem(irq_fd, &irq_next, &hist);
            if (hist.count > 0) {
                char tag[32];
                snprintf(tag, sizeof(tag), "cpu=%u", irq_next);
                print_hist("IRQ Latency", hist.slots, hist.count,
                           hist.total_latency_ns, tag);
                irq_total++;
            }
            irq_key = irq_next;
        }
        if (irq_total == 0) printf("  (no IRQ events yet)\n");

        /* Softirq latency */
        printf("\n--- Softirq Latency (__do_softirq) ---\n");
        __u32 softirq_key = 0, softirq_next;
        int softirq_total = 0;
        while (bpf_map_get_next_key(softirq_fd, &softirq_key, &softirq_next) == 0) {
            struct softirq_hist hist = {0};
            bpf_map_lookup_elem(softirq_fd, &softirq_next, &hist);
            if (hist.count > 0) {
                char tag[32];
                snprintf(tag, sizeof(tag), "cpu=%u", softirq_next);
                print_hist("Softirq Latency", hist.slots, hist.count,
                           hist.total_latency_ns, tag);
                softirq_total++;
            }
            softirq_key = softirq_next;
        }
        if (softirq_total == 0) printf("  (no softirq events yet)\n");

        printf("\n========================================================\n");
    }

    printf("\nStopping...\n");
    bpf_object__close(obj);
    return 0;
}