#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define DEVICE_MASTER_TEST    0x0030U
#define DEVICE_MASTER_CMD     0x0021U
#define DEVICE_MASTER_DATA    0x0020U
#define DEVICE_CORE_CHECK     0x0003U

#define TEST_PING             0x01
#define TEST_RESP_PONG        0x01

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

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int count = (argc > 1) ? atoi(argv[1]) : 10;
    if (count < 1 || count > 1000) count = 10;

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  TC01: RPMsg Link Test (PING)               │\n");
    printf("│  Ping count: %d                             │\n", count);
    printf("└─────────────────────────────────────────────┘\n\n");

    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[FAIL] Cannot open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }

    /* Handshake */
    ProtocolData hs = {.command = DEVICE_MASTER_DATA, .length = 0};
    write(fd, &hs, 6);
    usleep(200000);

    /* Diagnostics: verify channel works via DEVICE_CORE_CHECK */
    printf("  [DIAG] Testing DEVICE_CORE_CHECK echo...\n");
    {
        ProtocolData dtx = {.command = DEVICE_CORE_CHECK, .length = 0};
        write(fd, &dtx, 6);
        int echo_ok = 0;
        for (int j = 0; j < 20; j++) {
            ProtocolData rx;
            usleep(10000);
            ssize_t r = read(fd, &rx, sizeof(rx));
            if (r < 6) continue;
            if (rx.command == DEVICE_CORE_CHECK) {
                echo_ok = 1;
                break;
            }
            /* drain any other messages (e.g. handshake response) */
        }
        printf("  [DIAG] DEVICE_CORE_CHECK: %s\n", echo_ok ? "OK" : "FAIL (echo not received)");
    }

    /* Diagnostics: verify DEVICE_MASTER_TEST path */
    printf("  [DIAG] Testing DEVICE_MASTER_TEST...\n");
    {
        ProtocolData dtx;
        TestCtrlPacket_t dc;
        memset(&dtx, 0, sizeof(dtx));
        memset(&dc, 0, sizeof(dc));
        dtx.command = DEVICE_MASTER_TEST;
        dtx.length  = sizeof(dc);
        dc.subcmd = TEST_PING;
        memcpy(dtx.data, &dc, sizeof(dc));
        write(fd, &dtx, 6 + dtx.length);
        int mtest_ok = 0;
        for (int j = 0; j < 30; j++) {
            ProtocolData rx;
            usleep(20000);
            ssize_t r = read(fd, &rx, sizeof(rx));
            if (r < 6) continue;
            if (rx.command == DEVICE_MASTER_CMD) {
                TestRespPacket_t *resp = (TestRespPacket_t *)rx.data;
                if (resp->subcmd_echo == TEST_PING && resp->resp_code == TEST_RESP_PONG) {
                    mtest_ok = 1;
                    break;
                }
            }
        }
        printf("  [DIAG] DEVICE_MASTER_TEST: %s\n", mtest_ok ? "OK" : "FAIL (no response)");
    }
    printf("\n");

    int pass = 0, fail = 0;
    int total_rtt = 0, min_rtt = 999999, max_rtt = 0;

    struct timespec t1, t2;

    for (int i = 1; i <= count; i++) {
        ProtocolData tx;
        TestCtrlPacket_t ctrl;

        memset(&tx, 0, sizeof(tx));
        memset(&ctrl, 0, sizeof(ctrl));
        tx.command = DEVICE_MASTER_TEST;
        tx.length  = sizeof(ctrl);
        ctrl.subcmd = TEST_PING;
        memcpy(tx.data, &ctrl, sizeof(ctrl));

        clock_gettime(CLOCK_MONOTONIC, &t1);
        write(fd, &tx, 6 + tx.length);

        int got_resp = 0;
        for (int j = 0; j < 50; j++) {
            ProtocolData rx;
            usleep(5000);
            ssize_t r = read(fd, &rx, sizeof(rx));
            if (r < 6) continue;

            TestRespPacket_t *resp = (TestRespPacket_t *)rx.data;
            if (rx.command == DEVICE_MASTER_CMD &&
                resp->subcmd_echo == TEST_PING &&
                resp->resp_code == TEST_RESP_PONG) {
                clock_gettime(CLOCK_MONOTONIC, &t2);
                int rtt_us = (t2.tv_sec - t1.tv_sec) * 1000000 +
                             (t2.tv_nsec - t1.tv_nsec) / 1000;
                pass++;
                total_rtt += rtt_us;
                if (rtt_us < min_rtt) min_rtt = rtt_us;
                if (rtt_us > max_rtt) max_rtt = rtt_us;
                if (i <= 5 || i % 20 == 0)
                    printf("  [%3d/%3d] PONG rtt=%dus\n", i, count, rtt_us);
                got_resp = 1;
                break;
            }
        }

        if (!got_resp) {
            fail++;
            printf("  [%3d/%3d] TIMEOUT\n", i, count);
        }

        usleep(50000);
    }

    int avg_rtt = pass > 0 ? total_rtt / pass : 0;
    printf("\n─── Results ───\n");
    printf("  PASS: %d  FAIL: %d  TOTAL: %d\n", pass, fail, count);
    printf("  Avg RTT: %dus  Min: %dus  Max: %dus\n", avg_rtt, min_rtt, max_rtt);

    close(fd);

    if (fail > 0) {
        printf("\n[TC01] RESULT: FAIL (%d/%d timeout)\n", fail, count);
        return 1;
    }
    printf("\n[TC01] RESULT: PASS (100%% success)\n");
    return 0;
}