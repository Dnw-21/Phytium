#include "master.h"
#include "log.h"
#include <string.h>

/*============================================================================
 *  全局变量
 *============================================================================*/
static MasterNodeInfo_t    g_nodes[MASTER_MAX_NODES];
static MasterDownloadBuf_t g_dl_buf;
static uint32_t            g_sys_tick;

/*============================================================================
 *  共享内存模拟 Flash: status区 和 wave区
 *
 *  原来 GD32 使用内部 Flash 分区存储, 移植到 Phytium 后使用共享内存数组模拟.
 *  状态数据区: g_status_buf, MASTER_MAX_NODES × MASTER_FLASH_PER_NODE
 *  波形数据区: g_wave_buf, MASTER_MAX_NODES × MASTER_FLASH_PER_NODE
 *
 *  "擦除"操作 = memset 全零
 *============================================================================*/
static uint8_t g_status_buf[MASTER_MAX_NODES][MASTER_FLASH_PER_NODE];
static uint8_t g_wave_buf[MASTER_MAX_NODES][MASTER_FLASH_PER_NODE];

static void shm_write_bytes(uint8_t *dst, const uint8_t *data, uint32_t len)
{
    memcpy(dst, data, len);
}

static void shm_read_bytes(const uint8_t *src, uint8_t *buf, uint32_t len)
{
    memcpy(buf, src, len);
}

static void shm_erase(uint8_t *buf, uint32_t size)
{
    memset(buf, 0, size);
}

static uint8_t *status_buf_addr(uint8_t node_id)
{
    return g_status_buf[node_id];
}

static uint8_t *wave_buf_addr(uint8_t node_id)
{
    return g_wave_buf[node_id];
}

/*============================================================================
 *  master_flash_save_node_data: 保存节点状态数据 -> 共享内存
 *============================================================================*/
void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count)
{
    if (node_id >= MASTER_MAX_NODES || count > MASTER_NODE_UPLOAD_POINTS) return;
    uint8_t *base = status_buf_addr(node_id);
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    if (size > MASTER_FLASH_PER_NODE) return;

    shm_erase(base, MASTER_FLASH_PER_NODE);
    shm_write_bytes(base, (const uint8_t *)data, size);
    g_nodes[node_id].has_status_data = 1;
    log_info("SAVE: node%d status %dpts (%dB) to SHM", node_id, count, size);
}

uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count)
{
    if (node_id >= MASTER_MAX_NODES || !g_nodes[node_id].has_status_data) return 0;
    uint16_t count = g_nodes[node_id].last_total_points;
    if (count > max_count) count = max_count;
    if (count > MASTER_NODE_UPLOAD_POINTS) count = MASTER_NODE_UPLOAD_POINTS;
    uint8_t *base = status_buf_addr(node_id);
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    shm_read_bytes(base, (uint8_t *)buf, size);
    return count;
}

void master_flash_erase_node(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return;
    uint8_t *base = status_buf_addr(node_id);
    shm_erase(base, MASTER_FLASH_PER_NODE);
    g_nodes[node_id].has_status_data = 0;
}

/*============================================================================
 *  master_flash_save_wave_data: 波形数据 -> 波形区共享内存
 *
 *  收到波形头时先擦除整个该节点波形区, 后续逐包追加写入
 *============================================================================*/
void master_flash_save_wave_data(uint8_t node_id, const uint8_t *data, uint16_t len,
                                  uint32_t offset)
{
    if (node_id >= MASTER_MAX_NODES || len == 0) return;
    uint8_t *base = wave_buf_addr(node_id);
    if (offset + len > MASTER_FLASH_PER_NODE) return;

    if (offset == 0)
        shm_erase(base, MASTER_FLASH_PER_NODE);
    shm_write_bytes(base + offset, data, len);
    g_nodes[node_id].has_wave_data = 1;
}

uint16_t master_flash_load_wave_data(uint8_t node_id, uint8_t *buf, uint16_t len)
{
    if (node_id >= MASTER_MAX_NODES || !g_nodes[node_id].has_wave_data) return 0;
    uint8_t *base = wave_buf_addr(node_id);
    uint32_t total = g_nodes[node_id].last_wave_samples * sizeof(int16_t);
    if (len > total) len = (uint16_t)total;
    shm_read_bytes(base, buf, len);
    return len;
}

void master_flash_erase_wave(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return;
    uint8_t *base = wave_buf_addr(node_id);
    shm_erase(base, MASTER_FLASH_PER_NODE);
    g_nodes[node_id].has_wave_data = 0;
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
    memset(g_status_buf, 0, sizeof(g_status_buf));
    memset(g_wave_buf, 0, sizeof(g_wave_buf));
    g_sys_tick = 0;

    for (uint8_t i = 0; i < MASTER_MAX_NODES; i++) {
        g_nodes[i].node_id = i;
        g_nodes[i].is_online = 0;
        g_nodes[i].has_status_data = 0;
        g_nodes[i].has_wave_data = 0;
        g_nodes[i].severity = SEVERITY_NORMAL;
        g_nodes[i].fault_type = FAULT_NONE;
        g_nodes[i].last_recv_time = 0;
        g_nodes[i].fault_count = 0;
        g_nodes[i].wave_pending = 0;
        g_nodes[i].cmd_retry = 0;
        g_nodes[i].has_last_wave_hdr = 0;
        g_nodes[i].last_wave_fault_idx = 0xFF;
    }

    log_info("Master init: %d nodes, DLbuf=%dB, SHM=[S:%dB W:%dB]",
             MASTER_MAX_NODES, (int)sizeof(g_dl_buf),
             (int)sizeof(g_status_buf), (int)sizeof(g_wave_buf));
}

/*============================================================================
 *  公共查询接口
 *============================================================================*/
MasterNodeInfo_t *master_get_node_info(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return NULL;
    return &g_nodes[node_id];
}

void master_recv_wave_data(uint8_t node_id, uint16_t count)
{
    if (node_id >= MASTER_MAX_NODES) return;
    MasterNodeInfo_t *n = &g_nodes[node_id];
    n->has_wave_data = 1;
    n->last_wave_samples = count;
    n->last_recv_time = g_sys_tick;
}
