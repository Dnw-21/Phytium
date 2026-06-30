/*
 * shm2csv.c — SHM Ring Buffer → CSV stdout bridge (v2)
 * ================================================================
 * 正确处理环形缓冲区指针, 避免 speed=0 时数据覆盖导致读到垃圾数据。
 *
 * 用法:
 *   ./shm2csv --node 5bus  | python3 controller_online.py
 *   ./shm2csv --node 9bus  | ./controller_online_9bus
 */

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

/* ─── SHM 地址 ─── */
#define SHM_5BUS   0xC8100000UL
#define SHM_39BUS  0xC8140000UL
#define SHM_9BUS   0xC81C0000UL  /* 固件已更新 */

typedef struct {
    const char *name;
    unsigned long shm_base;
    unsigned long shm_size;
    int nm;
} NodeConfig;

static const NodeConfig nodes[] = {
    {"5bus",  SHM_5BUS,  0x40000, 14},   /* 256KB */
    {"39bus", SHM_39BUS, 0x80000, 98},   /* 512KB (匹配固件 2000Hz) */
    {"9bus",  SHM_9BUS,  0x20000, 24},   /* 128KB */
};

/* ─── SHM 布局 ─── */
typedef struct {
    volatile uint32_t wi;       /* +0: 写指针 (数据存储起始位置 mod data_sz) */
    volatile uint32_t ri;       /* +4: 读指针 (未使用, 固件忽略) */
    volatile uint32_t count;    /* +8: 已写入帧总数 */
    volatile uint32_t frame_sz; /* +12: 每帧字节数 */
    volatile uint8_t  data[];   /* +16: 环形数据 */
} ShmRegion;

static volatile int g_running = 1;
static void sig_handler(int sig) { (void)sig; g_running = 0; }

int main(int argc, char *argv[]) {
    const NodeConfig *cfg = &nodes[2];
    int ds = 1; /* 下采样: 1=全帧, N=每N帧取1帧 */

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--ds", 3) == 0 && i + 1 < argc)
            ds = atoi(argv[++i]);
        else if (strstr(argv[i], "5bus"))  cfg = &nodes[0];
        else if (strstr(argv[i], "39bus")) cfg = &nodes[1];
        else if (strstr(argv[i], "9bus"))  cfg = &nodes[2];
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    setvbuf(stdout, NULL, _IONBF, 0);

    int nm = cfg->nm;
    unsigned long base = cfg->shm_base;
    unsigned long shm_sz = cfg->shm_size;

    fprintf(stderr, "[shm2csv] %s nm=%d shm=0x%lX ds=%d\n", cfg->name, nm, base, ds);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    ShmRegion *shm = (ShmRegion *)mmap(NULL, shm_sz,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, base);
    if (shm == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    while (shm->frame_sz == 0 && g_running) { usleep(50000); }

    uint32_t fsz = shm->frame_sz;
    uint32_t dsz = (uint32_t)(shm_sz - 16);   /* 数据区大小 */
    uint32_t cap = dsz / fsz;       /* 环形缓冲区容量 (帧数) */

    uint32_t read_count = 0;
    uint32_t last_total = shm->count;
    uint32_t idle_ticks = 0;

    if (last_total > cap) {
        read_count = last_total - cap;
    }

    fprintf(stderr, "[shm2csv] fsz=%u dsz=%u cap=%u start=%u total=%u\n",
            fsz, dsz, cap, read_count, last_total);

    uint8_t rbuf[1024];
    float  *Zf = (float *)(rbuf + 8);
    uint32_t lines = 0;
    uint32_t skips = 0;
    uint32_t ds_cnt = 0;

    /* 主循环 */
    while (g_running) {
        uint32_t cur_total = shm->count;

        /* 环形缓冲区是否已被覆盖 */
        if (cur_total > read_count + cap) {
            uint32_t lost = cur_total - read_count - cap;
            read_count = cur_total - cap;
            skips += lost;
            fprintf(stderr, "[shm2csv] ! ring overflow, skipped %u frames (total skip=%u)\n",
                    lost, skips);
            last_total = cur_total;
            continue;
        }

        /* 逐帧输出 */
        while (read_count < cur_total) {
            /* v3: 帧级环形缓冲区 — 每帧固定槽位, 无包裹 */
            uint32_t slot = read_count % cap;
            volatile uint8_t *frame_src = shm->data + (slot * fsz);
            for (uint32_t i = 0; i < fsz; i++)
                rbuf[i] = frame_src[i];

            read_count++;

            /* 下采样 */
            ds_cnt++;
            if (ds_cnt < ds) continue;
            ds_cnt = 0;

            /* 解包并输出 */
            uint32_t ts_ms = *(uint32_t *)(rbuf);
            double t = ts_ms * 0.001;

            printf("%.6f", t);
            for (int i = 0; i < nm; i++)
                printf(",%.6f", (double)Zf[i]);
            printf("\n");
            fflush(stdout);

            lines++;
        }

        /* 检查结束: 仿真停止后, count 不再增长 (需要 count > 0) */
        if (read_count >= last_total && read_count > 0) {
            idle_ticks++;
            if (idle_ticks >= 3) {
                fprintf(stderr, "[shm2csv] done: %u lines, %u skipped\n", lines, skips);
                break;
            }
        } else {
            idle_ticks = 0;
        }
        last_total = shm->count;

        usleep(10000);
    }

    munmap((void *)shm, shm_sz);
    close(fd);
    return 0;
}
