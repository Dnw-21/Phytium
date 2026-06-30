/*
 * ukf_pipeline_online.c — 在线实时 UKF Pipeline (compile-time specialized)
 * =======================================================================
 * 基于 multi_node 目录下的实时 UKF 实现, 每个节点编译为独立二进制。
 *
 * 数据流:
 *   FreeRTOS → SHM ring buffer → ukf_pipeline_online
 *                                    ↓
 *                              [ukf_init] (一次)
 *                              [ukf_step] (每帧)
 *                                    ↓
 *                              stdout CSV (实时输出)
 *                              stderr heartbeat (监控用)
 *
 * 编译:
 *   5bus:  aarch64-linux-gnu-gcc -O2 -static -DUKF_NODE_5BUS \
 *          -o ukf_pipeline_5bus ukf_pipeline_online.c -lm
 *   39bus: aarch64-linux-gnu-gcc -O2 -static -DUKF_NODE_39BUS \
 *          -o ukf_pipeline_39bus ukf_pipeline_online.c -lm
 *   9bus:  aarch64-linux-gnu-gcc -O2 -static -DUKF_NODE_9BUS \
 *          -o ukf_pipeline_9bus ukf_pipeline_online.c -lm
 *
 * 输出格式 (stdout, CSV):
 *   time,delta1,...,deltaN,omega1,...,omegaN,RMSE
 *
 * 输出格式 (stderr, 心跳):
 *   [ukf-NODE] t=4.0s frames=251 X=[0.31,0.68] rmse=0.0001 lat=55us
 *
 * 内存占用: 纯静态分配, 零 malloc/free
 *   5bus:  ~20KB  (P:128B, Q:128B, R:1568B, 状态+temporary: ~17KB)
 *   39bus: ~120KB (P:3200B, Q:3200B, R:76832B, 状态+temporary: ~37KB)
 *   9bus:  ~30KB  (P:288B, Q:288B, R:4608B, 状态+temporary: ~24KB)
 *
 * 与旧版(ukf_pipeline.c)的关键区别:
 *   1. 使用 multi_node 实时 UKF 算法 (分离 Q/R, 矩阵求逆 Kalman增益)
 *   2. CSV 文本输出 (而非二进制帧, 更易调试)
 *   3. 编译时节点专用化 (更小的矩阵, 更高的 cache 命中率)
 *   4. 完全不依赖 ukf_core.c (独立的 UKF 实现)
 * =======================================================================
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <math.h>
#include <sched.h>

#include "ukf_online_core.h"

/* ─── 包含系统参数头文件 (通过宏重命名) ─── */

/* 39bus params */
#if defined(UKF_NODE_39BUS)
#define SIM_39BUS_YBUS_REAL  sim39_ybus_real
#define SIM_39BUS_YBUS_IMAG  sim39_ybus_imag
#define SIM_39BUS_RV_REAL    sim39_rv_real
#define SIM_39BUS_RV_IMAG    sim39_rv_imag
#define SIM_39BUS_E_ABS      sim39_e_abs
#define SIM_39BUS_PM         sim39_pm
#define SIM_39BUS_M          sim39_m
#define SIM_39BUS_D          sim39_d
#define SIM_39BUS_X0         sim39_x0
#include "sim_params_39bus.h"
#undef SIM_39BUS_YBUS_REAL
#undef SIM_39BUS_YBUS_IMAG
#undef SIM_39BUS_RV_REAL
#undef SIM_39BUS_RV_IMAG
#undef SIM_39BUS_E_ABS
#undef SIM_39BUS_PM
#undef SIM_39BUS_M
#undef SIM_39BUS_D
#undef SIM_39BUS_X0
#endif

/* 9bus params */
#if defined(UKF_NODE_9BUS)
#define SIM_9BUS_YBUS_REAL  sim9_ybus_real
#define SIM_9BUS_YBUS_IMAG  sim9_ybus_imag
#define SIM_9BUS_RV_REAL    sim9_rv_real
#define SIM_9BUS_RV_IMAG    sim9_rv_imag
#define SIM_9BUS_E_ABS      sim9_e_abs
#define SIM_9BUS_PM         sim9_pm
#define SIM_9BUS_M          sim9_m
#define SIM_9BUS_D          sim9_d
#define SIM_9BUS_X0         sim9_x0
#include "sim_params_9bus.h"
#undef SIM_9BUS_YBUS_REAL
#undef SIM_9BUS_YBUS_IMAG
#undef SIM_9BUS_RV_REAL
#undef SIM_9BUS_RV_IMAG
#undef SIM_9BUS_E_ABS
#undef SIM_9BUS_PM
#undef SIM_9BUS_M
#undef SIM_9BUS_D
#undef SIM_9BUS_X0
#endif

/* ─── SHM 地址 (编译时选择, 匹配 FreeRTOS 固件实际地址) ─── */
#if defined(UKF_NODE_5BUS)
  #define SHM_BASE   0xC8100000UL
  #define SHM_SIZE   0x40000UL   /* 256KB */
#elif defined(UKF_NODE_39BUS)
  #define SHM_BASE   0xC8140000UL
  #define SHM_SIZE   0x80000UL   /* 512KB */
#elif defined(UKF_NODE_9BUS)
  #define SHM_BASE   0xC81C0000UL
  #define SHM_SIZE   0x20000UL   /* 128KB */
#endif

/* ─── SHM 帧头结构 ─── */
typedef struct __attribute__((packed)) {
    uint32_t ts_ms;
    uint16_t seq;
    uint8_t  flags;
    uint8_t  gen_count;
    /* followed by float Z[N_MEAS] */
} ShmFrameHdr;

/* ─── 环形缓冲区 ─── */
typedef struct {
    volatile uint32_t wi;
    volatile uint32_t ri;
    volatile uint32_t count;
    volatile uint32_t frame_sz;
    uint8_t data[];
} ShmRegion;

/* ─── 全局 ─── */
static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ─── 系统参数初始化 ─── */

/* 5bus/2-gen 参数 */
#if defined(UKF_NODE_5BUS)
static void init_params(UKFParams *sp) {
    memset(sp, 0, sizeof(*sp));
    sp->n_gen = 2; sp->n_bus = 5;
    sp->ns = 4; sp->nm = 14; sp->np = 8;
    {
        const char *env_dt = getenv("UKF_DELTT");
        sp->deltt = env_dt ? atof(env_dt) : 0.0005;
        sp->fs = 1.0 / sp->deltt;
    }
    sp->fault_start = 5.0; sp->fault_end = 5.3;
    sp->voltage_format = 0;

    double yb_r[2][2][3] = {
        {{2.03667035, 1.62427823, 2.14589667}, {0.67555233, 0.02985437, 0.56404147}},
        {{0.67555233, 0.02985437, 0.56404147}, {5.79625866, 5.05810121, 5.90243101}}
    };
    double yb_i[2][2][3] = {
        {{-20.57319889, -20.86205835, -20.55705842}, {4.95647982, 4.35963697, 5.02663622}},
        {{4.95647982, 4.35963697, 5.02663622}, {-14.11308801, -14.47263687, -14.05308844}}
    };
    memcpy(sp->YBUS_real, yb_r, sizeof(yb_r));
    memcpy(sp->YBUS_imag, yb_i, sizeof(yb_i));

    double rv_r[5][2][3] = {
        {{1.04805804, 1.05805804, 1.04805804}, {-0.04332918, -0.04332918, -0.04332918}},
        {{0.99011682, 0.99883152, 0.99025818}, {-0.04452782, -0.04452782, -0.04452782}},
        {{0.96389199, 0.97188282, 0.96417782}, {-0.04605870, -0.04605870, -0.04605870}},
        {{0.98765432, 0.99654321, 0.98765432}, {-0.04444444, -0.04444444, -0.04444444}},
        {{0.97530864, 0.98419753, 0.97530864}, {-0.04567901, -0.04567901, -0.04567901}}
    };
    double rv_i[5][2][3] = {
        {{-0.11901316, -0.10903692, -0.11901316}, {0.0, 0.0, 0.0}},
        {{-0.11901316, -0.10903692, -0.11901316}, {0.0, 0.0, 0.0}},
        {{-0.11901316, -0.10903692, -0.11901316}, {0.0, 0.0, 0.0}},
        {{-0.11901316, -0.10903692, -0.11901316}, {0.0, 0.0, 0.0}},
        {{-0.11901316, -0.10903692, -0.11901316}, {0.0, 0.0, 0.0}}
    };
    memcpy(sp->RV_real, rv_r, sizeof(rv_r));
    memcpy(sp->RV_imag, rv_i, sizeof(rv_i));

    sp->E_abs[0] = 1.28377667; sp->E_abs[1] = 1.12407893;
    sp->PM[0] = 8.20212477; sp->PM[1] = 3.53179641;
    sp->M[0] = 0.02652582; sp->M[1] = 0.01591549;
    sp->D[0] = 0.0; sp->D[1] = 0.0;
    sp->X_0[0] = 0.31490084; sp->X_0[1] = -0.68510392;
    sp->X_0[2] = 0.0; sp->X_0[3] = 0.0;
}
#endif

/* 39bus/10-gen 参数 */
#if defined(UKF_NODE_39BUS)
static void init_params(UKFParams *sp) {
    memset(sp, 0, sizeof(*sp));
    sp->n_gen = 10; sp->n_bus = 39;
    sp->ns = 20; sp->nm = 98; sp->np = 40;
    {
        const char *env_dt = getenv("UKF_DELTT");
        sp->deltt = env_dt ? atof(env_dt) : 0.0005;
        sp->fs = 1.0 / sp->deltt;
    }
    sp->fault_start = 5.0; sp->fault_end = 5.3;
    sp->voltage_format = 0;

    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++)
            for (int k = 0; k < 3; k++) {
                sp->YBUS_real[i][j][k] = (double)sim39_ybus_real[i][j][k];
                sp->YBUS_imag[i][j][k] = (double)sim39_ybus_imag[i][j][k];
            }
    for (int i = 0; i < 39; i++)
        for (int j = 0; j < 10; j++)
            for (int k = 0; k < 3; k++) {
                sp->RV_real[i][j][k] = (double)sim39_rv_real[i][j][k];
                sp->RV_imag[i][j][k] = (double)sim39_rv_imag[i][j][k];
            }
    for (int i = 0; i < 10; i++) {
        sp->E_abs[i] = (double)sim39_e_abs[i];
        sp->PM[i] = (double)sim39_pm[i];
        sp->M[i] = (double)sim39_m[i];
        sp->D[i] = (double)sim39_d[i];
    }
    for (int i = 0; i < 20; i++) sp->X_0[i] = (double)sim39_x0[i];
}
#endif

/* 9bus/3-gen 参数 */
#if defined(UKF_NODE_9BUS)
static void init_params(UKFParams *sp) {
    memset(sp, 0, sizeof(*sp));
    sp->n_gen = 3; sp->n_bus = 9;
    sp->ns = 6; sp->nm = 24; sp->np = 12;
    {
        const char *env_dt = getenv("UKF_DELTT");
        sp->deltt = env_dt ? atof(env_dt) : 0.0005;
        sp->fs = 1.0 / sp->deltt;
    }
    sp->fault_start = 5.0; sp->fault_end = 5.3;
    sp->voltage_format = 1;  /* Vreal + Vimag */

    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                sp->YBUS_real[i][j][k] = (double)sim9_ybus_real[i][j][k];
                sp->YBUS_imag[i][j][k] = (double)sim9_ybus_imag[i][j][k];
            }
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                sp->RV_real[i][j][k] = (double)sim9_rv_real[i][j][k];
                sp->RV_imag[i][j][k] = (double)sim9_rv_imag[i][j][k];
            }
    for (int i = 0; i < 3; i++) {
        sp->E_abs[i] = (double)sim9_e_abs[i];
        sp->PM[i] = (double)sim9_pm[i];
        sp->M[i] = (double)sim9_m[i];
        sp->D[i] = (double)sim9_d[i];
    }
    for (int i = 0; i < 6; i++) sp->X_0[i] = (double)sim9_x0[i];
}
#endif

/* ─── 主程序 ─── */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    /* 全缓冲 stdout 并每 500 帧 flush, 减少每行 CSV 的 write 系统调用开销 */
    static char stdout_buf[65536];
    setvbuf(stdout, stdout_buf, _IOFBF, sizeof(stdout_buf));

    /* ── CPU affinity & polling config ── */
    const char *env_cpu = getenv("UKF_CPU");
    if (env_cpu) {
        int cpu = atoi(env_cpu);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0)
            fprintf(stderr, "[ukf-%s] Bound to CPU %d\n", UKF_NODE_NAME, cpu);
        else
            perror("[ukf-CPU] sched_setaffinity failed");
    }
    int poll_us = 1;
    const char *env_poll = getenv("UKF_POLL_US");
    if (env_poll) poll_us = atoi(env_poll);
    int busy_wait = 0;
    const char *env_busy = getenv("UKF_BUSY_WAIT");
    if (env_busy) busy_wait = atoi(env_busy);
    int readonly = 0;
    const char *env_ro = getenv("UKF_READONLY");
    if (env_ro) readonly = atoi(env_ro);
    fprintf(stderr, "[ukf-%s] poll_us=%d busy_wait=%d readonly=%d\n",
            UKF_NODE_NAME, poll_us, busy_wait, readonly);

    /* ── 1. 初始化系统参数 ── */
    UKFParams sp;
    init_params(&sp);

    /* ── 2. 初始化 UKF (在线模式: init一次, step多帧) ── */
    UKFState state;
    ukf_init(&state, &sp);

    fprintf(stderr, "[ukf-%s] init: %d-gen/%d-bus, ns=%d nm=%d vfmt=%d "
            "(online UKF, sigma_a=%.2f sigma_w=%.2f sigma_m=%.2f)\n",
            UKF_NODE_NAME, sp.n_gen, sp.n_bus, sp.ns, sp.nm, sp.voltage_format,
            UKF_SIG_A, UKF_SIG_W, UKF_SIG_M);

    /* ── 3. 打开 SHM ── */
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    ShmRegion *shm = (ShmRegion *)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, SHM_BASE);
    if (shm == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    fprintf(stderr, "[ukf-%s] SHM mapped: base=0x%lX size=0x%lX\n",
            UKF_NODE_NAME, (unsigned long)SHM_BASE, (unsigned long)SHM_SIZE);

    /* ── 4. 等待 FreeRTOS 初始化 SHM (count > 0 说明已写入数据) ── */
    while (shm->count == 0 && g_running) { usleep(10000); }
    uint32_t frame_sz = shm->frame_sz;
    uint32_t data_size = SHM_SIZE - 16;
    uint32_t buf_capacity = data_size / frame_sz;

    uint32_t total_count = shm->count;
    uint32_t last_count, ri;

    if (total_count >= buf_capacity) {
        /* slot = cnt % cap; 当 cnt >= cap 时 slot=0 已被覆盖为最新帧，
         * 最早可用帧是 cnt = total_count - buf_capacity + 1 */
        last_count = total_count - buf_capacity + 1;
    } else {
        last_count = 0;
    }
    /* 与 FreeRTOS shm_put_frame 一致: slot = cnt % cap, pos = slot * fsz */
    ri = (last_count % buf_capacity) * frame_sz;

    uint32_t total_frames = 0;
    uint32_t hb_count = 0;
    uint32_t max_count_seen = total_count;

    struct timespec t_wall0, t_wall1;
    clock_gettime(CLOCK_MONOTONIC, &t_wall0);

    fprintf(stderr, "[ukf-%s] running, count=%u wi=%u frame_sz=%u buf_cap=%u "
            "start=%u\n", UKF_NODE_NAME, total_count, shm->wi, frame_sz,
            buf_capacity, last_count);

    /* ── 5. 输出 CSV 头 ── */
    printf("# time");
    for (int i = 0; i < sp.n_gen; i++) printf(",delta%d", i + 1);
    for (int i = 0; i < sp.n_gen; i++) printf(",omega%d", i + 1);
    printf(",RMSE\n");
    fflush(stdout);

    /* ── 6. 主循环: 从 SHM 逐帧读取, 运行 ukf_step, 输出 CSV ── */
    double Z_buf[UKF_MAX_NM];
    uint8_t fbuf[1024];  /* max frame_sz = 400 for 39bus */
    struct timespec t_start, t_end;

    double lat_sum = 0;
    uint32_t lat_count = 0;
    uint32_t idle_count = 0;

    while (g_running) {
        uint32_t cur_count = shm->count;
        if (cur_count > max_count_seen) max_count_seen = cur_count;

        while (last_count < cur_count) {
            clock_gettime(CLOCK_MONOTONIC, &t_start);

            /* 从环形缓冲区读取一帧 (逐字节拷贝, 避免 /dev/mem 设备内存对齐问题) */
            for (uint32_t i = 0; i < frame_sz; i++)
                fbuf[i] = shm->data[(ri + i) % data_size];

            ShmFrameHdr *hdr = (ShmFrameHdr *)fbuf;
            float *Z_float = (float *)(fbuf + 8);

            /* float32 → double */
            for (int i = 0; i < sp.nm; i++)
                Z_buf[i] = (double)Z_float[i];

            double t = hdr->ts_ms / 1000.0;

            /* UKF step (在线实时: 每帧一个 step) */
            if (total_frames < 3) {
                fprintf(stderr, "[ukf-%s] frame=%u ts_ms=%u seq=%u Z[0]=%f Z[1]=%f\n",
                        UKF_NODE_NAME, total_frames, hdr->ts_ms, hdr->seq, Z_float[0], Z_float[1]);
            }
            double x_out[UKF_MAX_NS];
            double rmse_val = 0.0;
            int ret = ukf_step(&state, &sp, Z_buf, t, x_out, &rmse_val);

            clock_gettime(CLOCK_MONOTONIC, &t_end);

            if (ret == 0) {
                /* 延迟: SHM读取 → UKF输出 (微秒) */
                double lat_us = (t_end.tv_sec - t_start.tv_sec) * 1e6 +
                               (t_end.tv_nsec - t_start.tv_nsec) / 1e3;
                lat_sum += lat_us;
                lat_count++;

                /* 输出 CSV: time,delta1..,omega1..,RMSE */
                printf("%.6f", t);
                for (int i = 0; i < sp.ns; i++) printf(",%.8f", x_out[i]);
                printf(",%.8f\n", rmse_val);
            }

            total_frames++;
            hb_count++;
            last_count++;
            ri = (last_count % buf_capacity) * frame_sz;
            if (!readonly)
                shm->ri = last_count;   /* 通知 FreeRTOS 已消费, 解除背压 */
            else if (total_frames == 1)
                fprintf(stderr, "[ukf-%s] readonly mode: not updating SHM ri\n",
                        UKF_NODE_NAME);

            /* 心跳: 每 500 帧输出到 stderr, 并刷新 stdout 缓冲区 */
            if (hb_count >= 500) {
                fflush(stdout);
                double avg_lat = lat_count > 0 ? lat_sum / lat_count : 0;

                /* 构造 X 向量显示 (第一个 delta 和第一个 omega) */
                char xstr[128];
                if (sp.n_gen <= 2) {
                    snprintf(xstr, sizeof(xstr), "%.4f,%.4f",
                             state.X_hat[0], state.X_hat[1]);
                } else {
                    snprintf(xstr, sizeof(xstr), "%.4f,%.4f,...",
                             state.X_hat[0], state.X_hat[1]);
                }

                fprintf(stderr, "[ukf-%s] t=%.1fs frames=%u X=[%s] "
                        "rmse=%.4f lat=%.0fus\n",
                        UKF_NODE_NAME, t, total_frames, xstr, rmse_val, avg_lat);
                lat_sum = 0;
                lat_count = 0;
                hb_count = 0;
            }
        }

        /* 空闲检测 */
        if (last_count == cur_count) {
            idle_count++;
            if (idle_count == 400) {
                fprintf(stderr, "[ukf-%s] idle: %u frames processed, "
                        "waiting...\n", UKF_NODE_NAME, total_frames);
            } else if (idle_count % 4000 == 0 && idle_count > 400) {
                fprintf(stderr, "[ukf-%s] still idle (%us): %u frames total\n",
                        UKF_NODE_NAME, idle_count / 200, total_frames);
            }
        } else {
            idle_count = 0;
        }

        if (!busy_wait) usleep(poll_us);
        else sched_yield();
    }

    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &t_wall1);
    double wall_ms = (t_wall1.tv_sec - t_wall0.tv_sec) * 1e3 +
                     (t_wall1.tv_nsec - t_wall0.tv_nsec) / 1e6;
    uint32_t shm_produced = max_count_seen > total_count ? (max_count_seen - total_count) : 0;
    uint32_t dropped = shm_produced > total_frames ? (shm_produced - total_frames) : 0;
    double shm_fps = wall_ms > 0 ? shm_produced * 1000.0 / wall_ms : 0.0;
    double avg_lat = lat_count > 0 ? lat_sum / lat_count : 0.0;
    double fps = wall_ms > 0 ? total_frames * 1000.0 / wall_ms : 0.0;

    fprintf(stderr, "[ukf-%s] done: frames=%u avg_lat=%.1fus wall_ms=%.1f fps=%.1f shm_fps=%.1f shm_produced=%u dropped=%u\n",
            UKF_NODE_NAME, total_frames, avg_lat, wall_ms, fps, shm_fps, shm_produced, dropped);
    munmap((void *)shm, SHM_SIZE);
    close(fd);
    return 0;
}
