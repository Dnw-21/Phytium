#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#define DEVICE_MASTER_DATA    0x0020U
#define DEVICE_MASTER_CMD     0x0021U
#define DEVICE_MASTER_TEST    0x0030U
#define DEVICE_CORE_CHECK     0x0003U

#define TEST_PING             0x01
#define TEST_SINGLE_FAULT     0x02
#define TEST_CONTINUOUS       0x03
#define TEST_STOP             0x04
#define TEST_FLASH_CHECK      0x05
#define TEST_CHAOS_ENCRYPT    0x06
#define TEST_STATUS           0x07

#define TEST_RESP_OK          0x00
#define TEST_RESP_PONG        0x01
#define TEST_RESP_FAULT_SENT  0x02
#define TEST_RESP_RUNNING     0x03
#define TEST_RESP_STOPPED     0x04
#define TEST_RESP_FLASH_OK    0x05
#define TEST_RESP_ENCRYPT_OK  0x06
#define TEST_RESP_ERROR       0xFF

#define MAX_DATA_LENGTH       496

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    uint8_t  data[MAX_DATA_LENGTH];
} ProtocolData;

typedef struct __attribute__((packed)) {
    uint8_t  subcmd;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint8_t  severity;
    uint16_t sample_count;
    uint8_t  reserved[2];
} TestCtrlPacket_t;

typedef struct __attribute__((packed)) {
    uint8_t  resp_code;
    uint8_t  subcmd_echo;
    uint8_t  node_id;
    uint8_t  fault_type;
    uint32_t processed_count;
    uint32_t timestamp_ms;
} TestRespPacket_t;

static const char *fault_names[] = {
    [0] = "NONE",
    [1] = "OVER_VOLTAGE",
    [2] = "UNDER_VOLTAGE",
    [3] = "VOLTAGE_SAG",
    [4] = "VOLTAGE_SWELL",
    [5] = "TRANSIENT"
};

static const char *severity_names[] = {
    [0] = "NORMAL",
    [1] = "WARNING",
    [2] = "DANGER"
};

static int rpmsg_fd = -1;
static int running = 1;

static void signal_handler(int sig __attribute__((unused)))
{
    printf("\n退出...\n");
    running = 0;
}

static void print_banner(void)
{
    printf("\n"
           "╔══════════════════════════════════════════════════════╗\n"
           "║         测试控制面板 - Test Control Panel            ║\n"
           "║         Phytium Pi PE2204 OpenAMP Test Suite         ║\n"
           "╚══════════════════════════════════════════════════════╝\n\n");
}

static int send_test_cmd(int fd, uint8_t subcmd, uint8_t node_id,
                         uint8_t fault_type, uint8_t severity,
                         TestRespPacket_t *resp)
{
    ProtocolData tx;
    TestCtrlPacket_t ctrl;

    memset(&tx, 0, sizeof(tx));
    memset(&ctrl, 0, sizeof(ctrl));

    tx.command = DEVICE_MASTER_TEST;
    tx.length  = sizeof(ctrl);

    ctrl.subcmd    = subcmd;
    ctrl.node_id   = node_id;
    ctrl.fault_type = fault_type;
    ctrl.severity  = severity;
    ctrl.sample_count = 80;

    memcpy(tx.data, &ctrl, sizeof(ctrl));

    ssize_t wrote = write(fd, &tx, 6 + tx.length);
    if (wrote < 0) {
        fprintf(stderr, "[ERROR] write: %s\n", strerror(errno));
        return -1;
    }

    printf("    >> 发送: subcmd=0x%02X node=%d fault=%s sev=%s\n",
           subcmd, node_id, fault_names[fault_type], severity_names[severity]);

    /* Wait for response */
    for (int i = 0; i < 100; i++) {
        ProtocolData rx;
        usleep(10000);
        ssize_t r = read(fd, &rx, sizeof(rx));
        if (r < 6) continue;

        TestRespPacket_t *rsp = (TestRespPacket_t *)rx.data;
        if (rx.command == DEVICE_MASTER_CMD &&
            rsp->subcmd_echo == subcmd) {

            printf("    << 响应: code=0x%02X node=%d fault=%s count=%u t=%ums\n",
                   rsp->resp_code, rsp->node_id,
                   fault_names[rsp->fault_type],
                   rsp->processed_count, rsp->timestamp_ms);
            if (resp) memcpy(resp, rsp, sizeof(*resp));
            return 0;
        }
    }

    printf("    << 超时: 无响应\n");
    return -2;
}

static void menu_main(void)
{
    printf(
        "┌──────────────────────────────────────────────────────┐\n"
        "│  1. PING测试         验证RPMsg链路往返                │\n"
        "│  2. 单次故障注入      注入指定节点/类型故障帧          │\n"
        "│  3. 连续故障生成      持续向Linux发送仿真故障数据      │\n"
        "│  4. 停止测试          停止连续模式                     │\n"
        "│  5. Flash状态查询     查询节点Flash保存统计            │\n"
        "│  6. 混沌加密验证      测试encrypt/decrypt往返一致性    │\n"
        "│  7. 连续收包监控      观察FreeRTOS侧生成的所有命令      │\n"
        "│                                                      │\n"
        "│  a. 自动化测试套件    运行全部自动化测试               │\n"
        "│                                                      │\n"
        "│  q. 退出                                              │\n"
        "└──────────────────────────────────────────────────────┘\n"
        "选择: "
    );
}

static void do_ping(int fd)
{
    printf("\n─── PING测试 ───\n");
    for (int i = 1; i <= 5; i++) {
        TestRespPacket_t resp;
        int ret = send_test_cmd(fd, TEST_PING, 0, 0, 0, &resp);
        if (ret == 0 && resp.resp_code == TEST_RESP_PONG) {
            printf("    [%d/5] PING OK rtt=%ums\n", i, resp.timestamp_ms);
        }
        usleep(200000);
    }
}

static void do_single_fault(int fd)
{
    int node, type, sev;

    printf("\n─── 单次故障注入 ───\n");
    printf("节点 (0-2): "); fflush(stdout);
    scanf("%d", &node); while (getchar() != '\n');

    printf("故障类型 (1=过压 2=欠压 3=骤降 4=骤升 5=瞬态): ");
    fflush(stdout);
    scanf("%d", &type); while (getchar() != '\n');

    printf("严重程度 (0=正常 1=警告 2=危险): "); fflush(stdout);
    scanf("%d", &sev); while (getchar() != '\n');

    if (node < 0 || node > 2) node = 0;
    if (type < 1 || type > 5) type = 1;
    if (sev < 0 || sev > 2) sev = 2;

    printf("\n注入: node%d fault=%s severity=%s\n",
           node, fault_names[type], severity_names[sev]);

    TestRespPacket_t resp;
    send_test_cmd(fd, TEST_SINGLE_FAULT, (uint8_t)node,
                  (uint8_t)type, (uint8_t)sev, &resp);
}

static void do_continuous(int fd)
{
    int node, type, sev;

    printf("\n─── 连续故障生成 ───\n");
    printf("节点 (0-2): "); fflush(stdout);
    scanf("%d", &node); while (getchar() != '\n');

    printf("故障类型 (1=过压 2=欠压 3=骤降 4=骤升 5=瞬态): ");
    fflush(stdout);
    scanf("%d", &type); while (getchar() != '\n');

    printf("严重程度 (0=正常 1=警告 2=危险): "); fflush(stdout);
    scanf("%d", &sev); while (getchar() != '\n');

    if (node < 0 || node > 2) node = 0;
    if (type < 1 || type > 5) type = 1;
    if (sev < 0 || sev > 2) sev = 2;

    printf("\n启动: node%d fault=%s sev=%s (按Enter停止)\n",
           node, fault_names[type], severity_names[sev]);

    TestRespPacket_t resp;
    send_test_cmd(fd, TEST_CONTINUOUS, (uint8_t)node,
                  (uint8_t)type, (uint8_t)sev, &resp);

    int count = 0;
    while (1) {
        ProtocolData rx;
        ssize_t r = read(fd, &rx, sizeof(rx));
        if (r >= 6 && rx.command == DEVICE_MASTER_CMD) {
            printf("\r接收命令 #%d: node=%d cmd=0x%02X params=%d bytes  ",
                   ++count, rx.data[0], rx.data[1], rx.length - 2);
            fflush(stdout);
        }
        usleep(100000);

        /* Check for key press (Enter to stop) */
        fd_set fds;
        struct timeval tv = {0, 0};
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            getchar();
            break;
        }
    }

    printf("\n\n停止... ");
    send_test_cmd(fd, TEST_STOP, 0, 0, 0, NULL);
    printf("共接收 %d 条命令\n", count);
}

static void do_monitor(int fd)
{
    int count = 0;
    time_t start = time(NULL);

    printf("\n─── 连续收包监控 (按Enter退出) ───\n");
    printf("  cmd=0x%04X\n\n", DEVICE_MASTER_CMD);

    while (1) {
        ProtocolData rx;
        ssize_t r = read(fd, &rx, sizeof(rx));
        if (r >= 6 && rx.command == DEVICE_MASTER_CMD) {
            count++;
            if (count <= 10 || count % 20 == 0) {
                printf("  [%3d] node=%d cmd=0x%02X params=%d bytes\n",
                       count, rx.data[0], rx.data[1],
                       rx.length > 2 ? rx.length - 2 : 0);
            }
        }
        usleep(50000);

        fd_set fds;
        struct timeval tv = {0, 0};
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            getchar();
            break;
        }
    }

    time_t elapsed = time(NULL) - start;
    printf("\n监控结束: %d 条命令 / %ld 秒 = %.1f cmd/s\n",
           count, elapsed, elapsed > 0 ? (float)count / elapsed : 0);
}

static void do_flash_check(int fd)
{
    printf("\n─── Flash状态查询 ───\n");
    for (int node = 0; node < 3; node++) {
        TestRespPacket_t resp;
        int ret = send_test_cmd(fd, TEST_FLASH_CHECK, (uint8_t)node,
                                0, 0, &resp);
        if (ret == 0 && resp.resp_code == TEST_RESP_FLASH_OK) {
            printf("  Node%d: downloads=%u\n", node, resp.processed_count);
        } else {
            printf("  Node%d: 查询失败\n", node);
        }
    }
}

static void do_encrypt_test(int fd)
{
    printf("\n─── 混沌加密验证 ───\n");
    TestRespPacket_t resp;
    int ret = send_test_cmd(fd, TEST_CHAOS_ENCRYPT, 0, 0, 0, &resp);
    if (ret == 0 && resp.resp_code == TEST_RESP_ENCRYPT_OK) {
        printf("  结果: PASS  encrypt/decrypt往返一致\n");
    } else {
        printf("  结果: FAIL\n");
    }
}

int main(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }

    /* Startup handshake */
    {
        ProtocolData handshake = {
            .command = DEVICE_MASTER_DATA,
            .length  = 0
        };
        write(rpmsg_fd, &handshake, 6);
        usleep(200000);
    }

    print_banner();

    while (running) {
        menu_main();

        char choice = getchar();
        while (getchar() != '\n'); /* consume rest of line */

        switch (choice) {
        case '1': do_ping(rpmsg_fd);           break;
        case '2': do_single_fault(rpmsg_fd);    break;
        case '3': do_continuous(rpmsg_fd);      break;
        case '4': send_test_cmd(rpmsg_fd, TEST_STOP, 0, 0, 0, NULL); break;
        case '5': do_flash_check(rpmsg_fd);     break;
        case '6': do_encrypt_test(rpmsg_fd);    break;
        case '7': do_monitor(rpmsg_fd);         break;
        case 'a':
            system("clear");
            system("/home/user/demo/run_tests.sh --all 2>&1");
            break;
        case 'q':
        case 'Q':
            running = 0;
            break;
        default:
            printf("无效选项\n");
            break;
        }
    }

    close(rpmsg_fd);
    printf("再见.\n");
    return 0;
}