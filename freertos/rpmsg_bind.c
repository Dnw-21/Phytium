/*
 * rpmsg_bind.c — 创建 /dev/rpmsg0 并发送首条消息绑定 dest_addr
 * 参考 DEBUG_GUIDE.md S2 验证结果: eptinfo.dst = 0
 *
 * 编译: gcc -o rpmsg_bind rpmsg_bind.c
 * 用法: sudo ./rpmsg_bind
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>
#include <stdint.h>
#include <errno.h>

/* 匹配 FreeRTOS 端 RpmsgPkt 格式 */
#pragma pack(push, 1)
typedef struct {
    uint32_t command;
    uint16_t length;
    uint8_t  data[489];
} RpmsgPkt;
#pragma pack(pop)

#define RPMSG_PKT_HDR_SIZE  6
#define CMD_ECHO_REQ        0x0040

int main(void)
{
    int ctrl_fd, ept_fd;
    struct rpmsg_endpoint_info eptinfo;
    int ret;

    memset(&eptinfo, 0, sizeof(eptinfo));
    strncpy(eptinfo.name, "rpmsg-openamp-demo-channel", sizeof(eptinfo.name) - 1);
    eptinfo.src = RPMSG_ADDR_ANY;   /* 让内核分配 */
    eptinfo.dst = 0;                /* FreeRTOS endpoint src=0 */

    /* 1. 打开 rpmsg_ctrl 设备 */
    ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);
    if (ctrl_fd < 0) {
        perror("open /dev/rpmsg_ctrl0");
        return 1;
    }
    printf("Opened /dev/rpmsg_ctrl0\n");

    /* 2. 创建 endpoint → 生成 /dev/rpmsgX */
    ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);
    if (ret < 0) {
        printf("ioctl RPMSG_CREATE_EPT_IOCTL failed: %s (errno=%d)\n", strerror(errno), errno);
        /* 尝试 RPMSG_CREATE_DEV_IOCTL 作为备选 */
        ret = ioctl(ctrl_fd, RPMSG_CREATE_DEV_IOCTL, &eptinfo);
        if (ret < 0) {
            printf("ioctl RPMSG_CREATE_DEV_IOCTL also failed: %s (errno=%d)\n", strerror(errno), errno);
            close(ctrl_fd);
            return 1;
        }
        printf("Created dev (RPMSG_CREATE_DEV_IOCTL), ret=%d\n", ret);
    } else {
        printf("Created endpoint (RPMSG_CREATE_EPT_IOCTL), ret=%d\n", ret);
    }
    close(ctrl_fd);

    /* 3. 查找并打开 rpmsg 设备 */
    char devpath[64];
    for (int i = 0; i < 10; i++) {
        snprintf(devpath, sizeof(devpath), "/dev/rpmsg%d", i);
        ept_fd = open(devpath, O_RDWR);
        if (ept_fd >= 0) {
            printf("Opened %s\n", devpath);
            break;
        }
    }
    if (ept_fd < 0) {
        printf("No /dev/rpmsgX device found!\n");
        return 1;
    }

    /* 4. 发送 CMD_ECHO_REQ 绑定 dest_addr */
    RpmsgPkt pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.command = CMD_ECHO_REQ;
    pkt.length = 5;
    memcpy(pkt.data, "HELLO", 5);

    ret = write(ept_fd, &pkt, RPMSG_PKT_HDR_SIZE + pkt.length);
    if (ret < 0) {
        printf("write ECHO_REQ failed: %s (errno=%d)\n", strerror(errno), errno);
        /* 即使write失败，endpoint可能已经绑定了dest_addr */
        /* FreeRTOS侧可能已经收到了NS消息 */
    } else {
        printf("Sent CMD_ECHO_REQ (%d bytes)\n", ret);
    }

    /* 5. 读取回复 (非阻塞, 等待2秒) */
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(ept_fd, &rfds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    ret = select(ept_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret > 0) {
        RpmsgPkt rx;
        memset(&rx, 0, sizeof(rx));
        ret = read(ept_fd, &rx, sizeof(rx));
        if (ret > 0) {
            printf("Received reply: cmd=0x%04X len=%u data=%.*s\n",
                   rx.command, rx.length, rx.length, rx.data);
        }
    } else {
        printf("No reply within 2s (select ret=%d)\n", ret);
    }

    /* 保持设备打开 */
    printf("\n%s is active. Press Ctrl+C to exit.\n", devpath);

    while (1) {
        sleep(10);
    }

    close(ept_fd);
    return 0;
}
