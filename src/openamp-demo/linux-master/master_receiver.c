/*
 * Phytium Pi OpenAMP Master Data Receiver
 * Linux主核：接收FreeRTOS从核的主控数据
 *
 * 协议通道:
 *   DEVICE_MASTER_DATA (0x0020): Linux侧接收LoRa帧 → RPMsg → FreeRTOS
 *   DEVICE_MASTER_CMD  (0x0021): FreeRTOS → RPMsg → Linux → LoRa模块
 *
 * 移植说明:
 *   原GD32通过USART1直接与LoRa模块通信。
 *   移植后架构:
 *     - LoRa模块连接到飞腾派UART (Linux侧或FreeRTOS侧)
 *     - Linux侧通过RPMsg与FreeRTOS交换主控数据
 *     - 当前LoRa模块未接，使用stub模式运行
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
#include <time.h>

#define RPMSG_ADDR_ANY 0xFFFFFFFF

#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

#define DEVICE_MASTER_DATA  0x0020U
#define DEVICE_MASTER_CMD   0x0021U

#define MAX_DATA_LENGTH     496
#define CHANNEL_NAME        "rpmsg-openamp-demo-channel"

typedef struct {
    uint32_t command;
    uint16_t length;
    char data[MAX_DATA_LENGTH];
} ProtocolData;

#define MASTER_CMD_REQ_WAVE       0x01
#define MASTER_CMD_REQ_FAULT_LIST 0x02
#define MASTER_CMD_CLEAR_FLASH    0x03
#define MASTER_CMD_WAVE_COLLECT   0x04

static int ctrl_fd = -1;
static int rpmsg_fd = -1;
static int running = 1;
static int total_data_rx = 0;
static int total_cmd_rx = 0;

void signal_handler(int sig)
{
    printf("\n[INFO] Signal %d, stopping... (data: %d, cmd: %d)\n",
           sig, total_data_rx, total_cmd_rx);
    running = 0;
}

/*
 * assemble_master_data: 组装LoRa数据帧发送给FreeRTOS
 *
 * 当Linux侧收到LoRa模块原始帧时，打包为 DEVICE_MASTER_DATA 消息
 * 通过RPMsg发送到FreeRTOS侧进行处理。
 *
 * 当前LoRa模块未接，此函数预留接口。
 */
__attribute__((unused))
static int assemble_master_data(ProtocolData *pkt, const uint8_t *lora_frame, uint16_t frame_len)
{
    if (frame_len > MAX_DATA_LENGTH) {
        fprintf(stderr, "[ERROR] LoRa frame too long: %u > %u\n",
                frame_len, (unsigned)MAX_DATA_LENGTH);
        return -1;
    }
    pkt->command = DEVICE_MASTER_DATA;
    pkt->length = frame_len;
    memcpy(pkt->data, lora_frame, frame_len);
    return 6 + frame_len;
}

static void print_master_cmd(const ProtocolData *pkt)
{
    if (pkt->length < 2) return;

    uint8_t node_id = (uint8_t)pkt->data[0];
    uint8_t cmd_code = (uint8_t)pkt->data[1];

    const char *cmd_name;
    switch (cmd_code) {
    case MASTER_CMD_REQ_WAVE:       cmd_name = "REQ_WAVE"; break;
    case MASTER_CMD_REQ_FAULT_LIST: cmd_name = "REQ_FAULT_LIST"; break;
    case MASTER_CMD_CLEAR_FLASH:    cmd_name = "CLEAR_FLASH"; break;
    case MASTER_CMD_WAVE_COLLECT:   cmd_name = "WAVE_COLLECT"; break;
    default:                        cmd_name = "UNKNOWN"; break;
    }

    printf("[CMD  ] node=%d cmd=%s(0x%02X) params=%d\n",
           node_id, cmd_name, cmd_code, pkt->length - 2);
}

int main(int argc, char *argv[])
{
    struct rpmsg_endpoint_info eptinfo;
    char rx_buf[600];
    int ret;
    int monitor_mode = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--monitor") == 0) {
            monitor_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("\n用法: %s [--monitor]\n\n", argv[0]);
            printf("  --monitor    仅监听FreeRTOS主控命令，不发送模拟数据\n");
            printf("  --help,-h    显示帮助\n\n");
            return 0;
        }
    }

    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  OpenAMP Master Data Receiver             ║\n");
    printf("║  Phytium Pi PE2204 - Linux Side           ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    printf("[CONFIG] Channel: %s\n", CHANNEL_NAME);
    printf("[CONFIG] DEVICE_MASTER_DATA: 0x%04X\n", DEVICE_MASTER_DATA);
    printf("[CONFIG] DEVICE_MASTER_CMD:  0x%04X\n", DEVICE_MASTER_CMD);
    printf("[CONFIG] Monitor only: %s\n\n", monitor_mode ? "YES" : "NO");

    ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (ctrl_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg_ctrl0: %s\n", strerror(errno));
        fprintf(stderr, "[HINT]  1. remoteproc: echo start > /sys/class/remoteproc/remoteproc0/state\n");
        fprintf(stderr, "[HINT]  2. driver bind\n");
        fprintf(stderr, "[HINT]  3. sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0\n");
        return 1;
    }

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

    rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg0: %s\n", strerror(errno));
        ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL);
        close(ctrl_fd);
        return 1;
    }

    usleep(500000);

    if (!monitor_mode) {
        printf("[INFO] Master data receiver running.\n");
        printf("[INFO]  - DEVICE_MASTER_DATA: Linux → FreeRTOS (LoRa frame forwarding)\n");
        printf("[INFO]  - DEVICE_MASTER_CMD:  FreeRTOS → Linux (master commands)\n");
        printf("[INFO] LoRa module not connected yet - stub mode.\n");
        printf("[INFO] Press Ctrl+C to stop.\n\n");
    }

    while (running) {
        ret = read(rpmsg_fd, rx_buf, sizeof(rx_buf) - 1);
        if (ret > 0) {
            ProtocolData *pkt = (ProtocolData *)rx_buf;

            if (ret < 6) {
                continue;
            }

            switch (pkt->command) {
            case DEVICE_MASTER_CMD:
                total_cmd_rx++;
                printf("[CMD  #%03d] ", total_cmd_rx);
                print_master_cmd(pkt);
                break;

            default:
                printf("[UNKNOWN] command=0x%04X length=%d\n",
                       pkt->command, pkt->length);
                break;
            }
        } else if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "[ERROR] read: %s\n", strerror(errno));
                break;
            }
        }

        usleep(10000);
    }

    printf("\n[SUMMARY] Stopped. Total: %d cmds received\n", total_cmd_rx);

    if (rpmsg_fd >= 0) close(rpmsg_fd);
    ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL);
    close(ctrl_fd);
    printf("[INFO] Endpoint destroyed, program exit.\n");
    return 0;
}