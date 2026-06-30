/*
 * start_sim_nodes.c — 多节点仿真启动器 (v3)
 * ==========================================
 * 顺序创建 RPMsg endpoint, 等所有通道就绪后统一发送 SPEED+START,
 * 再分叉子进程等待各节点 DONE。避免并发创建导致 39bus/9bus 绑定失败。
 *
 * 编译: aarch64-linux-gnu-gcc -O2 -Wall -static -o start_sim_nodes_arm64 start_sim_nodes.c
 * 用法: sudo ./start_sim_nodes [speed]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

#define CMD_SIM_CTRL    0x0051
#define CMD_SIM39_CTRL  0x0060
#define CMD_SIM9_CTRL   0x0070
#define CMD_SIM_DONE    0x0053

#define SIM_CTRL_START  1
#define SIM_CTRL_SPEED  3

#pragma pack(push, 1)
typedef struct {
    uint32_t command;
    uint16_t length;
    uint8_t  data[489];
} RpmsgPkt;

typedef struct {
    uint8_t  cmd;
    uint8_t  node_id;
    uint16_t data;
} SimCtrlPkt;

typedef struct {
    uint8_t  ack_cmd;
    uint8_t  status;
    uint16_t reserved;
} SimAckPkt;
#pragma pack(pop)

#define RPMSG_HDR_SIZE 6

static int list_rpmsg_idxs(int *idxs, int max)
{
    int n = 0;
    for (int i = 0; i < 30 && n < max; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/rpmsg%d", i);
        if (access(path, F_OK) == 0) idxs[n++] = i;
    }
    return n;
}

static int send_ctrl(int fd, uint32_t ctrl_cmd, uint8_t cmd, uint8_t node_id, uint16_t data)
{
    RpmsgPkt pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.command = ctrl_cmd;
    pkt.length = sizeof(SimCtrlPkt);
    SimCtrlPkt *c = (SimCtrlPkt *)pkt.data;
    c->cmd = cmd;
    c->node_id = node_id;
    c->data = data;
    return write(fd, &pkt, RPMSG_HDR_SIZE + (int)pkt.length);
}

static int idx_used(int idx, int *used, int n_used)
{
    for (int i = 0; i < n_used; i++) if (used[i] == idx) return 1;
    return 0;
}

static int find_new_device(int *before, int n_before, int *after, int n_after, int *used, int n_used)
{
    for (int i = 0; i < n_after; i++) {
        int found = 0;
        for (int j = 0; j < n_before; j++) {
            if (after[i] == before[j]) { found = 1; break; }
        }
        if (!found && !idx_used(after[i], used, n_used)) return after[i];
    }
    return -1;
}

typedef struct {
    const char *name;
    const char *channel;
    int src_addr;
    uint32_t ctrl_cmd;
    int fd;
} Node;

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    int speed = 1;
    if (argc > 1) speed = atoi(argv[1]);

    Node nodes[] = {
        {"5bus",  "rpmsg-sim-5bus",  0,  CMD_SIM_CTRL,   -1},
        {"39bus", "rpmsg-sim-39bus", 10, CMD_SIM39_CTRL, -1},
        {"9bus",  "rpmsg-sim-9bus",  20, CMD_SIM9_CTRL,  -1},
    };
    int n_nodes = sizeof(nodes) / sizeof(nodes[0]);

    printf("[master] Starting %d sim nodes sequentially (speed=%d)...\n", n_nodes, speed);

    int before[30], after[30];
    int used_idx[30];
    int n_used = 0;
    int n_before = list_rpmsg_idxs(before, 30);

    /* 1. 顺序创建所有 RPMsg endpoint 并打开 /dev/rpmsgX */
    for (int i = 0; i < n_nodes; i++) {
        printf("[%s] Creating endpoint (ch=%s, dst=%d)...\n", nodes[i].name, nodes[i].channel, nodes[i].src_addr);

        int cfd = open("/dev/rpmsg_ctrl0", O_RDWR);
        if (cfd < 0) { perror("open ctrl0"); return 1; }

        struct rpmsg_endpoint_info ep;
        memset(&ep, 0, sizeof(ep));
        strncpy(ep.name, nodes[i].channel, sizeof(ep.name) - 1);
        ep.src = RPMSG_ADDR_ANY;
        ep.dst = nodes[i].src_addr;

        int ret = ioctl(cfd, RPMSG_CREATE_EPT_IOCTL, &ep);
        if (ret < 0) ret = ioctl(cfd, RPMSG_CREATE_DEV_IOCTL, &ep);
        close(cfd);
        if (ret < 0) {
            printf("[%s] ioctl FAIL: %s\n", nodes[i].name, strerror(errno));
            return 1;
        }

        /* 等待 udev 创建 /dev/rpmsgX */
        int tries = 0;
        int dev_idx = -1;
        while (tries < 50 && dev_idx < 0) {
            usleep(100000); /* 100ms */
            int n_after = list_rpmsg_idxs(after, 30);
            dev_idx = find_new_device(before, n_before, after, n_after, used_idx, n_used);
            if (dev_idx >= 0) {
                n_before = n_after;
                used_idx[n_used++] = dev_idx;
                break;
            }
            tries++;
        }
        if (dev_idx < 0) {
            printf("[%s] WARN: No new rpmsg device found, skipping\n", nodes[i].name);
            nodes[i].fd = -1;
            continue;
        }

        char devpath[32];
        snprintf(devpath, sizeof(devpath), "/dev/rpmsg%d", dev_idx);
        nodes[i].fd = open(devpath, O_RDWR);
        if (nodes[i].fd < 0) {
            perror("open rpmsg");
            printf("[%s] WARN: Failed to open %s, skipping\n", nodes[i].name, devpath);
            continue;
        }
        printf("[%s] Opened %s\n", nodes[i].name, devpath);

        usleep(200000); /* 让 FreeRTOS 侧完成绑定 */
    }

    /* 2. 统一发送 SPEED + START (跳过未成功打开的节点) */
    for (int i = 0; i < n_nodes; i++) {
        if (nodes[i].fd < 0) continue;
        send_ctrl(nodes[i].fd, nodes[i].ctrl_cmd, SIM_CTRL_SPEED, (uint8_t)nodes[i].src_addr, (uint16_t)speed);
        usleep(50000);
        send_ctrl(nodes[i].fd, nodes[i].ctrl_cmd, SIM_CTRL_START, (uint8_t)nodes[i].src_addr, 0);
        printf("[%s] SPEED=%d START sent\n", nodes[i].name, speed);
    }

    /* 3. 分叉子进程等待 DONE (跳过未成功打开的节点) */
    pid_t pids[3];
    for (int i = 0; i < n_nodes; i++) {
        if (nodes[i].fd < 0) { pids[i] = -1; continue; }
        pid_t pid = fork();
        if (pid == 0) {
            uint8_t rxbuf[500];
            int done = 0;
            while (!done) {
                fd_set rfds;
                struct timeval tv = {5, 0};
                FD_ZERO(&rfds);
                FD_SET(nodes[i].fd, &rfds);
                int ret = select(nodes[i].fd + 1, &rfds, NULL, NULL, &tv);
                if (ret < 0) { if (errno == EINTR) continue; break; }
                if (ret == 0) {
                    printf("[%s] still running...\n", nodes[i].name);
                    continue;
                }
                int n = read(nodes[i].fd, rxbuf, sizeof(rxbuf));
                if (n <= 0) break;
                if (n >= (int)sizeof(SimAckPkt)) {
                    SimAckPkt *ack = (SimAckPkt *)rxbuf;
                    if (ack->ack_cmd == CMD_SIM_DONE) {
                        printf("[%s] DONE (status=%d)\n", nodes[i].name, ack->status);
                        done = 1;
                    }
                }
            }
            close(nodes[i].fd);
            exit(0);
        }
        pids[i] = pid;
    }

    for (int i = 0; i < n_nodes; i++) if (pids[i] > 0) waitpid(pids[i], NULL, 0);

    printf("[master] All nodes completed.\n");
    return 0;
}
