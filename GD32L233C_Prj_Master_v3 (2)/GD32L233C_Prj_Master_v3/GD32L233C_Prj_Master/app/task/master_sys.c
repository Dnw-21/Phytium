#include "master.h"
#include "gd32l23x_fmc.h"
#include "log.h"
#include <string.h>

/*============================================================================
 *  全局变量
 *============================================================================*/
static MasterNodeInfo_t    g_nodes[MASTER_MAX_NODES];
static MasterDownloadBuf_t g_dl_buf;
static uint32_t            g_sys_tick;

/*============================================================================
 *  Flash 工具函数
 *============================================================================*/
#define FMC_PAGE_SIZE   4096
static inline uint32_t status_flash_addr(uint8_t node_id)
{
    return MASTER_STATUS_FLASH_BASE + (uint32_t)node_id * MASTER_FLASH_PER_NODE;
}

static void flash_page_align_erase(uint32_t addr, uint32_t size)
{
    uint32_t start = addr & ~(FMC_PAGE_SIZE - 1);
    uint32_t end   = (addr + size + FMC_PAGE_SIZE - 1) & ~(FMC_PAGE_SIZE - 1);
    fmc_unlock();
    for (uint32_t a = start; a < end; a += FMC_PAGE_SIZE)
        fmc_page_erase(a);
    fmc_lock();
}

static void flash_write_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    fmc_unlock();
    uint32_t i = 0;
    while (i + 4 <= len) {
        uint32_t word;
        memcpy(&word, &data[i], 4);
        fmc_word_program(addr + i, word);
        i += 4;
    }
    if (i < len) {
        uint32_t last_word = 0;
        memcpy(&last_word, &data[i], len - i);
        fmc_word_program(addr + i, last_word);
    }
    fmc_lock();
}

static void flash_read_bytes(uint32_t addr, uint8_t *buf, uint32_t len)
{
    memcpy(buf, (const void *)addr, len);
}

/*============================================================================
 *  master_flash_save_node_data: 保存故障节点数据 → 状态区 Flash
 *============================================================================*/
void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count)
{
    if (node_id >= MASTER_MAX_NODES || count > FAULT_UPLOAD_POINTS) return;
    uint32_t base = status_flash_addr(node_id);
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    if (size > MASTER_FLASH_PER_NODE) return;

    flash_page_align_erase(base, MASTER_FLASH_PER_NODE);
    flash_write_bytes(base, (const uint8_t *)data, size);
    g_nodes[node_id].has_status_data = 1;
    log_info("SAVE: node%d status %dpts (%dB) to Flash", node_id, count, size);
}

/*============================================================================
 *  master_flash_load_node_data: 从状态区 Flash 读取节点数据
 *============================================================================*/
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count)
{
    if (node_id >= MASTER_MAX_NODES || !g_nodes[node_id].has_status_data) return 0;
    uint16_t count = g_nodes[node_id].last_total_points;
    if (count > max_count) count = max_count;
    if (count > FAULT_UPLOAD_POINTS) count = FAULT_UPLOAD_POINTS;
    uint32_t addr = status_flash_addr(node_id);
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    flash_read_bytes(addr, (uint8_t *)buf, size);
    return count;
}

void master_flash_erase_node(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return;
    uint32_t base = status_flash_addr(node_id);
    flash_page_align_erase(base, MASTER_FLASH_PER_NODE);
    g_nodes[node_id].has_status_data = 0;
}

/*============================================================================
 *  master_get_download_buf
 *============================================================================*/
MasterDownloadBuf_t *master_get_download_buf(void)
{
    return &g_dl_buf;
}

/*============================================================================
 *  master_init: 主控系统初始化
 *============================================================================*/
void master_init(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(&g_dl_buf, 0, sizeof(g_dl_buf));
    g_sys_tick = 0;

    for (uint8_t i = 0; i < MASTER_MAX_NODES; i++) {
        g_nodes[i].node_id = i;
        g_nodes[i].is_online = 0;
        g_nodes[i].has_status_data = 0;
        g_nodes[i].severity = SEVERITY_NORMAL;
        g_nodes[i].fault_type = FAULT_NONE;
        g_nodes[i].last_recv_time = 0;
        g_nodes[i].fault_count = 0;
    }

    log_info("Master init: %d nodes, DLbuf=%dB, StatusFlash=0x%08X",
             MASTER_MAX_NODES, sizeof(g_dl_buf), MASTER_STATUS_FLASH_BASE);
}

/*============================================================================
 *  公共查询接口
 *============================================================================*/
MasterNodeInfo_t *master_get_node_info(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return NULL;
    return &g_nodes[node_id];
}
