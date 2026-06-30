/**
 * shm_direct.h — Direct SHM reader for controller_online (zero CSV layer)
 *
 * Usage:
 *   #include "shm_direct.h"
 *   // in main():
 *   volatile uint8_t *mem = shm_map("39bus", &size, &fsz, &cap);
 *   while (1) {
 *       if (shm_read_frame(mem, cap, fsz, read_idx, z_k, NM, &k_time)) {
 *           ukf_step(...);
 *           read_idx++;
 *       }
 *   }
 */
#ifndef SHM_DIRECT_H
#define SHM_DIRECT_H

#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#define SHM_5BUS_BASE   0xC8100000UL
#define SHM_5BUS_SIZE   0x40000UL
#define SHM_39BUS_BASE  0xC8140000UL
#define SHM_39BUS_SIZE  0x80000UL
#define SHM_9BUS_BASE   0xC81C0000UL
#define SHM_9BUS_SIZE   0x20000UL

#define SHM_HDR_SIZE    16

static inline volatile uint8_t *shm_map(const char *node_name, size_t *out_size, int *out_fsz, int *out_cap)
{
    unsigned long base;
    size_t size;
    int fsz;

    if (strcmp(node_name, "5bus") == 0) {
        base = SHM_5BUS_BASE;  size = SHM_5BUS_SIZE;  fsz = 64;
    } else if (strcmp(node_name, "39bus") == 0) {
        base = SHM_39BUS_BASE; size = SHM_39BUS_SIZE; fsz = 400;
    } else if (strcmp(node_name, "9bus") == 0) {
        base = SHM_9BUS_BASE;  size = SHM_9BUS_SIZE;  fsz = 104;
    } else {
        fprintf(stderr, "ERROR: unknown node '%s'. Use: 5bus|39bus|9bus\n", node_name);
        return NULL;
    }

    int fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return NULL;
    }

    volatile uint8_t *mem = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, base);
    close(fd);

    if (mem == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    /* Wait for FreeRTOS to initialise SHM header (fsz != 0)
     * Use memory barrier to avoid stale reads on device memory.
     * IMPORTANT: Do NOT poll too aggressively - this can trigger
     * rpmsg sysfs notifications that may cause kernel Oops.
     */
    int timeout = 30000; /* 30 seconds, matches controller timeout */
    __sync_synchronize(); /* full memory barrier before first read */
    while (*(volatile uint32_t *)(mem + 12) == 0 && timeout-- > 0) {
        usleep(1000);
        __sync_synchronize(); /* ensure fresh read each iteration */
    }
    if (timeout <= 0) {
        fprintf(stderr, "ERROR: SHM not initialised (timeout)\n");
        munmap((void *)mem, size);
        return NULL;
    }

    *out_size = size;
    *out_fsz  = fsz;
    *out_cap  = (int)((size - SHM_HDR_SIZE) / fsz);
    return mem;
}

/**
 * Try to read one new frame from SHM.
 * Returns 1 if a new frame was read, 0 if no new data yet.
 *
 * IMPORTANT: We copy the frame into a local buffer via memcpy, then parse it.
 * Direct float* dereference on /dev/mem device memory causes SIGBUS (BUS_ADRALN)
 * because the compiler at -O3 generates SIMD instructions (LDP/STP) that require
 * strict alignment on device memory, while normal memory allows unaligned access.
 */
static inline int shm_read_frame(volatile uint8_t *mem, int cap, int fsz,
                                 int read_idx, double *z_k, int n_meas,
                                 double *k_time)
{
    uint32_t cnt = *(volatile uint32_t *)(mem + 8);
    if ((int)cnt <= read_idx) return 0;          /* No new frame */

    int slot = read_idx % cap;
    volatile uint8_t *src = mem + SHM_HDR_SIZE + slot * fsz;

    /* Copy frame into local buffer to avoid SIMD alignment issues on device memory */
    uint8_t fbuf[400];
    for (int i = 0; i < fsz; i++) {
        fbuf[i] = src[i];
    }

    uint32_t ts_ms = *(uint32_t *)fbuf;
    float *z_float = (float *)(fbuf + 8);

    for (int i = 0; i < n_meas; i++) {
        z_k[i] = (double)z_float[i];
    }
    *k_time = ts_ms / 1000.0;
    return 1;
}

static inline void shm_unmap(volatile uint8_t *mem, size_t size)
{
    munmap((void *)mem, size);
}

#endif /* SHM_DIRECT_H */
