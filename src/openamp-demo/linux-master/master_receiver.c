#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define DEVICE_MASTER_DATA  0x0020U
#define DEVICE_LORA_DATA    0x0023U

#define MAX_DATA_LENGTH     496

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    char data[MAX_DATA_LENGTH];
} ProtocolData;

static int rpmsg_fd = -1;
static int running = 1;
static int lora_count = 0;

void signal_handler(int sig)
{
    printf("\n[Stop] LoRa frames: %d\n", lora_count);
    running = 0;
}

int main(void)
{
    char rx_buf[600];
    int ret;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }
    usleep(100000);

    {
        ProtocolData ping = {.command = DEVICE_MASTER_DATA, .length = 0};
        write(rpmsg_fd, &ping, 6);
        usleep(200000);
    }

    while (running) {
        memset(rx_buf, 0, sizeof(rx_buf));
        ret = read(rpmsg_fd, rx_buf, sizeof(rx_buf) - 1);

        if (ret > 0) {
            ProtocolData *pkt = (ProtocolData *)rx_buf;
            if (ret < 6) continue;

            if (pkt->command == DEVICE_LORA_DATA) {
                lora_count++;
                printf("[#%d] len=%d  ", lora_count, pkt->length);
                for (int i = 0; i < pkt->length; i++)
                    printf("%02X ", (unsigned char)pkt->data[i]);
                printf("\n");
            }
        } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }

        usleep(10000);
    }

    if (rpmsg_fd >= 0) close(rpmsg_fd);
    return 0;
}
