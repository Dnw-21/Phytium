#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>

#define RPMSG_ADDR_ANY 0xFFFFFFFF

#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

static int fd = -1;
static int running = 1;

void signal_handler(int sig) {
    printf("\n[MASTER] 接收到信号 %d，正在停止...\n", sig);
    running = 0;
}

int rpmsg_init(const char* ept_name, uint32_t dst_addr) {
    struct rpmsg_endpoint_info eptinfo;
    char dev_name[64];
    
    snprintf(dev_name, sizeof(dev_name), "/dev/rpmsg%d", dst_addr);
    
    printf("[MASTER] 打开设备: %s\n", dev_name);
    fd = open(dev_name, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] 无法打开 %s: %s\n", dev_name, strerror(errno));
        return -1;
    }
    
    memset(&eptinfo, 0, sizeof(eptinfo));
    strncpy(eptinfo.name, ept_name, sizeof(eptinfo.name) - 1);
    eptinfo.src = RPMSG_ADDR_ANY;
    eptinfo.dst = dst_addr;
    
    printf("[MASTER] 创建端点: name=%s, src=ANY, dst=%u\n", 
           eptinfo.name, eptinfo.dst);
    
    if (ioctl(fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo) < 0) {
        fprintf(stderr, "[ERROR] 创建端点失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    printf("[MASTER] ✅ 端点创建成功！\n");
    return 0;
}

int rpmsg_send_msg(const void* data, int len) {
    int ret;
    
    ret = write(fd, data, len);
    if (ret < 0) {
        fprintf(stderr, "[ERROR] 发送消息失败: %s\n", strerror(errno));
        return -1;
    }
    
    printf("[MASTER] 发送消息: %d 字节\n", ret);
    return ret;
}

int rpmsg_recv_msg(void* buf, int maxlen) {
    int ret;
    
    ret = read(fd, buf, maxlen);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; 
        }
        fprintf(stderr, "[ERROR] 接收消息失败: %s\n", strerror(errno));
        return -1;
    }
    
    return ret;
}

void rpmsg_cleanup(void) {
    if (fd >= 0) {
        ioctl(fd, RPMSG_DESTROY_EPT_IOCTL);
        close(fd);
        fd = -1;
        printf("[MASTER] 端点已销毁\n");
    }
}

void print_usage(const char* prog) {
    printf("\n用法: %s [选项]\n\n", prog);
    printf("选项:\n");
    printf("  --channel NAME   通道名称 (默认: rpmsg-openamp-demo-channel)\n");
    printf("  --dst ADDR       目标地址 (默认: 0)\n");
    printf("  --count N        发送次数 (默认: 100)\n");
    printf("  --interval MS    发送间隔毫秒 (默认: 100)\n");
    printf("  --help,-h        显示帮助信息\n\n");
    printf("示例:\n");
    printf("  %s                          # 使用默认参数运行\n", prog);
    printf("  %s --count 50               # 发送50次\n", prog);
    printf("  %s --channel my-channel     # 使用自定义通道名\n", prog);
}

int main(int argc, char* argv[]) {
    char channel_name[64] = "rpmsg-openamp-demo-channel";
    uint32_t dst_addr = 0;
    int msg_count = 100;
    int interval_ms = 100;
    char tx_buf[512];
    char rx_buf[512];
    int i;
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--channel") == 0 && i + 1 < argc) {
            strncpy(channel_name, argv[++i], sizeof(channel_name) - 1);
        } else if (strcmp(argv[i], "--dst") == 0 && i + 1 < argc) {
            dst_addr = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            msg_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   OpenAMP Master Demo - 飞腾派 CEK8903    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    printf("[CONFIG] 通道名称: %s\n", channel_name);
    printf("[CONFIG] 目标地址: %u\n", dst_addr);
    printf("[CONFIG] 发送次数: %d\n", msg_count);
    printf("[CONFIG] 发送间隔: %d ms\n\n", interval_ms);
    
    if (rpmsg_init(channel_name, dst_addr) != 0) {
        fprintf(stderr, "\n[ERROR] 初始化失败！请确认：\n");
        fprintf(stderr, "  1. 远程处理器已启动: echo start > /sys/class/remoteproc/remoteproc0/state\n");
        fprintf(stderr, "  2. 已绑定驱动: echo rpmsg_chrdev > /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override\n");
        fprintf(stderr, "  3. /dev/rpmsg0 设备存在\n\n");
        return 1;
    }
    
    usleep(500000); 
    
    printf("[MASTER] 开始通信测试...\n\n");
    
    for (i = 1; i <= msg_count && running; i++) {
        int rx_len;
        
        snprintf(tx_buf, sizeof(tx_buf), "Hello World! No:%d", i);
        
        printf("[SEND #%03d] %s\n", i, tx_buf);
        
        if (rpmsg_send_msg(tx_buf, strlen(tx_buf) + 1) < 0) {
            break;
        }
        
        usleep(interval_ms * 1000);
        
        rx_len = rpmsg_recv_msg(rx_buf, sizeof(rx_buf) - 1);
        if (rx_len > 0) {
            rx_buf[rx_len] = '\0';
            printf("[RECV      ] %s\n", rx_buf);
        }
    }
    
    if (!running) {
        printf("\n[MASTER] 用户中断，正在停止...\n");
    } else {
        printf("\n[MASTER] ✅ 测试完成！共发送/接收 %d 条消息\n", i - 1);
    }
    
    rpmsg_cleanup();
    
    printf("\n[INFO] 程序退出\n");
    return 0;
}
