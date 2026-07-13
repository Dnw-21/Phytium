#ifndef __TASKS_H
#define __TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "data_frame.h"

#define NODE_MAX_COUNT     10    // 最大节点数量
#define MASTER_ADDR         0x000A  // 主节点地址值
#define SLAVE_ADDR_BASE     0x000B  // 从节点地址基础值
#define LORA_BUFFER_SIZE    256     // LORA接收缓冲区大小

#define RECV_QUEUE_LENGTH   16

typedef struct {
    uint8_t  sync_code[CHAOS_SYNC_SIZE];
    uint8_t  rx_type;
    uint16_t enc_len;
    uint8_t  enc_data[220];
} RecvPacket_t;

extern QueueHandle_t g_recv_queue;

#endif /* __TASKS_H */