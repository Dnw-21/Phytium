/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UKF_MONITOR_H
#define __UKF_MONITOR_H

#define TASK_COMM_LEN    16
#define MAX_SLOTS        32
#define MAX_IRQ_NAME     32
#define MAX_SOFTIRQ_NAME 16

/* ---- scheduling latency histogram ---- */
struct sched_hist {
    __u32 slots[MAX_SLOTS];   /* log2 histogram buckets (us) */
    __u32 count;
    __u64 total_latency_ns;
    char comm[TASK_COMM_LEN];
};

/* ---- IRQ latency histogram ---- */
struct irq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
    char name[MAX_IRQ_NAME];
};

/* ---- softirq latency histogram ---- */
struct softirq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
    __u32 vec; /* softirq vector number */
};

/* ---- events for perf_event_array output ---- */
struct sched_event {
    __u32 pid;
    __u32 prev_pid;
    __u64 delta_us;
    char task[TASK_COMM_LEN];
    char prev_task[TASK_COMM_LEN];
};

/* ---- configuration ---- */
struct config {
    __u64 min_sched_us;       /* only report sched latency > this */
    __u64 min_irq_us;         /* only report IRQ latency > this */
    __u64 min_softirq_us;     /* only report softirq latency > this */
    __u32 target_tgid;        /* filter: target process TGID (0 = all) */
    __u32 target_pid;         /* filter: target PID (0 = all) */
    char target_comm[TASK_COMM_LEN]; /* filter: process name prefix ("controller" = UKF) */
};

#endif /* __UKF_MONITOR_H */