#include "master.h"
#include "shm_print.h"
#include <string.h>

static MasterNodeInfo_t    g_nodes[MASTER_MAX_NODES];
static MasterDownloadBuf_t g_dl_buf;
static uint32_t            g_sys_tick;

static uint8_t g_status_buf[MASTER_MAX_NODES][MASTER_FLASH_PER_NODE];

void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count)
{
    if (node_id >= MASTER_MAX_NODES || count > FAULT_UPLOAD_POINTS) return;
    uint8_t *base = g_status_buf[node_id];
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    if (size > MASTER_FLASH_PER_NODE) return;

    memset(base, 0, MASTER_FLASH_PER_NODE);
    memcpy(base, (const uint8_t *)data, size);
    g_nodes[node_id].has_status_data = 1;
    shm_spf("SAVE: node%d status %dpts (%dB) to SHM\r\n", node_id, count, size);
}

uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count)
{
    if (node_id >= MASTER_MAX_NODES || !g_nodes[node_id].has_status_data) return 0;
    uint16_t count = g_nodes[node_id].last_total_points;
    if (count > max_count) count = max_count;
    if (count > FAULT_UPLOAD_POINTS) count = FAULT_UPLOAD_POINTS;
    uint8_t *base = g_status_buf[node_id];
    uint32_t size = (uint32_t)count * sizeof(NodeSample_t);
    memcpy(buf, base, size);
    return count;
}

void master_flash_erase_node(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return;
    uint8_t *base = g_status_buf[node_id];
    memset(base, 0, MASTER_FLASH_PER_NODE);
    g_nodes[node_id].has_status_data = 0;
}

MasterDownloadBuf_t *master_get_download_buf(void)
{
    return &g_dl_buf;
}

void master_init(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(&g_dl_buf, 0, sizeof(g_dl_buf));
    memset(g_status_buf, 0, sizeof(g_status_buf));
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

    shm_spf("Master init: %d nodes, DLbuf=%dB, StatusSHM=%dB\r\n",
             MASTER_MAX_NODES, (int)sizeof(g_dl_buf),
             (int)sizeof(g_status_buf));
}

MasterNodeInfo_t *master_get_node_info(uint8_t node_id)
{
    if (node_id >= MASTER_MAX_NODES) return NULL;
    return &g_nodes[node_id];
}