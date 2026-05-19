#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

#define DEVICE_MASTER_TEST    0x0030U
#define DEVICE_MASTER_CMD     0x0021U
#define DEVICE_MASTER_DATA    0x0020U

#define TEST_SINGLE_FAULT     0x02
#define TEST_RESP_FAULT_SENT  0x02

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    uint8_t  data[496];
} ProtocolData;

typedef struct __attribute__((packed)) {
    uint8_t  subcmd;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint8_t  severity;
    uint16_t sample_count;
    uint8_t  reserved[2];
} TestCtrlPacket_t;

typedef struct __attribute__((packed)) {
    uint8_t  resp_code;
    uint8_t  subcmd_echo;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint32_t processed_count;
    uint32_t timestamp_ms;
} TestRespPacket_t;

static const char *fault_names[] = {
    "NONE", "OVER_VOLTAGE", "UNDER_VOLTAGE", "VOLTAGE_SAG", "VOLTAGE_SWELL", "TRANSIENT"
};
static const char *severity_names[] = {"NORMAL", "WARNING", "DANGER"};

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  TC02: Fault Injection Test                 │\n");
    printf("│  所有节点 × 所有故障类型组合                │\n");
    printf("└─────────────────────────────────────────────┘\n\n");

    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[FAIL] Cannot open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }

    ProtocolData hs = {.command = DEVICE_MASTER_DATA, .length = 0};
    write(fd, &hs, 6);
    usleep(200000);

    int pass = 0, fail = 0;

    for (int node = 0; node < 3; node++) {
        for (int type = 1; type <= 5; type++) {
            for (int sev = 0; sev <= 2; sev++) {
                ProtocolData tx;
                TestCtrlPacket_t ctrl;

                memset(&tx, 0, sizeof(tx));
                memset(&ctrl, 0, sizeof(ctrl));
                tx.command = DEVICE_MASTER_TEST;
                tx.length  = sizeof(ctrl);
                ctrl.subcmd    = TEST_SINGLE_FAULT;
                ctrl.node_id   = (uint8_t)node;
                ctrl.fault_type = (uint8_t)type;
                ctrl.severity  = (uint8_t)sev;
                ctrl.sample_count = 80;
                memcpy(tx.data, &ctrl, sizeof(ctrl));

                write(fd, &tx, 6 + tx.length);

                int ok = 0;
                for (int j = 0; j < 50; j++) {
                    ProtocolData rx;
                    usleep(5000);
                    ssize_t r = read(fd, &rx, sizeof(rx));
                    if (r < 6) continue;

                    TestRespPacket_t *resp = (TestRespPacket_t *)rx.data;
                    if (rx.command == DEVICE_MASTER_CMD &&
                        resp->subcmd_echo == TEST_SINGLE_FAULT &&
                        resp->resp_code == TEST_RESP_FAULT_SENT &&
                        resp->node_id == node &&
                        resp->fault_type == type) {
                        ok = 1;
                        break;
                    }
                }

                if (ok) {
                    pass++;
                    printf("  [OK] node%d %s/%s\n",
                           node, fault_names[type], severity_names[sev]);
                } else {
                    fail++;
                    printf("  [FAIL] node%d %s/%s — no response\n",
                           node, fault_names[type], severity_names[sev]);
                }

                usleep(50000);
            }
        }
    }

    printf("\n─── Results ───\n");
    printf("  Total: %d  PASS: %d  FAIL: %d\n", pass + fail, pass, fail);

    close(fd);

    if (fail > 0) {
        printf("\n[TC02] RESULT: FAIL (%d failures)\n", fail);
        return 1;
    }
    printf("\n[TC02] RESULT: PASS (all 45 combinations OK)\n");
    return 0;
}