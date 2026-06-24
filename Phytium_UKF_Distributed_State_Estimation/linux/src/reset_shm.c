/*
 * reset_shm.c — 重置 FreeRTOS SHM 计数器
 * 用法: sudo ./reset_shm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define SHM_5BUS_BASE   0xC8100000UL
#define SHM_5BUS_SIZE   0x40000UL
#define SHM_39BUS_BASE  0xC8140000UL
#define SHM_39BUS_SIZE  0x80000UL   /* 512KB */
#define SHM_9BUS_BASE   0xC81C0000UL
#define SHM_9BUS_SIZE   0x20000UL

int main() {
    struct { uint32_t base; uint32_t size; const char *name; } regions[] = {
        {SHM_5BUS_BASE,  SHM_5BUS_SIZE,  "5bus"},
        {SHM_39BUS_BASE, SHM_39BUS_SIZE, "39bus"},
        {SHM_9BUS_BASE,  SHM_9BUS_SIZE,  "9bus"},
    };

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    for (int i = 0; i < 3; i++) {
        volatile uint32_t *shm = (volatile uint32_t *)mmap(NULL, regions[i].size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, regions[i].base);
        if (shm == MAP_FAILED) {
            printf("[%s] mmap FAILED\n", regions[i].name);
            continue;
        }

        uint32_t old_wi = shm[0], old_ri = shm[1], old_count = shm[2], old_fs = shm[3];
        printf("[%s] OLD: wi=%u ri=%u count=%u frame_sz=%u\n",
               regions[i].name, old_wi, old_ri, old_count, old_fs);

        /* 重置计数器, 保留 frame_sz */
        shm[0] = 0;  /* wi */
        shm[1] = 0;  /* ri */
        shm[2] = 0;  /* count */

        printf("[%s] NEW: wi=0 ri=0 count=0 frame_sz=%u\n", regions[i].name, old_fs);
        munmap((void *)shm, regions[i].size);
    }

    close(fd);
    printf("All SHM counters reset.\n");
    return 0;
}