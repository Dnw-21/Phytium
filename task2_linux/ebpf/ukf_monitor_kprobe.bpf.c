/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ukf_monitor_kprobe.bpf.c — eBPF kprobe version (no BTF/FTRACE required)
 *
 * Uses raw kprobe/kretprobe (no BPF_KPROBE macro, no arch headers needed).
 * Targets:
 *   - finish_task_switch  → detect UKF scheduling, measure interval
 *   - __handle_irq_event_percpu → IRQ handler latency
 *   - __do_softirq        → softirq latency
 *
 * Compile (VM cross):
 *   clang -target bpf -I/usr/include/x86_64-linux-gnu -I/usr/include \
 *         -g -O2 -c ukf_monitor_kprobe.bpf.c -o ukf_monitor_kprobe.bpf.o
 */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define TASK_COMM_LEN    16
#define MAX_SLOTS        32

/* ---- log2 helper ---- */
static __always_inline __u32 log2l(__u64 v)
{
    __u32 r = 0;
    while (v >>= 1) r++;
    return r;
}

/* ---- bpf_map_lookup_or_try_init ---- */
static __always_inline void *
bpf_map_lookup_or_try_init(void *map, const void *key, const void *init)
{
    void *val;
    val = bpf_map_lookup_elem(map, key);
    if (val)
        return val;
    if (bpf_map_update_elem(map, key, init, BPF_NOEXIST) == 0)
        return bpf_map_lookup_elem(map, key);
    return bpf_map_lookup_elem(map, key);
}

/* ---- filter: match "controller" prefix in comm ---- */
static __always_inline int is_ukf_comm(const char *comm)
{
    for (int i = 0; i < 11; i++) {
        if (comm[i] == '\0') return 0;
        if (comm[i] != "controller"[i]) return 0;
    }
    return 1;
}

/* ================================================================
 * 1. SCHEDULING INTERVAL
 *    kprobe on finish_task_switch → bpf_get_current_pid_tgid() = next task
 * ================================================================ */

struct sched_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
    char comm[TASK_COMM_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, __u64);
} sched_last SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct sched_hist);
} sched_hists SEC(".maps");

SEC("kprobe/finish_task_switch")
int kprobe_finish_task_switch(struct pt_regs *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = (__u32)(pid_tgid >> 32);
    __u32 tgid = (__u32)pid_tgid;

    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));
    if (!is_ukf_comm(comm))
        return 0;

    __u64 now = bpf_ktime_get_ns();

    __u64 *last = bpf_map_lookup_elem(&sched_last, &pid);
    if (last) {
        __s64 delta_ns = now - *last;
        if (delta_ns > 0) {
            struct sched_hist zero = {0};
            struct sched_hist *hist = bpf_map_lookup_or_try_init(&sched_hists, &tgid, &zero);
            if (hist) {
                if (!hist->comm[0])
                    __builtin_memcpy(hist->comm, comm, TASK_COMM_LEN);
                __u64 delta_us = delta_ns / 1000;
                __u32 slot = log2l(delta_us);
                if (slot >= MAX_SLOTS) slot = MAX_SLOTS - 1;
                __sync_fetch_and_add(&hist->slots[slot], 1);
                __sync_fetch_and_add(&hist->count, 1);
                __sync_fetch_and_add(&hist->total_latency_ns, delta_ns);
            }
        }
    }

    bpf_map_update_elem(&sched_last, &pid, &now, BPF_ANY);
    return 0;
}

/* ================================================================
 * 2. HARDIRQ LATENCY
 *    kprobe/kretprobe on __handle_irq_event_percpu
 * ================================================================ */

struct irq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} irq_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct irq_hist);
} irq_hists SEC(".maps");

SEC("kprobe/__handle_irq_event_percpu")
int kprobe_irq_entry(struct pt_regs *ctx)
{
    __u32 zero = 0;
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&irq_start, &zero, &ts, BPF_ANY);
    return 0;
}

SEC("kretprobe/__handle_irq_event_percpu")
int kretprobe_irq_exit(struct pt_regs *ctx)
{
    __u32 zero = 0;
    __u64 *tsp = bpf_map_lookup_elem(&irq_start, &zero);
    if (!tsp) return 0;

    __s64 delta_ns = bpf_ktime_get_ns() - *tsp;
    if (delta_ns < 0) return 0;

    struct irq_hist zero_h = {0};
    struct irq_hist *hist = bpf_map_lookup_or_try_init(&irq_hists, &zero, &zero_h);
    if (!hist) return 0;

    __u64 delta_us = delta_ns / 1000;
    __u32 slot = log2l(delta_us);
    if (slot >= MAX_SLOTS) slot = MAX_SLOTS - 1;
    __sync_fetch_and_add(&hist->slots[slot], 1);
    __sync_fetch_and_add(&hist->count, 1);
    __sync_fetch_and_add(&hist->total_latency_ns, delta_ns);
    return 0;
}

/* ================================================================
 * 3. SOFTIRQ LATENCY
 *    kprobe/kretprobe on __do_softirq
 * ================================================================ */

struct softirq_hist {
    __u32 slots[MAX_SLOTS];
    __u32 count;
    __u64 total_latency_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} softirq_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16);
    __type(key, __u32);
    __type(value, struct softirq_hist);
} softirq_hists SEC(".maps");

SEC("kprobe/__do_softirq")
int kprobe_softirq_entry(struct pt_regs *ctx)
{
    __u32 zero = 0;
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&softirq_start, &zero, &ts, BPF_ANY);
    return 0;
}

SEC("kretprobe/__do_softirq")
int kretprobe_softirq_exit(struct pt_regs *ctx)
{
    __u32 zero = 0;
    __u64 *tsp = bpf_map_lookup_elem(&softirq_start, &zero);
    if (!tsp) return 0;

    __s64 delta_ns = bpf_ktime_get_ns() - *tsp;
    if (delta_ns < 0) return 0;

    struct softirq_hist zero_h = {0};
    struct softirq_hist *hist = bpf_map_lookup_or_try_init(&softirq_hists, &zero, &zero_h);
    if (!hist) return 0;

    __u64 delta_us = delta_ns / 1000;
    __u32 slot = log2l(delta_us);
    if (slot >= MAX_SLOTS) slot = MAX_SLOTS - 1;
    __sync_fetch_and_add(&hist->slots[slot], 1);
    __sync_fetch_and_add(&hist->count, 1);
    __sync_fetch_and_add(&hist->total_latency_ns, delta_ns);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";