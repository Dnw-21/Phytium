#include "hal_flash.h"
#include "hal_platform_gd32l23x.h"
#include "gd32l23x.h"
#include "gd32l23x_fmc.h"
#include <string.h>

typedef struct {
    uint32_t base_addr;
    uint32_t total_size;
    uint32_t page_size;
    uint8_t  storage_type;
} flash_partition_t;

static const flash_partition_t g_partitions[] = {
    [HAL_FLASH_PARTITION_STATUS] = {
        .base_addr    = HAL_FLASH_GD32L23X_STATUS_BASE,
        .total_size   = HAL_FLASH_GD32L23X_STATUS_SIZE,
        .page_size    = HAL_FLASH_GD32L23X_PAGE_SIZE,
        .storage_type = HAL_FLASH_TYPE_INTERNAL,
    },
    [HAL_FLASH_PARTITION_WAVE] = {
        .base_addr    = HAL_FLASH_GD32L23X_WAVE_BASE,
        .total_size   = HAL_FLASH_GD32L23X_WAVE_SIZE,
        .page_size    = HAL_FLASH_GD32L23X_PAGE_SIZE,
        .storage_type = HAL_FLASH_TYPE_INTERNAL,
    },
};

#define PARTITION_COUNT (sizeof(g_partitions) / sizeof(g_partitions[0]))

int hal_flash_init(void)
{
    return HAL_OK;
}

int hal_flash_get_info(hal_flash_partition_t partition, hal_flash_info_t *info)
{
    if (partition >= PARTITION_COUNT || !info) return HAL_ERR_INVAL;

    const flash_partition_t *p = &g_partitions[partition];
    info->base_addr    = p->base_addr;
    info->total_size   = p->total_size;
    info->page_size    = p->page_size;
    info->write_align  = 4;
    info->storage_type = p->storage_type;
    info->reserved[0]  = 0;
    info->reserved[1]  = 0;
    info->reserved[2]  = 0;
    return HAL_OK;
}

int hal_flash_erase(uint32_t addr, uint32_t size)
{
    uint32_t start = addr & ~(HAL_FLASH_GD32L23X_PAGE_SIZE - 1);
    uint32_t end   = (addr + size + HAL_FLASH_GD32L23X_PAGE_SIZE - 1) & ~(HAL_FLASH_GD32L23X_PAGE_SIZE - 1);

    fmc_unlock();
    for (uint32_t a = start; a < end; a += HAL_FLASH_GD32L23X_PAGE_SIZE) {
        fmc_page_erase(a);
    }
    fmc_lock();
    return HAL_OK;
}

int hal_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (len == 0) return HAL_OK;

    fmc_unlock();

    const uint32_t *src = (const uint32_t *)data;
    uint32_t words = (len + 3) / 4;
    uint32_t tail_bytes = len & 3;
    uint32_t aligned = words - (tail_bytes ? 1 : 0);
    uint32_t i;

    for (i = 0; i < aligned; i++) {
        fmc_word_program(addr + i * 4, src[i]);
    }

    if (tail_bytes) {
        uint32_t last_word = 0;
        memcpy(&last_word, &data[aligned * 4], tail_bytes);
        fmc_word_program(addr + aligned * 4, last_word);
    }

    fmc_lock();
    return HAL_OK;
}

int hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (len == 0) return HAL_OK;
    memcpy(buf, (const void *)addr, len);
    return HAL_OK;
}
