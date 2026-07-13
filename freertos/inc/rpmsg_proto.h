#ifndef __RPMSG_PROTO_H
#define __RPMSG_PROTO_H

#include "ftypes.h"

#define RPMSG_MAX_PAYLOAD  489

#define CMD_LORA_RAW       0x0023U  // LoRa 原始数据
#define CMD_LORA_PARSED    0x0024U  // LoRa 解析数据
#define CMD_NODE_STATUS    0x0025U  // 节点头数据
#define CMD_HEARTBEAT      0x0030U  // 心跳
#define CMD_ECHO_REQ       0x0040U  // 回显请求
#define CMD_ECHO_RESP      0x0041U  // 回显响应

typedef struct __attribute__((packed)) {
    u32 command;
    u16 length;
    u8  data[RPMSG_MAX_PAYLOAD];
} RpmsgPkt;

#define RPMSG_PKT_HDR_SIZE  6
#define RPMSG_PKT_MAX_SIZE  (RPMSG_PKT_HDR_SIZE + RPMSG_MAX_PAYLOAD)

#endif
