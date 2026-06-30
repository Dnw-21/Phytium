/* shm_print_dump.c — 读取 FreeRTOS SHM 调试打印缓冲区 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define SHM_BASE 0xC8000000UL
#define SHM_SIZE (1024*1024)

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }
    volatile uint32_t *shm = (volatile uint32_t *)mmap(NULL, SHM_SIZE, PROT_READ, MAP_SHARED, fd, SHM_BASE);
    if (shm == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    uint32_t wi = shm[0];
    uint32_t ri = shm[1];
    uint32_t hb = shm[2];
    volatile char *buf = (volatile char *)(shm + 3);
    uint32_t bsz = SHM_SIZE - 12;

    printf("SHM_PRINT wi=%u ri=%u hb=%u bsz=%u\n", wi, ri, hb, bsz);

    if (wi == ri) { printf("(empty)\n"); munmap((void *)shm, SHM_SIZE); close(fd); return 0; }

    uint32_t r = ri;
    while (r != wi) {
        putchar(buf[r]);
        r = (r + 1) % bsz;
    }
    fflush(stdout);

    munmap((void *)shm, SHM_SIZE);
    close(fd);
    return 0;
}
