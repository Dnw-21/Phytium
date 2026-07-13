#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

#define RPMSG_MAX_PAYLOAD  489

#define CMD_LORA_RAW       0x0023U
#define CMD_LORA_PARSED    0x0024U
#define CMD_NODE_STATUS    0x0025U
#define CMD_HEARTBEAT      0x0030U
#define CMD_ECHO_REQ       0x0040U
#define CMD_ECHO_RESP      0x0041U

#pragma pack(push, 1)
typedef struct {
    uint32_t command;
    uint16_t length;
    uint8_t  data[RPMSG_MAX_PAYLOAD];
} RpmsgPkt;
#pragma pack(pop)

#define RPMSG_PKT_HDR_SIZE  6

static volatile int g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

static int find_rpmsg_ctrl(char *path, size_t path_sz)
{
    const char *prefix = "rpmsg_ctrl";
    DIR *dir = opendir("/dev");
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, strlen(prefix)) != 0)
            continue;
        snprintf(path, path_sz, "/dev/%s", ent->d_name);
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return -1;
}

static int create_ept_from_ctrl(const char *ctrl_path, const char *service_name,
                                char *ept_path, size_t ept_path_sz)
{
    int ctrl_fd = open(ctrl_path, O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "ERROR: open %s: %s\n", ctrl_path, strerror(errno));
        return -1;
    }

    struct rpmsg_endpoint_info eptinfo;
    memset(&eptinfo, 0, sizeof(eptinfo));
    strncpy(eptinfo.name, service_name, sizeof(eptinfo.name) - 1);
    eptinfo.src = 0xFFFFFFFF;
    eptinfo.dst = 0;

    int ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);
    if (ret < 0) {
        fprintf(stderr, "ERROR: ioctl RPMSG_CREATE_EPT_IOCTL: %s\n", strerror(errno));
        close(ctrl_fd);
        return -1;
    }

    close(ctrl_fd);

    DIR *dir = opendir("/dev");
    if (!dir) return -1;

    struct dirent *ent;
    int found = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "rpmsg", 5) != 0) continue;
        if (strncmp(ent->d_name, "rpmsg_ctrl", 10) == 0) continue;
        snprintf(ept_path, ept_path_sz, "/dev/%s", ent->d_name);
        found = 1;
        break;
    }
    closedir(dir);

    if (!found) {
        fprintf(stderr, "ERROR: endpoint device not found after create\n");
        return -1;
    }

    return 0;
}

static const char *cmd_name(uint32_t cmd)
{
    switch (cmd) {
    case CMD_LORA_RAW:    return "LORA_RAW";
    case CMD_LORA_PARSED: return "LORA_PARSED";
    case CMD_HEARTBEAT:   return "HEARTBEAT";
    case CMD_ECHO_REQ:    return "ECHO_REQ";
    case CMD_ECHO_RESP:   return "ECHO_RESP";
    default:              return "UNKNOWN";
    }
}

static void print_lora_raw(const uint8_t *data, uint16_t len)
{
    printf("  [LoRa frame %uB] ", len);
    if (len >= 2 && data[0] == 0xAA && data[1] == 0x55) {
        uint16_t data_len = ((uint16_t)data[2] << 8) | data[3];
        uint8_t  frm_type = (len >= 9) ? data[8] : 0;
        printf("AA55 dlen=%u type=0x%02X", data_len, frm_type);
        if (len >= 13) {
            uint32_t sync = ((uint32_t)data[9] << 24) | ((uint32_t)data[10] << 16)
                          | ((uint32_t)data[11] << 8) | data[12];
            printf(" sync=0x%08X", sync);
        }
    }
    printf("\n");

    int show = (len < 48) ? len : 48;
    printf("  hex: ");
    for (int i = 0; i < show; i++)
        printf("%02X ", data[i]);
    if (len > show) printf("...");
    printf("\n");
}

int main(int argc, char *argv[])
{
    char ctrl_path[256] = {0};
    char ept_path[256] = {0};
    const char *service_name = "rpmsg-openamp-demo-channel";
    int fd = -1;
    unsigned long pkt_count = 0;
    unsigned long byte_total = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            service_name = argv[++i];
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    setbuf(stdout, NULL);

    printf("========================================\n");
    printf(" RPMsg Receiver — Phytium PE2204\n");
    printf(" Service: %s\n", service_name);
    printf("========================================\n\n");

    if (find_rpmsg_ctrl(ctrl_path, sizeof(ctrl_path)) < 0) {
        fprintf(stderr, "ERROR: No /dev/rpmsg_ctrl* found.\n");
        fprintf(stderr, "  Is remoteproc running? Check:\n");
        fprintf(stderr, "  cat /sys/class/remoteproc/remoteproc0/state\n");
        fprintf(stderr, "  ls /dev/rpmsg*\n");
        return 1;
    }
    printf("[1] Found ctrl: %s\n", ctrl_path);

    printf("[2] Creating endpoint '%s'...\n", service_name);
    if (create_ept_from_ctrl(ctrl_path, service_name, ept_path, sizeof(ept_path)) < 0) {
        fprintf(stderr, "ERROR: Failed to create endpoint\n");
        return 1;
    }
    printf("[2] Endpoint device: %s\n", ept_path);

    printf("[3] Opening %s\n", ept_path);
    fd = open(ept_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "ERROR: open %s: %s\n", ept_path, strerror(errno));
        return 1;
    }
    printf("[3] Device open OK\n\n");

    printf("[4] Sending ECHO_REQ to bind dest_addr...\n");
    {
        RpmsgPkt hello;
        memset(&hello, 0, sizeof(hello));
        hello.command = CMD_ECHO_REQ;
        hello.length  = 5;
        memcpy(hello.data, "HELLO", 5);
        ssize_t w = write(fd, &hello, RPMSG_PKT_HDR_SIZE + hello.length);
        if (w < 0) {
            fprintf(stderr, "WARNING: ECHO_REQ write failed: %s\n", strerror(errno));
        } else {
            printf("[4] ECHO_REQ sent (%zd bytes), waiting for response...\n", w);
        }
        usleep(200000);
        uint8_t tmp[512];
        ssize_t r = read(fd, tmp, sizeof(tmp));
        if (r > 0) {
            RpmsgPkt *resp = (RpmsgPkt *)tmp;
            printf("[4] ECHO_RESP received: cmd=0x%04X len=%u ✅\n",
                   resp->command, resp->length);
        } else {
            printf("[4] No ECHO_RESP (may arrive with first LoRa data)\n");
        }
    }
    printf("\nWaiting for LoRa data... (Ctrl+C to stop)\n\n");

    uint8_t rbuf[512];
    while (g_running) {
        ssize_t n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            fprintf(stderr, "read error: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            usleep(10000);
            continue;
        }

        if ((size_t)n < RPMSG_PKT_HDR_SIZE) {
            printf("[PKT] Too short: %zd bytes\n", n);
            continue;
        }

        RpmsgPkt pkt;
        memset(&pkt, 0, sizeof(pkt));
        size_t copy_len = (size_t)n > sizeof(pkt) ? sizeof(pkt) : (size_t)n;
        memcpy(&pkt, rbuf, copy_len);

        pkt_count++;
        byte_total += (unsigned long)n;

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm *tm_info = localtime(&ts.tv_sec);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

        printf("[%s PKT #%lu] cmd=0x%04X(%s) len=%u total=%luB\n",
               time_str, pkt_count, pkt.command, cmd_name(pkt.command),
               pkt.length, byte_total);

        switch (pkt.command) {
        case CMD_LORA_RAW:
            print_lora_raw(pkt.data, pkt.length);
            break;
        case CMD_ECHO_RESP:
            printf("  Echo response: %uB\n", pkt.length);
            break;
        case CMD_HEARTBEAT:
            printf("  Heartbeat\n");
            break;
        default:
            printf("  Data: %uB\n", pkt.length);
            break;
        }
    }

    printf("\n========================================\n");
    printf(" Done. Received %lu packets, %lu bytes\n", pkt_count, byte_total);
    printf("========================================\n");

    if (fd >= 0) close(fd);
    return 0;
}
