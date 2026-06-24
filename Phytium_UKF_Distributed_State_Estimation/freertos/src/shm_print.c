#include "shm_print.h"
#include "ftypes.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define SHM_BASE  ((volatile u32 *)0xC8000000UL)
#define SHM_WI    (SHM_BASE[0])
#define SHM_RI    (SHM_BASE[1])
#define SHM_HB    (SHM_BASE[2])
#define SHM_BUF   ((volatile char *)(SHM_BASE + 3))
#define SHM_BSZ   (1024 * 1024 - 12)

static SemaphoreHandle_t shm_mutex;

static void shm_put(char c)
{
    u32 w = SHM_WI, r = SHM_RI, n = (w + 1) % SHM_BSZ;
    if (n != r) { SHM_BUF[w] = c; SHM_WI = n; }
}

void shm_print_init(void)
{
    shm_mutex = xSemaphoreCreateMutex();
}

void shm_puts(const char *s)
{
    if (shm_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(shm_mutex, portMAX_DELAY);
    }
    while (*s) shm_put(*s++);
    if (shm_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(shm_mutex);
    }
}

void shm_spf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    shm_puts(buf);
}

void shm_clear(void)
{
    SHM_WI = 0;
    SHM_RI = 0;
    SHM_HB = 0;
    volatile char *p = SHM_BUF;
    for (u32 k = 0; k < 65536; k++) p[k] = 0;
}

void shm_hb_inc(void)
{
    SHM_HB++;
}
