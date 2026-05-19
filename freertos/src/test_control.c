#include "test_control.h"
#include "master.h"
#include "data_frame.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>

extern void master_recv_inject_data(const uint8_t *data, uint16_t len);

#define TEST_WAVE_SAMPLES  80
#define TEST_PATTERN       0xA5

static uint32_t g_test_total_faults  = 0;
static uint32_t g_test_total_packets = 0;
static uint32_t g_test_start_ms      = 0;
static uint8_t  g_test_running       = 0;

void test_control_init(void)
{
    g_test_total_faults  = 0;
    g_test_total_packets = 0;
    g_test_start_ms      = 0;
    g_test_running       = 0;
}

static void fill_test_frame(uint8_t *buf, uint16_t *out_len,
                             uint8_t node_id, uint8_t fault_type,
                             uint8_t severity, uint16_t sample_count)
{
    FaultUploadHeader_t hdr;
    uint16_t payload_len, data_len, frame_len;
    uint32_t timestamp;
    uint8_t crc;
    int off, i;

    memset(&hdr, 0, sizeof(hdr));
    hdr.data_type    = DATA_TYPE_STATUS;
    hdr.severity     = severity;
    hdr.timestamp    = (uint32_t)(g_test_total_packets * 1000);
    hdr.fault_type   = (FaultType_t)fault_type;
    hdr.node_index   = node_id;
    hdr.total_points = sample_count;
    hdr.sample_rate  = NODE_SAMPLE_RATE;

    payload_len = sizeof(FaultUploadHeader_t);
    data_len    = 10 + payload_len;
    frame_len   = 4 + data_len + 3;

    if (frame_len > 512) {
        *out_len = 0;
        return;
    }

    off = 0;
    buf[off++] = 0xAA;
    buf[off++] = 0x55;
    buf[off++] = (uint8_t)(data_len >> 8);
    buf[off++] = (uint8_t)(data_len);

    timestamp = hdr.timestamp;
    buf[off++] = (uint8_t)(timestamp >> 24);
    buf[off++] = (uint8_t)(timestamp >> 16);
    buf[off++] = (uint8_t)(timestamp >> 8);
    buf[off++] = (uint8_t)(timestamp);

    buf[off++] = DATA_TYPE_STATUS;
    buf[off++] = (uint8_t)(node_id);
    memcpy(buf + off, &hdr, payload_len);
    off += payload_len;

    crc = 0;
    for (i = 4; i < off; i++) crc ^= buf[i];
    buf[off++] = crc;

    buf[off++] = 0x55;
    buf[off++] = 0xAA;

    *out_len = (uint16_t)off;
}

int test_control_handle(const TestCtrlPacket_t *ctrl, TestRespPacket_t *resp)
{
    if (!ctrl || !resp) return -1;

    memset(resp, 0, sizeof(*resp));
    resp->subcmd_echo = ctrl->subcmd;

    switch (ctrl->subcmd) {
    case TEST_PING: {
        resp->resp_code = TEST_RESP_PONG;
        resp->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        log_info("TEST: PING received, responding PONG");
        break;
    }

    case TEST_SINGLE_FAULT: {
        uint16_t frame_len;
        uint8_t frame_buf[512];

        fill_test_frame(frame_buf, &frame_len,
                        ctrl->node_id, ctrl->fault_type,
                        ctrl->severity, TEST_WAVE_SAMPLES);

        if (frame_len > 0) {
            master_recv_inject_data(frame_buf, frame_len);
            g_test_total_faults++;
            g_test_total_packets++;
            resp->resp_code      = TEST_RESP_FAULT_SENT;
            resp->node_id        = ctrl->node_id;
            resp->fault_type     = ctrl->fault_type;
            resp->processed_count = g_test_total_packets;
            resp->timestamp_ms   = xTaskGetTickCount() * portTICK_PERIOD_MS;

            log_info("TEST: FAULT node=%d type=%d sev=%d",
                     ctrl->node_id, ctrl->fault_type, ctrl->severity);
        } else {
            resp->resp_code = TEST_RESP_ERROR;
        }
        break;
    }

    case TEST_CONTINUOUS: {
        if (!g_test_running) {
            g_test_running  = 1;
            g_test_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        resp->resp_code = TEST_RESP_RUNNING;
        resp->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        log_info("TEST: CONTINUOUS mode started, node=%d type=%d",
                 ctrl->node_id, ctrl->fault_type);
        break;
    }

    case TEST_STOP: {
        g_test_running = 0;
        resp->resp_code = TEST_RESP_STOPPED;
        resp->processed_count = g_test_total_packets;
        resp->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        log_info("TEST: STOPPED, total_pkts=%lu", g_test_total_packets);
        break;
    }

    case TEST_FLASH_CHECK: {
        MasterNodeInfo_t *node = master_get_node_info(ctrl->node_id);
        if (node) {
            resp->resp_code = TEST_RESP_FLASH_OK;
            resp->node_id   = ctrl->node_id;
            resp->processed_count = node->fault_count;
            resp->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        } else {
            resp->resp_code = TEST_RESP_ERROR;
        }
        break;
    }

    case TEST_CHAOS_ENCRYPT: {
        uint8_t test_in[16];
        uint8_t test_out[16];
        uint8_t test_dec[16];
        uint8_t matched = 1;
        int i;

        for (i = 0; i < 16; i++) test_in[i] = (uint8_t)(i * 17 + TEST_PATTERN);
        chaos_encrypt_block(test_in, 16);
        memcpy(test_out, test_in, 16);
        chaos_decrypt_block(test_out, 16);
        memcpy(test_dec, test_out, 16);

        for (i = 0; i < 16; i++) {
            if (test_in[i] != test_dec[i]) { matched = 0; break; }
        }

        resp->resp_code = matched ? TEST_RESP_ENCRYPT_OK : TEST_RESP_ERROR;
        resp->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        log_info("TEST: CHAOS encrypt/decrypt %s", matched ? "PASS" : "FAIL");
        break;
    }

    case TEST_STATUS: {
        resp->resp_code     = g_test_running ? TEST_RESP_RUNNING : TEST_RESP_STOPPED;
        resp->processed_count = g_test_total_packets;
        resp->timestamp_ms  = xTaskGetTickCount() * portTICK_PERIOD_MS;
        break;
    }

    default:
        resp->resp_code = TEST_RESP_ERROR;
        break;
    }

    return 0;
}

void test_control_get_status(uint32_t *total_faults, uint32_t *total_packets,
                             uint32_t *uptime_ms)
{
    if (total_faults)  *total_faults  = g_test_total_faults;
    if (total_packets) *total_packets = g_test_total_packets;
    if (uptime_ms)     *uptime_ms     = (g_test_running && g_test_start_ms > 0)
                      ? (xTaskGetTickCount() * portTICK_PERIOD_MS - g_test_start_ms)
                      : 0;
}