/*
 * shm_reader.c — Linux 端共享内存读取器
 * =======================================
 * 从 /dev/mem 读取 FreeRTOS 写入的测量数据, 输出到 stdout。
 * 用法: sudo ./shm_reader <node>
 *   node: 5bus | 39bus | 9bus
 *
 * 编译: gcc -O2 -o shm_reader shm_reader.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define SHM_5BUS_BASE   0xC8100000UL
#define SHM_5BUS_SIZE   0x8000UL
#define SHM_39BUS_BASE  0xC8108000UL
#define SHM_39BUS_SIZE  0x10000UL
#define SHM_9BUS_BASE   0xC8118000UL
#define SHM_9BUS_SIZE   0x8000UL

typedef struct {
    volatile uint32_t wi;       /* write_index */
    volatile uint32_t ri;       /* read_index */
    volatile uint32_t count;    /* frame_count */
    volatile uint32_t frame_sz; /* frame_size */
    uint8_t data[];             /* ring buffer */
} ShmRegion;

int main(int argc, char *argv[]) {
    uint32_t base, size;
    if (argc < 2) { fprintf(stderr, "Usage: shm_reader <5bus|39bus|9bus>\n"); return 1; }
    if (!strcmp(argv[1], "5bus"))  { base = SHM_5BUS_BASE; size = SHM_5BUS_SIZE; }
    else if (!strcmp(argv[1], "39bus")) { base = SHM_39BUS_BASE; size = SHM_39BUS_SIZE; }
    else if (!strcmp(argv[1], "9bus")) { base = SHM_9BUS_BASE; size = SHM_9BUS_SIZE; }
    else { fprintf(stderr, "Unknown node: %s\n", argv[1]); return 1; }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    ShmRegion *shm = (ShmRegion *)mmap(NULL, size, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, base);
    if (shm == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "[shm-%s] base=0x%X size=%u frame_sz=%u wi=%u\n",
            argv[1], base, size, shm->frame_sz, shm->wi);

    uint32_t data_size = size - 16;
    uint32_t frame_sz = shm->frame_sz;

    /* Wait for first data */
    while (shm->count == 0) { usleep(10000); }

    /* Start from the latest frame: position WI - frame_sz in ring buffer */
    uint32_t start_count = shm->count;
    uint32_t ri = (shm->wi - frame_sz + data_size) % data_size;

    while (1) {
        uint32_t cur_count = shm->count;

        while (start_count <= cur_count) {
            uint8_t fbuf[500];
            for (uint32_t i = 0; i < frame_sz; i++) {
                fbuf[i] = shm->data[(ri + i) % data_size];
            }
            fwrite(fbuf, frame_sz, 1, stdout);
            ri = (ri + frame_sz) % data_size;
            start_count++;
        }

        if (start_count > cur_count) {
            usleep(5000);
        }
    }

    munmap((void *)shm, size);
    close(fd);
    return 0;
}
