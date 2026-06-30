#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>
#include <signal.h>

static int running = 1;
static uint32_t frame_count = 0;
static uint32_t byte_count = 0;

void sig_handler(int sig) {
    printf("\n[STOP] frames=%u bytes=%u\n", frame_count, byte_count);
    running = 0;
}

/*
 * LoRa帧格式: [0xAA][0x55][LEN_H][LEN_L][DATA(N字节)][CRC8][0x55][0xAA]
 * 从接收字节流中按状态机提取完整帧并显示
 */
static void parse_and_display(unsigned char *buf, int *pos, unsigned char b)
{
    static unsigned char frame[1024];
    static int  state = 0;
    static int  idx = 0;
    static uint16_t data_len = 0;
    static uint8_t crc = 0;

    buf[(*pos)++] = b;
    if (*pos >= 8192) *pos = 0;

    switch (state) {
    case 0:
        if (b == 0xAA) state = 1;
        break;
    case 1:
        if (b == 0x55) { state = 2; frame[0] = 0xAA; frame[1] = 0x55; }
        else if (b != 0xAA) state = 0;
        break;
    case 2:
        data_len = (uint16_t)b << 8;
        state = 3;
        break;
    case 3:
        data_len |= b;
        if (data_len == 0 || data_len > 512) { state = 0; break; }
        frame[2] = (uint8_t)(data_len >> 8);
        frame[3] = (uint8_t)(data_len);
        idx = 0;
        crc = 0;
        state = 4;
        break;
    case 4:
        frame[4 + idx] = b;
        crc ^= b;
        idx++;
        if (idx >= data_len) state = 5;
        break;
    case 5:
        frame[4 + idx] = b;
        if (crc == b) state = 6;
        else state = 0;
        break;
    case 6:
        frame[4 + idx + 1] = b;
        if (b == 0x55) state = 7;
        else state = 0;
        break;
    case 7:
        state = 0;
        if (b == 0xAA) {
            uint16_t total = 4 + data_len + 3;
            frame[4 + idx + 2] = b;
            frame_count++;

            uint16_t L = ((uint16_t)frame[2] << 8) | frame[3];
            printf("\n======== [LoRa #%u] len=%d bytes ========\n", frame_count, total);
            printf("RAW: ");
            for (int i = 0; i < total && i < 64; i++)
                printf("%02X ", frame[i]);
            if (total > 64) printf("...(+%d)", total - 64);
            printf("\n");

            printf("DATA(%d): ", L);
            for (int i = 0; i < L && i < 80; i++)
                printf("%02X ", frame[4 + i]);
            if (L > 80) printf("...(+%d)", L - 80);
            printf("\n");

            byte_count += total;
            fflush(stdout);
        }
        break;
    }
}

int main()
{
    struct termios tio;
    unsigned char buf[8192];
    unsigned char byte;
    int pos = 0;
    int fd, n;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    setbuf(stdout, NULL);

    fd = open("/dev/ttyAMA3", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "open /dev/ttyAMA3: %s\n", strerror(errno));
        return 1;
    }

    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);

    printf("=== LoRa Receiver on /dev/ttyAMA3 (115200 8N1) ===\n");
    printf("Waiting for LoRa frames from terminal node...\n\n");

    while (running) {
        n = read(fd, &byte, 1);
        if (n == 1) {
            parse_and_display(buf, &pos, byte);
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                fprintf(stderr, "read error: %s\n", strerror(errno));
                break;
            }
            usleep(10000);
        }
    }

    close(fd);
    return 0;
}
