/*
 * stress_monitor.c — 高并发 UKF 压力测试 + 实时资源监控
 *
 * 功能:
 *  1. fork/exec 启动 N 个 UKF 子进程，支持 taskset CPU 绑定
 *  2. perf_event_open 采集每进程软件事件 (task_clock, cpu_clock)
 *  3. /proc/[pid]/stat 采集调度统计
 *  4. /proc/stat /proc/meminfo /proc/interrupts 采集系统级指标
 *  5. ANSI 终端实时刷新显示
 *
 * 编译: aarch64-linux-gnu-gcc -O2 -o stress_monitor stress_monitor.c
 * 运行: sudo ./stress_monitor
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <time.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>

#define MAX_PROC 32
#define MAX_CPU  4
#define INTERVAL_MS 1000

static int use_opt = 0;  /* 0=FT版本, 1=OPT版本 */

typedef struct {
    pid_t pid;
    char name[64];
    int cpu;
    int fd_task;
    int fd_cpu;
    unsigned long long prev_task, prev_cpu;
    unsigned long long prev_utime, prev_stime;
    unsigned long long start_us;
    int active;
    /* schedstat: /proc/[pid]/schedstat */
    unsigned long long prev_run_delay;   /* wait_sum  (ns) */
    unsigned long long prev_nr_switches; /* nr_switches (voluntary + involuntary) */
} ProcInfo;

static volatile int g_running = 1;

static void sigint_handler(int sig) { g_running = 0; }

static unsigned long long get_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

/* ---- perf_event_open wrapper ---- */
static int perf_open(pid_t pid, uint32_t type, uint64_t config)
{
    struct perf_event_attr attr = {
        .type = type,
        .size = sizeof(attr),
        .config = config,
        .disabled = 1,
    };
    int fd = syscall(__NR_perf_event_open, &attr, pid, -1, -1, 0);
    if (fd < 0) {
        fprintf(stderr, "perf_event_open failed for pid %d: %s\n", 
                pid, strerror(errno));
        return -1;
    }
    if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) < 0) {
        fprintf(stderr, "PERF_EVENT_IOC_RESET failed: %s\n", strerror(errno));
    }
    return fd;
}

static unsigned long long perf_read(int fd)
{
    unsigned long long val = 0;
    if (fd >= 0) {
        ssize_t ret = read(fd, &val, sizeof(val));
        if (ret != sizeof(val)) {
            fprintf(stderr, "perf_read failed: ret=%zd, errno=%s\n", 
                    ret, strerror(errno));
            return 0;
        }
    }
    return val;
}

static void perf_start(int fd)
{
    if (fd >= 0) ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}

static void perf_stop(int fd)
{
    if (fd >= 0) ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
}

/* ---- /proc/[pid]/stat parser ---- */
static int read_proc_stat(pid_t pid, unsigned long long *utime,
                           unsigned long long *stime,
                           unsigned long long *minflt,
                           unsigned long long *majflt)
{
    char path[256], buf[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    fclose(f);

    char *p = strrchr(buf, ')');
    if (!p) return -1;
    p += 2; /* skip ') ' */

    int field = 3;
    char *tok = strtok(p, " ");
    *minflt = *majflt = *utime = *stime = 0;
    while (tok) {
        if (field == 10) *minflt = strtoull(tok, NULL, 10);
        if (field == 12) *majflt = strtoull(tok, NULL, 10);
        if (field == 14) *utime = strtoull(tok, NULL, 10);
        if (field == 15) *stime = strtoull(tok, NULL, 10);
        tok = strtok(NULL, " ");
        field++;
    }
    return 0;
}

/* ---- /proc/stat CPU parser ---- */
static int read_sys_cpu(double cpu_pct[MAX_CPU])
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char line[256];
    static unsigned long long prev_idle[MAX_CPU] = {0};
    static unsigned long long prev_total[MAX_CPU] = {0};

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu", 3) != 0) continue;
        int idx;
        if (line[3] == ' ') continue; /* skip aggregate 'cpu ' */
        if (sscanf(line + 3, "%d", &idx) != 1) continue;
        if (idx < 0 || idx >= MAX_CPU) continue;

        unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
        sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
        unsigned long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
        unsigned long long total_delta = total - prev_total[idx];
        unsigned long long idle_delta = idle - prev_idle[idx];
        if (total_delta > 0)
            cpu_pct[idx] = 100.0 * (1.0 - (double)idle_delta / total_delta);
        else
            cpu_pct[idx] = 0.0;
        prev_total[idx] = total;
        prev_idle[idx] = idle;
    }
    fclose(f);
    return 0;
}

/* ---- /proc/meminfo parser ---- */
static void read_meminfo(unsigned long long *mem_used_kb,
                          unsigned long long *mem_total_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char line[256];
    *mem_used_kb = *mem_total_kb = 0;
    unsigned long long total = 0, free = 0, buffers = 0, cached = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %llu", &total) == 1) *mem_total_kb = total;
        if (sscanf(line, "MemFree: %llu", &free) == 1) ;
        if (sscanf(line, "Buffers: %llu", &buffers) == 1) ;
        if (sscanf(line, "Cached: %llu", &cached) == 1) ;
    }
    fclose(f);
    *mem_used_kb = total - free - buffers - cached;
}

/* ---- /proc/stat ctxt parser ---- */
static unsigned long long read_ctxt(void)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    char line[256];
    unsigned long long val = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "ctxt %llu", &val) == 1) break;
    }
    fclose(f);
    return val;
}

/* ---- /proc/softirqs total parser ---- */
static unsigned long long read_softirqs_total(void)
{
    FILE *f = fopen("/proc/softirqs", "r");
    if (!f) return 0;
    char line[512];
    unsigned long long total = 0;
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && (*p < '0' || *p > '9')) p++;
        unsigned long long v;
        while (sscanf(p, "%llu", &v) == 1) {
            total += v;
            while (*p && *p != ' ') p++;
            while (*p && *p == ' ') p++;
        }
    }
    fclose(f);
    return total;
}

/* ---- /proc/interrupts total parser ---- */
static unsigned long long read_interrupts_total(void)
{
    FILE *f = fopen("/proc/interrupts", "r");
    if (!f) return 0;
    char line[512];
    unsigned long long total = 0;
    fgets(line, sizeof(line), f); /* skip header */
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && (*p < '0' || *p > '9')) p++;
        unsigned long long v;
        while (sscanf(p, "%llu", &v) == 1) {
            total += v;
            while (*p && *p != ' ') p++;
            while (*p && *p == ' ') p++;
        }
    }
    fclose(f);
    return total;
}

/* ---- /proc/[pid]/schedstat parser ---- */
static int read_schedstat(pid_t pid, unsigned long long *run_delay_ns,
                          unsigned long long *nr_switches)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/schedstat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    unsigned long long run_time, wait_sum;
    int nr_vol, nr_invol;
    int n = fscanf(f, "%llu %llu %d %d", &run_time, &wait_sum, &nr_vol, &nr_invol);
    fclose(f);
    if (n >= 2) {
        *run_delay_ns = wait_sum;
        *nr_switches = (n >= 4) ? (unsigned long long)(nr_vol + nr_invol) : 0;
        return 0;
    }
    return -1;
}

/* ---- /proc/loadavg parser ---- */
static void read_loadavg(double *l1, double *l5, double *l15)
{
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) { *l1 = *l5 = *l15 = 0; return; }
    fscanf(f, "%lf %lf %lf", l1, l5, l15);
    fclose(f);
}

/* ---- ANSI clear screen ---- */
static void clear_screen(void)
{
    printf("\033[2J\033[H");
}

/* ---- print horizontal bar ---- */
static void print_bar(double pct, int width)
{
    int filled = (int)(pct * width / 100.0);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;
    printf("[");
    for (int i = 0; i < width; i++)
        putchar(i < filled ? '#' : ' ');
    printf("]");
}

/* ---- CPU allocation: fixed per node type ---- */
static int ukf_cpu(const char *node, int instance_idx)
{
    /* Balance compute load: 5bus->CPU0, 39bus->CPU2, 9bus->CPU0 (lightweight filler) */
    if (strcmp(node, "5bus") == 0) return 0;
    if (strcmp(node, "9bus") == 0) return 0;
    if (strcmp(node, "39bus") == 0) return 2;
    return 0; /* default */
}

/* ---- spawn UKF child ---- */
static pid_t spawn_ukf(const char *exe, const char *node, int bind_cpu)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* Set library path for FT version only */
        if (!use_opt) {
            setenv("LD_LIBRARY_PATH", "/home/user/ukf/lib", 1);
        }

        /* Change to working directory where system_params files are */
        if (chdir("/home/user") != 0) {
            perror("chdir");
        }

        if (bind_cpu >= 0) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(bind_cpu, &set);
            int ret = sched_setaffinity(0, sizeof(set), &set);
            if (ret != 0) {
                fprintf(stderr, "[WARN] sched_setaffinity(CPU%d) failed: %s\n",
                        bind_cpu, strerror(errno));
            } else {
                fprintf(stderr, "[BIND] %s -> CPU%d (pid=%d)\n", node, bind_cpu, (int)getpid());
            }
        }
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
        execl(exe, exe, node, NULL);
        perror("exec");
        _exit(1);
    }
    /* parent: log binding info */
    fprintf(stderr, "[SPAWN] %s -> CPU%d pid=%d\n", node, bind_cpu, (int)pid);
    return pid;
}

/* ---- main ---- */
int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    ProcInfo procs[MAX_PROC] = {0};
    int nproc = 0;

    int argi = 1;
    while (argi < argc && nproc < MAX_PROC) {
        if (strcmp(argv[argi], "--opt") == 0) {
            use_opt = 1;
            /* argi++ handled at end of loop */
        } else if (strcmp(argv[argi], "--5bus") == 0 && argi + 1 < argc) {
            int cnt = atoi(argv[++argi]);
            for (int i = 0; i < cnt && nproc < MAX_PROC; i++) {
                procs[nproc].cpu = ukf_cpu("5bus", i);
                snprintf(procs[nproc].name, sizeof(procs[nproc].name),
                         "5bus#%d", i);
                const char *exe = use_opt
                    ? "/home/user/build5/controller_online_5bus_opt"
                    : "/home/user/build5/controller_online_5bus_ft";
                procs[nproc].pid = spawn_ukf(exe, "5bus", procs[nproc].cpu);
                nproc++;
            }
        } else if (strcmp(argv[argi], "--9bus") == 0 && argi + 1 < argc) {
            int cnt = atoi(argv[++argi]);
            for (int i = 0; i < cnt && nproc < MAX_PROC; i++) {
                procs[nproc].cpu = ukf_cpu("9bus", i);
                snprintf(procs[nproc].name, sizeof(procs[nproc].name),
                         "9bus#%d", i);
                const char *exe = use_opt
                    ? "/home/user/build9/controller_online_9bus_opt"
                    : "/home/user/build9/controller_online_9bus_ft";
                procs[nproc].pid = spawn_ukf(exe, "9bus", procs[nproc].cpu);
                nproc++;
            }
        } else if (strcmp(argv[argi], "--39bus") == 0 && argi + 1 < argc) {
            int cnt = atoi(argv[++argi]);
            for (int i = 0; i < cnt && nproc < MAX_PROC; i++) {
                procs[nproc].cpu = ukf_cpu("39bus", i);
                snprintf(procs[nproc].name, sizeof(procs[nproc].name),
                         "39bus#%d", i);
                const char *exe = use_opt
                    ? "/home/user/build39/controller_online_39bus_opt"
                    : "/home/user/build39/controller_online_39bus_ft";
                procs[nproc].pid = spawn_ukf(exe, "39bus", procs[nproc].cpu);
                nproc++;
            }
        }
        argi++;
    }

    if (nproc == 0) {
        procs[0].cpu = 0;
        strcpy(procs[0].name, "5bus#0");
        procs[0].pid = spawn_ukf("/home/user/build5/controller_online_5bus_ft", "5bus", 0);

        procs[1].cpu = 2;
        strcpy(procs[1].name, "9bus#0");
        procs[1].pid = spawn_ukf("/home/user/build9/controller_online_9bus_ft", "9bus", 2);

        procs[2].cpu = 0;
        strcpy(procs[2].name, "39bus#0");
        procs[2].pid = spawn_ukf("/home/user/build39/controller_online_39bus_ft", "39bus", 0);
        nproc = 3;
    }

    sleep(6); /* let children start, wait for FreeRTOS SHM init */

    /* open perf events */
    for (int i = 0; i < nproc; i++) {
        if (procs[i].pid <= 0) continue;
        procs[i].fd_task = perf_open(procs[i].pid, PERF_TYPE_SOFTWARE,
                                        PERF_COUNT_SW_TASK_CLOCK);
        procs[i].fd_cpu  = perf_open(procs[i].pid, PERF_TYPE_SOFTWARE,
                                        PERF_COUNT_SW_CPU_CLOCK);
        perf_start(procs[i].fd_task);
        perf_start(procs[i].fd_cpu);
        procs[i].start_us = get_us();
        procs[i].active = 1;
    }

    /* main monitoring loop */
    unsigned long long last_us = get_us();
    unsigned long long prev_ctxt = 0, prev_softirq = 0, prev_intr = 0;
    int first_loop = 1;
    while (g_running) {
        usleep(INTERVAL_MS * 1000);
        unsigned long long now_us = get_us();
        double dt_sec = (now_us - last_us) / 1e6;
        last_us = now_us;

        /* Stop all perf events before reading */
        for (int i = 0; i < nproc; i++) {
            if (!procs[i].active || procs[i].pid <= 0) continue;
            perf_stop(procs[i].fd_task);
            perf_stop(procs[i].fd_cpu);
        }

        clear_screen();
        printf("========== UKF Stress Monitor | %s ==========\n",
               nproc > 3 ? "HIGH LOAD" : "BASELINE");
        printf("Interval: %.1fs | Processes: %d\n\n", dt_sec, nproc);

        /* System CPU */
        double cpu_pct[MAX_CPU] = {0};
        read_sys_cpu(cpu_pct);
        printf("--- System CPU ---\n");
        for (int c = 0; c < MAX_CPU; c++) {
            printf("CPU%d: ", c);
            print_bar(cpu_pct[c], 30);
            printf(" %5.1f%%\n", cpu_pct[c]);
        }

        /* Memory */
        unsigned long long mem_used_kb, mem_total_kb;
        read_meminfo(&mem_used_kb, &mem_total_kb);
        double mem_pct = 100.0 * mem_used_kb / mem_total_kb;
        printf("\n--- Memory ---\nUsed: %.1f%% (%llu/%llu MB)\n",
               mem_pct, mem_used_kb / 1024, mem_total_kb / 1024);
        print_bar(mem_pct, 30);
        printf("\n");

        /* Load & interrupts & context switches */
        double l1, l5, l15;
        read_loadavg(&l1, &l5, &l15);
        unsigned long long ctxt = read_ctxt();
        unsigned long long softirq = read_softirqs_total();
        unsigned long long intr = read_interrupts_total();
        double ctxt_rate = first_loop ? 0 : (double)(ctxt - prev_ctxt) / dt_sec;
        double softirq_rate = first_loop ? 0 : (double)(softirq - prev_softirq) / dt_sec;
        double intr_rate = first_loop ? 0 : (double)(intr - prev_intr) / dt_sec;
        prev_ctxt = ctxt;
        prev_softirq = softirq;
        prev_intr = intr;
        first_loop = 0;

        printf("\n--- System Load / Interrupts / Context Switches ---\n");
        printf("Load: %.2f %.2f %.2f | CTX/s: %.0f | IRQ/s: %.0f | SOFTIRQ/s: %.0f\n",
               l1, l5, l15, ctxt_rate, intr_rate, softirq_rate);

        /* Per-process stats */
        printf("\n--- Per-Process (task-clock/s, cpu-clock/s, sched delay) ---\n");
        printf("%-12s %8s %12s %12s %10s %10s %6s\n",
               "NAME", "PID", "TASK_CLK/s", "CPU_CLK/s", "RUN_DLY(us)", "CTX_SW/s", "CPU%%");
        for (int i = 0; i < nproc; i++) {
            if (!procs[i].active || procs[i].pid <= 0) continue;

            unsigned long long task = perf_read(procs[i].fd_task);
            unsigned long long cpu  = perf_read(procs[i].fd_cpu);

            unsigned long long ut, st, mi, ma;
            if (read_proc_stat(procs[i].pid, &ut, &st, &mi, &ma) != 0) {
                procs[i].active = 0;
                continue;
            }

            double task_rate = (double)(task - procs[i].prev_task) / dt_sec;
            double cpu_rate  = (double)(cpu - procs[i].prev_cpu) / dt_sec;

            unsigned long long ut_delta = ut - procs[i].prev_utime;
            unsigned long long st_delta = st - procs[i].prev_stime;
            double proc_cpu = 100.0 * (ut_delta + st_delta) / (dt_sec * sysconf(_SC_CLK_TCK));

            /* schedstat */
            unsigned long long run_delay_ns = 0, nr_sw = 0;
            double run_delay_us = 0, ctxsw_rate = 0;
            if (read_schedstat(procs[i].pid, &run_delay_ns, &nr_sw) == 0) {
                run_delay_us = (double)(run_delay_ns - procs[i].prev_run_delay) / 1000.0;
                ctxsw_rate = (double)(nr_sw - procs[i].prev_nr_switches) / dt_sec;
                procs[i].prev_run_delay = run_delay_ns;
                procs[i].prev_nr_switches = nr_sw;
            }

            printf("%-12s %8d %12.2e %12.2e %10.1f %10.0f %6.1f\n",
                   procs[i].name, procs[i].pid,
                   task_rate, cpu_rate, run_delay_us, ctxsw_rate, proc_cpu);

            procs[i].prev_task = task;
            procs[i].prev_cpu = cpu;
            procs[i].prev_utime = ut;
            procs[i].prev_stime = st;

            perf_start(procs[i].fd_task);
            perf_start(procs[i].fd_cpu);
        }

        printf("\n[Q/q/Ctrl+C] Quit\n");
        fflush(stdout);
    }

    /* cleanup */
    printf("\nStopping all UKF processes...\n");
    for (int i = 0; i < nproc; i++) {
        if (procs[i].pid > 0) {
            perf_stop(procs[i].fd_task);
            kill(procs[i].pid, SIGTERM);
            waitpid(procs[i].pid, NULL, 0);
            close(procs[i].fd_task);
            close(procs[i].fd_cpu);
        }
    }
    printf("Done.\n");
    return 0;
}
