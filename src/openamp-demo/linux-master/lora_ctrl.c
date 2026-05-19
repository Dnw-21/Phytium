#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

#define DEVICE_LORA_CTRL  0x0022U
#define LORA_SUBCMD_STOP  0x00
#define LORA_SUBCMD_START 0x01
#define LORA_SUBCMD_QUERY 0x02

#define MAX_DATA_LENGTH   496

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    uint8_t  data[MAX_DATA_LENGTH];
} ProtocolData;

static int send_ctrl(int fd, uint8_t subcmd)
{
    ProtocolData tx;
    memset(&tx, 0, sizeof(tx));
    tx.command = DEVICE_LORA_CTRL;
    tx.length  = 1;
    tx.data[0] = subcmd;

    ssize_t ret = write(fd, &tx, 6 + tx.length);
    if (ret < 0) {
        fprintf(stderr, "[ERROR] write: %s\n", strerror(errno));
        return -1;
    }

    printf("[SEND] LoRa CTRL subcmd=0x%02X (%s) -> %zd bytes\n",
           subcmd,
           subcmd == LORA_SUBCMD_START ? "START" :
           subcmd == LORA_SUBCMD_STOP  ? "STOP"  : "QUERY",
           ret);

    /* Wait for response */
    usleep(100000);

    ProtocolData rx;
    memset(&rx, 0, sizeof(rx));
    ssize_t rr = read(fd, &rx, sizeof(rx));
    if (rr >= 8 && rx.command == DEVICE_LORA_CTRL) {
        uint8_t state = rx.data[0];
        printf("[RESP] LoRa RX is now: %s\n",
               state ? "ENABLED" : "DISABLED");
        return state;
    }

    printf("[RESP] No confirmation received (%zd bytes, cmd=0x%04X)\n",
           rr, rr >= 6 ? rx.command : 0);
    return -1;
}

int main(int argc, char *argv[])
{
    const char *cmd = (argc > 1) ? argv[1] : "status";

    if (strcmp(cmd, "start") != 0 &&
        strcmp(cmd, "stop")  != 0 &&
        strcmp(cmd, "status") != 0 &&
        strcmp(cmd, "on")    != 0 &&
        strcmp(cmd, "off")   != 0 &&
        strcmp(cmd, "-h")    != 0 &&
        strcmp(cmd, "--help") != 0) {
        fprintf(stderr, "Usage: %s <start|stop|status>\n", argv[0]);
        fprintf(stderr, "  start/on   Enable LoRa RX\n");
        fprintf(stderr, "  stop/off   Disable LoRa RX\n");
        fprintf(stderr, "  status     Query LoRa RX state\n");
        return 1;
    }

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        printf("Usage: %s <start|stop|status>\n", argv[0]);
        printf("\n");
        printf("Control LoRa data reception on FreeRTOS CPU3.\n");
        printf("  start/on   Enable LoRa RX (UART3 polling resumes)\n");
        printf("  stop/off   Disable LoRa RX (UART3 polling paused)\n");
        printf("  status     Query current LoRa RX state\n");
        return 0;
    }

    int fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] Cannot open /dev/rpmsg0: %s\n", strerror(errno));
        fprintf(stderr, "[HINT]  Run: echo start > /sys/class/remoteproc/remoteproc0/state\n");
        return 1;
    }

    uint8_t subcmd;
    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "on") == 0)
        subcmd = LORA_SUBCMD_START;
    else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "off") == 0)
        subcmd = LORA_SUBCMD_STOP;
    else
        subcmd = LORA_SUBCMD_QUERY;

    int ret = send_ctrl(fd, subcmd);
    close(fd);
    return (ret >= 0) ? 0 : 1;
}
