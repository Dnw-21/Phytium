#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define DEVICE_MASTER_CMD     0x0021U
#define DEVICE_MASTER_TEST    0x0030U
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

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int duration = (argc > 1) ? atoi(argv[1]) : 10;
    if (duration < 1 || duration > 300) duration = 10;

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  TC05: Stress Test (repeated fault inject)  │\n");
    printf("│  Duration: %d seconds                       │\n", duration);
    printf("│  Rapid fire SINGLE_FAULT across all nodes    │\n");
    printf("└─────────────────────────────────────────────┘\n\n");

    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[FAIL] Cannot open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }

    ProtocolData hs = {.command = DEVICE_MASTER_DATA, .length = 0};
    write(fd, &hs, 6);
    usleep(200000);

    int sent = 0, ack = 0, timeout = 0;
    time_t t_start = time(NULL);
    int node = 0, ftype = 0, sev = 0;

    printf("  Stress injection started...\n");
    printf("  TARGET: max rate via repeated SINGLE_FAULT\n\n");

    while (time(NULL) - t_start < duration) {
        ProtocolData tx;
        TestCtrlPacket_t ctrl;

        memset(&tx, 0, sizeof(tx));
        memset(&ctrl, 0, sizeof(ctrl));
        tx.command = DEVICE_MASTER_TEST;
        tx.length  = sizeof(ctrl);
        ctrl.subcmd     = TEST_SINGLE_FAULT;
        ctrl.node_id    = (uint8_t)node;
        ctrl.fault_type = (uint8_t)(ftype + 1);
        ctrl.severity   = (uint8_t)(sev + 1);
        memcpy(tx.data, &ctrl, sizeof(ctrl));

        write(fd, &tx, 6 + tx.length);
        sent++;

        int got = 0;
        for (int j = 0; j < 15; j++) {
            ProtocolData rx;
            usleep(1000);
            ssize_t r = read(fd, &rx, sizeof(rx));
            if (r < 6) continue;
            TestRespPacket_t *resp = (TestRespPacket_t *)rx.data;
            if (rx.command == DEVICE_MASTER_CMD &&
                resp->subcmd_echo == TEST_SINGLE_FAULT &&
                resp->resp_code == TEST_RESP_FAULT_SENT) {
                ack++;
                got = 1;
                break;
            }
        }
        if (!got) timeout++;

        node++; if (node >= 3) node = 0;
        if (node == 0) {
            ftype++;
            if (ftype >= 5) { ftype = 0; sev++; if (sev >= 3) sev = 0; }
        }

        usleep(2000);
    }

    time_t elapsed = time(NULL) - t_start;
    if (elapsed < 1) elapsed = 1;

    printf("\n─── Results ───\n");
    printf("  Sent:     %d\n", sent);
    printf("  ACKed:    %d\n", ack);
    printf("  Timeout:  %d\n", timeout);
    printf("  Elapsed:  %lds\n", elapsed);
    printf("  Rate:     %d faults/s\n", sent / (int)elapsed);
    printf("  ACK rate: %.1f%%\n", sent > 0 ? (ack * 100.0 / sent) : 0.0);

    close(fd);

    int pass = (ack > 0 && ack >= sent * 70 / 100);
    if (pass) {
        printf("\n[TC05] RESULT: PASS (ACK rate %.1f%% >= 70%%)\n",
               ack * 100.0 / sent);
        return 0;
    }
    printf("\n[TC05] RESULT: FAIL (ACK rate %.1f%% < 70%%)\n",
           ack * 100.0 / sent);
    return 1;
}