#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/select.h>
#include <termios.h>

static volatile int g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

static int uart_open(const char *dev, int baud)
{
    struct termios tty;
    speed_t sp;

    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror(dev); return -1; }

    switch (baud) {
    case 9600:   sp = B9600;   break;
    case 115200: sp = B115200; break;
    default:     sp = B115200; break;
    }

    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) < 0) { perror("tcgetattr"); close(fd); return -1; }

    cfsetospeed(&tty, sp);
    cfsetispeed(&tty, sp);

    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) < 0) { perror("tcsetattr"); close(fd); return -1; }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(int argc, char *argv[])
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ttyAMA3";
    int       baud = (argc > 2) ? atoi(argv[2]) : 115200;
    const char *mode = (argc > 3) ? argv[3] : "txrx";

    int fd, count = 0;
    unsigned long tx_bytes = 0, rx_bytes = 0;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    setbuf(stdout, NULL);

    printf("========================================\n");
    printf(" UART Test Tool - %s @ %d bps\n", dev, baud);
    printf(" Mode: %s\n", mode);
    printf(" Ctrl+C to stop\n");
    printf("========================================\n\n");

    fd = uart_open(dev, baud);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n", dev);
        return 1;
    }

    printf("[INFO] %s opened OK\n", dev);

    int do_tx = (strcmp(mode, "rx") != 0);
    int do_rx = (strcmp(mode, "tx") != 0);

    while (g_running) {
        fd_set fds;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100ms */

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        int r = select(fd + 1, do_rx ? &fds : NULL, NULL, NULL, &tv);

        if (r > 0 && FD_ISSET(fd, &fds)) {
            unsigned char rx[1024];
            int n = read(fd, rx, sizeof(rx));
            if (n > 0) {
                rx_bytes += n;
                printf("[RX %lu] %d bytes: ", rx_bytes, n);
                for (int i = 0; i < n && i < 64; i++) {
                    if (rx[i] >= 0x20 && rx[i] <= 0x7E)
                        putchar(rx[i]);
                    else
                        printf("<%02X>", rx[i]);
                }
                printf("\n");
            }
        }

        if (do_tx && (count % 10 == 0)) {
            char buf[128];
            int len = snprintf(buf, sizeof(buf),
                     "UART3_TEST:#%06d TX=%lu RX=%lu\r\n",
                     count, tx_bytes, rx_bytes);
            int n = write(fd, buf, len);
            if (n > 0) tx_bytes += n;
        }

        count++;
    }

    printf("\n========================================\n");
    printf(" Done. TX=%lu bytes, RX=%lu bytes\n", tx_bytes, rx_bytes);
    printf("========================================\n");

    close(fd);
    return 0;
}
