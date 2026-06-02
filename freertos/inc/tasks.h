#ifndef __TASKS_H
#define __TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "data_frame.h"

#define NODE_MAX_COUNT     10
#define MASTER_ADDR         0x000A
#define SLAVE_ADDR_BASE     0x000B
#define LORA_BUFFER_SIZE    256

#define RECV_QUEUE_LENGTH   16

typedef struct {
    uint64_t sync_code;
    uint8_t  rx_type;
    uint16_t enc_len;
    uint8_t  enc_data[220];
} RecvPacket_t;

extern QueueHandle_t g_recv_queue;

#endif