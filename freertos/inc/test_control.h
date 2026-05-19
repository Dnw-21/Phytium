#ifndef __TEST_CONTROL_H
#define __TEST_CONTROL_H

#include <stdint.h>

/* RPMsg 测试控制命令 */
#define DEVICE_MASTER_TEST       0x0030U

/* 测试子命令 */
#define TEST_PING                0x01
#define TEST_SINGLE_FAULT        0x02
#define TEST_CONTINUOUS          0x03
#define TEST_STOP                0x04
#define TEST_FLASH_CHECK         0x05
#define TEST_CHAOS_ENCRYPT       0x06
#define TEST_STATUS              0x07

/* 测试响应代码 */
#define TEST_RESP_OK             0x00
#define TEST_RESP_PONG           0x01
#define TEST_RESP_FAULT_SENT     0x02
#define TEST_RESP_RUNNING        0x03
#define TEST_RESP_STOPPED        0x04
#define TEST_RESP_FLASH_OK       0x05
#define TEST_RESP_ENCRYPT_OK     0x06
#define TEST_RESP_ERROR          0xFF

/* 测试控制结构体 (RPMsg payload) */
typedef struct __attribute__((packed)) {
    uint8_t  subcmd;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint8_t  severity;
    uint16_t sample_count;
    uint8_t  reserved[2];
} TestCtrlPacket_t;

/* 测试响应结构体 (RPMsg payload) */
typedef struct __attribute__((packed)) {
    uint8_t  resp_code;
    uint8_t  subcmd_echo;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint32_t processed_count;
    uint32_t timestamp_ms;
} TestRespPacket_t;

void test_control_init(void);
int  test_control_handle(const TestCtrlPacket_t *ctrl, TestRespPacket_t *resp);
void test_control_get_status(uint32_t *total_faults, uint32_t *total_packets,
                             uint32_t *uptime_ms);

#endif