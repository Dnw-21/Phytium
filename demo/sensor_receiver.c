/*
 * Phytium Pi OpenAMP Sensor Data Receiver
 * Linux主核接收从核10组传感器模拟数据
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define RPMSG_ADDR_ANY 0xFFFFFFFF

#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

/* 与从核一致的协议结构 */
typedef struct {
    uint32_t command;
    uint16_t length;
    char data[496];
} ProtocolData;

/* 传感器数据包 */
typedef struct {
    uint32_t sensor_id;
    uint32_t timestamp;
    float voltage;
    float current;
    float temperature;
    uint8_t status;
} SensorPacket;

#define DEVICE_SENSOR_DATA   0x0010U
#define SENSOR_PACKET_COUNT  10
#define CHANNEL_NAME "rpmsg-openamp-demo-channel"

static int ctrl_fd = -1;
static int rpmsg_fd = -1;
static int running = 1;
static int total_packets_rx = 0;
static int total_batches_rx = 0;

void signal_handler(int sig) {
    printf("\n[INFO] Signal %d, stopping... (total batches: %d, packets: %d)\n",
           sig, total_batches_rx, total_packets_rx);
    running = 0;
}

/* 组装协议数据 */
static int assemble_request(ProtocolData *pkt, uint32_t command) {
    pkt->command = command;
    pkt->length = 0;
    return 6; /* command(4) + length(2) */
}

/* 解析协议数据 */
static int parse_sensor_data(const char *data, size_t len, SensorPacket *sensor) {
    if (len < 6 + sizeof(SensorPacket)) {
        return -1;
    }
    ProtocolData *pkt = (ProtocolData *)data;
    if (pkt->command != DEVICE_SENSOR_DATA) {
        return -2;
    }
    memcpy(sensor, pkt->data, sizeof(SensorPacket));
    return 0;
}

/* 打印传感器数据 */
static void print_sensor(const SensorPacket *s, int num) {
    const char *status_str[] = {"[NORMAL]", "[WARN  ]", "[ERROR ]"};
    printf("  [PKT %2d] ID=%2d ts=%5u V=%.2fV A=%.2fA T=%.1fC %s\n",
           num, s->sensor_id, s->timestamp,
           s->voltage, s->current, s->temperature,
           status_str[s->status > 2 ? 0 : s->status]);
}

int main(int argc, char *argv[]) {
    struct rpmsg_endpoint_info eptinfo;
    char rx_buf[512];
    int i, ret;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\n");
    printf("================================================\n");
    printf("  OpenAMP Sensor Data Receiver - Phytium Pi\n");
    printf("================================================\n\n");
    printf("[CONFIG] Channel: %s, Sensor packets: %d\n\n", CHANNEL_NAME, SENSOR_PACKET_COUNT);

    /* Step 1: 打开控制设备创建端点 */
    ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg_ctrl0: %s\n", strerror(errno));
        fprintf(stderr, "[HINT] 请确认:\n");
        fprintf(stderr, "  1. remoteproc已启动: echo start > /sys/class/remoteproc/remoteproc0/state\n");
        fprintf(stderr, "  2. 已绑定驱动\n");
        fprintf(stderr, "  3. 权限: sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0\n");
        return 1;
    }

    /* Step 2: 创建 RPMsg 端点 */
    memset(&eptinfo, 0, sizeof(eptinfo));
    strncpy(eptinfo.name, CHANNEL_NAME, sizeof(eptinfo.name) - 1);
    eptinfo.src = RPMSG_ADDR_ANY;
    eptinfo.dst = 0;

    printf("[INFO] Creating endpoint: name=%s, src=ANY, dst=0\n", eptinfo.name);
    if (ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo) < 0) {
        fprintf(stderr, "[ERROR] ioctl CREATE_EPT: %s\n", strerror(errno));
        close(ctrl_fd);
        return 1;
    }
    printf("[INFO] Endpoint created successfully!\n\n");

    /* Step 3: 打开数据通道 */
    rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg0: %s\n", strerror(errno));
        ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL);
        close(ctrl_fd);
        return 1;
    }

    usleep(500000);

    /* 主循环：请求传感器数据 → 接收处理10个数据包 → 打印标志位 → 重复 */
    while (running) {
        ProtocolData tx_pkt;
        int batch_packets = 0;
        SensorPacket sensors[SENSOR_PACKET_COUNT];

        printf("---------- Batch %d ----------\n", total_batches_rx + 1);

        /* 发送传感器数据请求 */
        int tx_len = assemble_request(&tx_pkt, DEVICE_SENSOR_DATA);
        ret = write(rpmsg_fd, &tx_pkt, tx_len);
        if (ret < 0) {
            fprintf(stderr, "[ERROR] write request: %s\n", strerror(errno));
            usleep(500000);
            continue;
        }
        printf("[SEND] Requested sensor data from slave\n");

        /* 接收10组传感器数据 */
        printf("[RECV] Waiting for %d sensor packets...\n", SENSOR_PACKET_COUNT);
        while (batch_packets < SENSOR_PACKET_COUNT && running) {
            ret = read(rpmsg_fd, rx_buf, sizeof(rx_buf) - 1);
            if (ret > 0) {
                SensorPacket sp;
                if (parse_sensor_data(rx_buf, ret, &sp) == 0) {
                    batch_packets++;
                    total_packets_rx++;
                    print_sensor(&sp, batch_packets);
                    sensors[batch_packets - 1] = sp;
                }
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "[ERROR] read: %s\n", strerror(errno));
                break;
            }
            usleep(1000);
        }

        /* 打印处理完成标志 */
        if (batch_packets == SENSOR_PACKET_COUNT) {
            total_batches_rx++;
            printf("\n");
            printf("  >> [COMPLETED] Batch %d: Received %d/%d sensor packets\n",
                   total_batches_rx, batch_packets, SENSOR_PACKET_COUNT);
            printf("  >> [STATS]    Total: %d batches, %d packets\n",
                   total_batches_rx, total_packets_rx);
        } else {
            printf("\n");
            printf("  >> [WARNING]  Batch incomplete: %d/%d packets received\n",
                   batch_packets, SENSOR_PACKET_COUNT);
        }

        /* 定时收发间隔 (模拟周期性传感器采集) */
        if (running && argc < 2) {
            printf("\n[INFO] Waiting 2s for next batch... (Ctrl+C to stop)\n\n");
            sleep(2);
        }
    }

    printf("\n[SUMMARY] Stopped. Total: %d batches, %d packets\n",
           total_batches_rx, total_packets_rx);

    /* 清理 */
    if (rpmsg_fd >= 0) close(rpmsg_fd);
    ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL);
    close(ctrl_fd);
    printf("[INFO] Endpoint destroyed, program exit.\n");
    return 0;
}
