/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ukf_monitor.c — eBPF 用户态加载器
 *
 * 加载 ukf_monitor.bpf.o，读取 BPF maps 并输出统计信息到终端。
 * 设计为可以和 stress_monitor 并行运行，或在串口终端独立运行。
 *
 * 编译: gcc -o ukf_monitor ukf_monitor.c -lbpf -lelf -lz
 * 用法: sudo ./ukf_monitor [interval_sec]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "ukf_monitor.h"

static volatile int running = 1;

static void sigint_handler(int sig)
{
    running = 0;
}

/* softirq name lookup */
static const char *softirq_name(unsigned int vec)
{
    static const char *names[] = {
        "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK",
        "IRQ_POLL", "TASKLET", "SCHED", "HRTIMER", "RCU"
    };
    if (vec < sizeof(names) / sizeof(names[0]))
        return names[vec];
    return "???";
}

/* print log2 histogram */
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

int main(int argc, char *argv[])
{
    int interval = (argc > 1) ? atoi(argv[1]) : 2;
    if (interval < 1) interval = 1;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    /* load BPF object */
    struct bpf_object *obj = bpf_object__open("ukf_monitor.bpf.o");
    if (!obj) {
        fprintf(stderr, "ERROR: cannot open ukf_monitor.bpf.o\n");
        fprintf(stderr, "  Run: bpftool gen skeleton ukf_monitor.bpf.o > /dev/null\n");
        fprintf(stderr, "  Or:  clang -target bpf -g -O2 -c ukf_monitor.bpf.c -o ukf_monitor.bpf.o\n");
        return 1;
    }

    if (bpf_object__load(obj)) {
        fprintf(stderr, "ERROR: bpf_object__load failed: %s\n", strerror(errno));
        bpf_object__close(obj);
        return 1;
    }

    /* attach all auto-attachable programs */
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, obj) {
        const char *name = bpf_program__name(prog);
        struct bpf_link *link = bpf_program__attach(prog);
        if (!link) {
            fprintf(stderr, "WARN: cannot attach %s: %s\n", name, strerror(errno));
            continue;
        }
        printf("attached: %s\n", name);
    }

    printf("\n=== UKF eBPF Monitor ===\n");
    printf("Collecting: scheduling latency, IRQ latency, softirq latency\n");
    printf("Interval: %ds | Press Ctrl+C to stop\n\n", interval);

    /* get map FDs */
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
        printf("\033[2J\033[H");  /* clear screen */

        printf("========== UKF eBPF Monitor | Interval: %ds ==========\n\n", interval);

        /* ---- Scheduling Latency ---- */
        printf("--- Scheduling Latency (UKF processes only) ---\n");
        __u32 sched_key = 0, next_key;
        int sched_total = 0;
        while (bpf_map_get_next_key(sched_fd, &sched_key, &next_key) == 0) {
            struct sched_hist hist = {0};
            bpf_map_lookup_elem(sched_fd, &next_key, &hist);
            if (hist.count > 0) {
                print_hist("Sched Latency", hist.slots, hist.count,
                           hist.total_latency_ns, hist.comm);
                sched_total++;
            }
            sched_key = next_key;
        }
        if (sched_total == 0) printf("  (no UKF scheduling events yet)\n\n");

        /* ---- IRQ Latency ---- */
        printf("\n--- IRQ Handler Latency (top 10) ---\n");
        __u32 irq_key = 0, irq_next;
        struct irq_entry { __u32 key; struct irq_hist hist; };
        struct irq_entry irq_entries[256];
        int irq_cnt = 0;
        while (bpf_map_get_next_key(irq_fd, &irq_key, &irq_next) == 0) {
            bpf_map_lookup_elem(irq_fd, &irq_next, &irq_entries[irq_cnt].hist);
            irq_entries[irq_cnt].key = irq_next;
            irq_cnt++;
            irq_key = irq_next;
        }
        /* sort by count descending */
        for (int i = 0; i < irq_cnt-1; i++)
            for (int j = i+1; j < irq_cnt; j++)
                if (irq_entries[j].hist.count > irq_entries[i].hist.count) {
                    struct irq_entry tmp = irq_entries[i];
                    irq_entries[i] = irq_entries[j];
                    irq_entries[j] = tmp;
                }
        int shown = 0;
        for (int i = 0; i < irq_cnt && shown < 10; i++) {
            if (irq_entries[i].hist.count == 0) continue;
            printf("  IRQ %-3d [%-25s] count=%d avg=%.1fus\n",
                   irq_entries[i].key,
                   irq_entries[i].hist.name,
                   irq_entries[i].hist.count,
                   (double)irq_entries[i].hist.total_latency_ns /
                   irq_entries[i].hist.count / 1000.0);
            shown++;
        }
        if (shown == 0) printf("  (no IRQ events yet)\n");

        /* ---- Softirq Latency ---- */
        printf("\n--- Softirq Latency ---\n");
        __u32 softirq_key = 0, softirq_next;
        int softirq_total = 0;
        while (bpf_map_get_next_key(softirq_fd, &softirq_key, &softirq_next) == 0) {
            struct softirq_hist hist = {0};
            bpf_map_lookup_elem(softirq_fd, &softirq_next, &hist);
            if (hist.count > 0) {
                printf("  %-10s [vec=%u] count=%d avg=%.1fus\n",
                       softirq_name(hist.vec), hist.vec, hist.count,
                       (double)hist.total_latency_ns / hist.count / 1000.0);
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