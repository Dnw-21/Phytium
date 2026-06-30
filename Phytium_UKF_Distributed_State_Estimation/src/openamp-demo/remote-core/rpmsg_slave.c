#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openamp/open_amp.h>
#include <metal/alloc.h>
#include <metal/io.h>
#include <metal/device.h>

#define RPMSG_ADDR_ANY 0xFFFFFFFF
#define SHARED_MEM_SIZE (1024 * 1024) 
#define ENDPOINT_NAME "rpmsg-openamp-demo-channel"

static struct rpmsg_endpoint lept;
static struct rpmsg_device *rdev;
static int ept_recv_cb(struct rpmsg_endpoint *ept, void *data,
                       size_t len, uint32_t src, void *priv);
static int ept_destroy_cb(struct rpmsg_endpoint *ept);

static int app_running = 1;

void signal_handler(int sig) {
    printf("\n[SLAVE] 接收到信号 %d，正在停止...\n", sig);
    app_running = 0;
}

int ept_recv_cb(struct rpmsg_endpoint *ept, void *data,
                size_t len, uint32_t src, void *priv) {
    char *recv_msg = (char *)data;
    
    printf("[SLAVE RECV] %s\n", recv_msg);
    
    char response[256];
    snprintf(response, sizeof(response), "[SLAVE ACK] 收到: %.50s", recv_msg);
    
    if (rpmsg_send(ept, response, strlen(response) + 1) > 0) {
        printf("[SLAVE SEND] 已回复确认消息\n");
    }
    
    return RPMSG_SUCCESS;
}

int ept_destroy_cb(struct rpmsg_endpoint *ept) {
    printf("[SLAVE] 端点被销毁\n");
    return RPMSG_SUCCESS;
}

int main(int argc, char *argv[]) {
    int ret;
    struct metal_init_params params = METAL_INIT_DEFAULTS;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   OpenAMP Slave Demo - 飞腾派 CEK8903     ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");
    
    printf("[SLAVE] 初始化libmetal...\n");
    ret = metal_init(&params);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] libmetal初始化失败: %d\n", ret);
        return -1;
    }
    
    printf("[SLAVE] 初始化OpenAMP...\n");
    ret = platform_init(argc, argv, &platform);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] 平台初始化失败: %d\n", ret);
        goto cleanup_metal;
    }
    
    printf("[SLAVE] 创建RPMsg设备...\n");
    rdev = rpmsg_virtio_create_vdev(platform.vdev, 0, 
                                     VIRTIO_DEV_DEVICE, NULL);
    if (!rdev) {
        fprintf(stderr, "[ERROR] 创建RPMsg设备失败\n");
        goto cleanup_platform;
    }
    
    printf("[SLAVE] 等待主机就绪...\n");
    rpmsg_virtio_wait_remote_ready(rdev);
    
    printf("[SLAVE] 创建端点: %s\n", ENDPOINT_NAME);
    ret = rpmsg_create_ept(&lept, rdev, ENDPOINT_NAME,
                           RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                           ept_recv_cb, ept_destroy_cb);
    if (ret != 0) {
        fprintf(stderr,"[ERROR] 创建端点失败: %d\n", ret);
        goto cleanup_rdev;
    }
    
    printf("[SLAVE] ✅ 端点创建成功！等待主机消息...\n\n");
    
    while (app_running) {
        usleep(100000); 
    }
    
    printf("\n[SLAVE] 正在清理资源...\n");
    
cleanup:
    rpmsg_destroy_ept(&lept);
cleanup_rdev:
    rpmsg_virtio_shutdown_vdev(rdev);
cleanup_platform:
    platform_cleanup();
cleanup_metal:
    metal_finish();
    
    printf("[SLAVE] 程序退出\n");
    return 0;
}
