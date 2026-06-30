/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ukf_monitor.bpf.c — eBPF kernel-side: 调度延迟 + 中断 + 软中断监控
 *
 * 参考: lmp/eBPF_Hub/runqlat, lmp/eBPF_Hub/runqslower
 *
 * 编译: clang -target bpf -g -O2 -c ukf_monitor.bpf.c -o ukf_monitor.bpf.o
 *
 * 挂载点:
 *   tracepoint/sched/sched_wakeup          — 记录进程唤醒时间戳
 *   tracepoint/sched/sched_wakeup_new      — 记录新进程唤醒时间戳
 *   tracepoint/sched/sched_switch          — 计算调度延迟
 *   tracepoint/irq/irq_handler_entry       — 记录IRQ开始时间
 *   tracepoint/irq/irq_handler_exit        — 计算IRQ处理延迟
 *   tracepoint/irq/softirq_entry           — 记录软中断开始时间
 *   tracepoint/irq/softirq_exit            — 计算软中断处理延迟
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include "ukf_monitor.h"

#define TASK_RUNNING  0

/* ---- bpf_map_lookup_or_try_init (from lmp maps.bpf.h) ---- */
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

/* ---- filter: only monitor UKF processes ---- */
static __always_inline int is_ukf_process(struct task_struct *task)
{
    char comm[TASK_COMM_LEN];
    BPF_CORE_READ_INTO(&comm, task, comm);
    /* match "controller" prefix */
    for (int i = 0; i < 11; i++) {
        if (comm[i] == '\0') return 0;
        if (comm[i] != "controller"[i]) return 0;
    }
    return 1;
}

/* ---- log2 helper ---- */
static __always_inline __u32 log2l(__u64 v)
{
    __u32 r = 0;
    while (v >>= 1) r++;
    return r;
}

/* ================================================================
 * 1. SCHEDULING LATENCY (run queue wait time)
 *    Tracepoints: sched_wakeup → sched_switch
 * ================================================================ */

/* map: pid → enqueue timestamp (ns) */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, __u64);
} sched_start SEC(".maps");

/* map: tgid → histogram */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct sched_hist);
} sched_hists SEC(".maps");

/* map: perf event output for slow sched events */
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} sched_events SEC(".maps");

static int trace_sched_enqueue(__u32 tgid, __u32 pid)
{
    if (!pid) return 0;
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&sched_start, &pid, &ts, BPF_ANY);
    return 0;
}

SEC("tp_btf/sched_wakeup")
int BPF_PROG(sched_wakeup, struct task_struct *p)
{
    return trace_sched_enqueue(p->tgid, p->pid);
}

SEC("tp_btf/sched_wakeup_new")
int BPF_PROG(sched_wakeup_new, struct task_struct *p)
{
    return trace_sched_enqueue(p->tgid, p->pid);
}

SEC("tp_btf/sched_switch")
int BPF_PROG(sched_switch, bool preempt, struct task_struct *prev, struct task_struct *next)
{
    /* involuntary switch: prev is still runnable → re-enqueue */
    if (prev->__state == TASK_RUNNING)
        trace_sched_enqueue(BPF_CORE_READ(prev, tgid), BPF_CORE_READ(prev, pid));

    if (!is_ukf_process(next)) return 0;

    __u32 pid = BPF_CORE_READ(next, pid);
    __u64 *tsp = bpf_map_lookup_elem(&sched_start, &pid);
    if (!tsp) return 0;

    __s64 delta_ns = bpf_ktime_get_ns() - *tsp;
    if (delta_ns < 0) goto cleanup;

    __u32 tgid = BPF_CORE_READ(next, tgid);
    struct sched_hist zero = {0};
    struct sched_hist *hist = bpf_map_lookup_or_try_init(&sched_hists, &tgid, &zero);
    if (!hist) goto cleanup;

    if (!hist->comm[0])
        bpf_probe_read_kernel_str(&hist->comm, sizeof(hist->comm), next->comm);

    __u64 delta_us = delta_ns / 1000;
    __u32 slot = log2l(delta_us);
    if (slot >= MAX_SLOTS) slot = MAX_SLOTS - 1;
    __sync_fetch_and_add(&hist->slots[slot], 1);
    __sync_fetch_and_add(&hist->count, 1);
    __sync_fetch_and_add(&hist->total_latency_ns, delta_ns);

    /* output slow events (>10us) to perf buffer */
    if (delta_us > 10) {
        struct sched_event ev = {0};
        ev.pid = pid;
        ev.prev_pid = BPF_CORE_READ(prev, pid);
        ev.delta_us = delta_us;
        bpf_probe_read_kernel_str(&ev.task, sizeof(ev.task), next->comm);
        bpf_probe_read_kernel_str(&ev.prev_task, sizeof(ev.prev_task), prev->comm);
        bpf_perf_event_output(ctx, &sched_events, BPF_F_CURRENT_CPU, &ev, sizeof(ev));
    }

cleanup:
    bpf_map_delete_elem(&sched_start, &pid);
    return 0;
}

/* ================================================================
 * 2. HARDIRQ LATENCY
 *    Tracepoints: irq_handler_entry → irq_handler_exit
 * ================================================================ */

/* map: irq number → entry timestamp (ns) */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, __u64);
} irq_start SEC(".maps");

/* map: irq number → histogram */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct irq_hist);
} irq_hists SEC(".maps");

SEC("tp_btf/irq_handler_entry")
int BPF_PROG(irq_handler_entry, int irq, struct irqaction *action)
{
    char name[MAX_IRQ_NAME] = {};
    bpf_probe_read_kernel_str(name, sizeof(name), action->name);
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&irq_start, &irq, &ts, BPF_ANY);

    /* init histogram name on first use */
    struct irq_hist zero = {0};
    struct irq_hist *hist = bpf_map_lookup_or_try_init(&irq_hists, &irq, &zero);
    if (hist && !hist->name[0])
        __builtin_memcpy(hist->name, name, MAX_IRQ_NAME);
    return 0;
}

SEC("tp_btf/irq_handler_exit")
int BPF_PROG(irq_handler_exit, int irq, int ret)
{
    __u64 *tsp = bpf_map_lookup_elem(&irq_start, &irq);
    if (!tsp) return 0;

    __s64 delta_ns = bpf_ktime_get_ns() - *tsp;
    bpf_map_delete_elem(&irq_start, &irq);
    if (delta_ns < 0) return 0;

    struct irq_hist *hist = bpf_map_lookup_elem(&irq_hists, &irq);
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
 *    Tracepoints: softirq_entry → softirq_exit
 * ================================================================ */

/* map: cpu → current softirq vec */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} softirq_start SEC(".maps");

/* map: vec → histogram */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16);
    __type(key, __u32);
    __type(value, struct softirq_hist);
} softirq_hists SEC(".maps");

SEC("tp_btf/softirq_entry")
int BPF_PROG(softirq_entry, unsigned int vec)
{
    __u32 zero = 0;
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&softirq_start, &zero, &ts, BPF_ANY);
    return 0;
}

SEC("tp_btf/softirq_exit")
int BPF_PROG(softirq_exit, unsigned int vec)
{
    __u32 zero = 0;
    __u64 *tsp = bpf_map_lookup_elem(&softirq_start, &zero);
    if (!tsp) return 0;

    __s64 delta_ns = bpf_ktime_get_ns() - *tsp;
    if (delta_ns < 0) return 0;

    struct softirq_hist zero_h = {0};
    struct softirq_hist *hist = bpf_map_lookup_or_try_init(&softirq_hists, &vec, &zero_h);
    if (!hist) return 0;

    if (!hist->vec) hist->vec = vec;

    __u64 delta_us = delta_ns / 1000;
    __u32 slot = log2l(delta_us);
    if (slot >= MAX_SLOTS) slot = MAX_SLOTS - 1;
    __sync_fetch_and_add(&hist->slots[slot], 1);
    __sync_fetch_and_add(&hist->count, 1);
    __sync_fetch_and_add(&hist->total_latency_ns, delta_ns);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";