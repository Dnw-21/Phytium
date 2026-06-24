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
#define DEVICE_MASTER_DATA    0x0020U

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    uint8_t  data[496];
} ProtocolData;

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int timeout = 10;

    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  TC03: Command Transmission Test            │\n");
    printf("│  FreeRTOS → Linux: 观察命令传输             │\n");
    printf("│  Timeout: %d seconds                        │\n", timeout);
    printf("└─────────────────────────────────────────────┘\n\n");

    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("[FAIL] Cannot open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }

    ProtocolData hs = {.command = DEVICE_MASTER_DATA, .length = 0};
    write(fd, &hs, 6);
    usleep(200000);

    printf("  Listening for DEVICE_MASTER_CMD (0x0021)...\n");

    int count = 0;
    time_t start = time(NULL);
    int first_node = -1, last_cmd = -1;

    while (time(NULL) - start < timeout) {
        ProtocolData rx;
        ssize_t r = read(fd, &rx, sizeof(rx));
        if (r >= 6 && rx.command == DEVICE_MASTER_CMD) {
            count++;
            if (count == 1) {
                first_node = rx.data[0];
                last_cmd = rx.data[1];
            }
            if (count <= 5 || count % 10 == 0) {
                printf("  [%3d] node=%d cmd=0x%02X params=%d bytes\n",
                       count, rx.data[0], rx.data[1],
                       rx.length > 2 ? rx.length - 2 : 0);
            }
            last_cmd = rx.data[1];
        }
        usleep(20000);
    }

    time_t elapsed = time(NULL) - start;
    printf("\n─── Results ───\n");
    printf("  Commands received: %d\n", count);
    printf("  Elapsed: %lds\n", elapsed);
    printf("  Rate: %.1f commands/s\n", elapsed > 0 ? (float)count / elapsed : 0);
    printf("  First: node=%d cmd=0x%02X\n", first_node, last_cmd);

    close(fd);

    if (count == 0) {
        printf("\n[TC03] RESULT: FAIL (no commands received)\n");
        return 1;
    }
    printf("\n[TC03] RESULT: PASS (%d commands received)\n", count);
    return 0;
}