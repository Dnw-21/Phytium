/*
 * lora_receiver.c — Linux侧 ATK-MWCC68D LoRa 接收程序 (v3 - 修复版)
 *
 * Fixes:
 *   v3.1: UART端口改为/dev/ttyAMA2 (J1 Pin8=TXD, Pin10=RXD)
 *   v3.2: 添加FIOPAD IOMUX配置 (GPIO3_1=MD0, GPIO2_10=AUX)
 *   v3.3: 修正AT命令为GD32 BSP验证格式
 *   v3.4: mmap优化 + 变量初始化修复
 *
 * 编译:
 *   gcc -Wall -O2 -o lora_receiver lora_receiver.c
 * 运行:
 *   sudo ./lora_receiver
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>

/* ==========================================================================
 *  PE2204 寄存器定义
 * ========================================================================== */
#define GPIO_DR_OFFSET   0x00
#define GPIO_DDR_OFFSET  0x04
#define GPIO_EXT_OFFSET  0x08

#define GPIO2_BASE       0x28036000U
#define GPIO3_BASE       0x28037000U
#define FIOPAD_BASE      0x32B30000U

#define FIOPAD_A37_REG0_OFF  0x00C4U  /* GPIO2_10 (AUX) */
#define FIOPAD_C49_REG0_OFF  0x00E0U  /* GPIO3_1 (MD0)  */
#define FIOPAD_FUNC6         6

#define AUX_PIN          10       /* GPIO2_10 */
#define MD0_PIN          1        /* GPIO3_1  */

/* ==========================================================================
 *  LoRa 模块参数 (匹配 GD32 v3 mwcc68_cfg.h)
 * ========================================================================== */
#define LORA_ADDR        0x000B
#define LORA_CHN         23
#define LORA_NETID       0
#define LORA_RATE        5        /* 19.2kbps */
#define LORA_TMODE       1        /* 定点传输 */
#define LORA_PACKSIZE    3        /* 240字节 */
#define LORA_BPS         7        /* 115200 */
#define LORA_POWER       4        /* 20dBm */

/* ==========================================================================
 *  GPIO + FIOPAD 硬件操作
 * ========================================================================== */
static int mem_fd = -1;

static volatile uint32_t *gpio2_map = NULL;
static volatile uint32_t *gpio3_map = NULL;
static volatile uint32_t *fiopad_map = NULL;

static void hw_init(void)
{
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("/dev/mem"); exit(1); }

    gpio2_map  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, GPIO2_BASE & ~0xFFFUL);
    gpio3_map  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, GPIO3_BASE & ~0xFFFUL);
    fiopad_map = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, FIOPAD_BASE & ~0xFFFUL);

    if (gpio2_map == MAP_FAILED || gpio3_map == MAP_FAILED || fiopad_map == MAP_FAILED) {
        perror("mmap"); exit(1);
    }
}

static void hw_deinit(void)
{
    if (gpio2_map  && gpio2_map  != MAP_FAILED) munmap((void *)gpio2_map,  0x1000);
    if (gpio3_map  && gpio3_map  != MAP_FAILED) munmap((void *)gpio3_map,  0x1000);
    if (fiopad_map && fiopad_map != MAP_FAILED) munmap((void *)fiopad_map,  0x1000);
    if (mem_fd >= 0) close(mem_fd);
}

static uint32_t gpio_read_reg(volatile uint32_t *base, uint32_t off)
{
    return *(volatile uint32_t *)((uintptr_t)base + off);
}

static void gpio_write_reg(volatile uint32_t *base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)base + off) = val;
}

static void fiopad_set_gpio(uint32_t reg_off)
{
    uint32_t val = gpio_read_reg(fiopad_map, reg_off);
    val &= ~0x07U;
    val |= FIOPAD_FUNC6;
    gpio_write_reg(fiopad_map, reg_off, val);
}

static void gpio_set_output(volatile uint32_t *base, int pin)
{
    uint32_t ddr = gpio_read_reg(base, GPIO_DDR_OFFSET);
    ddr |= (1U << pin);
    gpio_write_reg(base, GPIO_DDR_OFFSET, ddr);
}

static void gpio_set_input(volatile uint32_t *base, int pin)
{
    uint32_t ddr = gpio_read_reg(base, GPIO_DDR_OFFSET);
    ddr &= ~(1U << pin);
    gpio_write_reg(base, GPIO_DDR_OFFSET, ddr);
}

static void gpio_write(volatile uint32_t *base, int pin, int high)
{
    uint32_t dr = gpio_read_reg(base, GPIO_DR_OFFSET);
    if (high) dr |= (1U << pin);
    else      dr &= ~(1U << pin);
    gpio_write_reg(base, GPIO_DR_OFFSET, dr);
}

static int gpio_read(volatile uint32_t *base, int pin)
{
    return (gpio_read_reg(base, GPIO_EXT_OFFSET) >> pin) & 1;
}

static void md0_at_mode(void)    { gpio_set_output(gpio3_map, MD0_PIN); gpio_write(gpio3_map, MD0_PIN, 1); }
static void md0_data_mode(void)  { gpio_set_output(gpio3_map, MD0_PIN); gpio_write(gpio3_map, MD0_PIN, 0); }
static int  aux_ready(void)      { return gpio_read(gpio2_map, AUX_PIN); }

/* ==========================================================================
 *  UART 操作
 * ========================================================================== */
static int uart_fd = -1;

static int uart_open(const char *dev, int baud)
{
    struct termios tty;
    speed_t sp;

    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror(dev); return -1; }

    switch (baud) {
    case 9600:   sp = B9600;   break;
    case 115200: sp = B115200; break;
    default:     sp = B115200; break;
    }

    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, sp);
    cfsetispeed(&tty, sp);

    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 10;   /* 1s for first byte, 100ms inter-byte */

    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void uart_flush(void) { tcflush(uart_fd, TCIOFLUSH); }

static int uart_read_timeout(unsigned char *buf, int max_len, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;
    int total = 0;

    while (total < max_len) {
        FD_ZERO(&fds);
        FD_SET(uart_fd, &fds);
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int r = select(uart_fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) break;

        int n = read(uart_fd, buf + total, max_len - total);
        if (n > 0) total += n;
        else break;

        timeout_ms = 50;
    }
    return total;
}

/* ==========================================================================
 *  AT 命令 (使用 GD32 BSP mwcc68_app.c 验证过的格式)
 * ========================================================================== */
static int at_send(const char *cmd, int timeout_ms)
{
    unsigned char resp[256];
    int len;

    uart_flush();
    write(uart_fd, cmd, strlen(cmd));
    write(uart_fd, "\r\n", 2);
    usleep(150000);

    len = uart_read_timeout(resp, sizeof(resp) - 1, timeout_ms);
    if (len > 0) {
        resp[len] = '\0';
        while (len > 0 && (resp[len-1] == '\r' || resp[len-1] == '\n'))
            resp[--len] = '\0';
        printf("    [%d] %s\n", len, resp);
        if (strstr((char *)resp, "OK") || strstr((char *)resp, "+OK"))
            return 1;
    } else {
        printf("    [no response]\n");
    }
    return 0;
}

static int at(const char *cmd) { return at_send(cmd, 500); }

/* ==========================================================================
 *  LoRa 模块初始化 (照搬 GD32 mwcc68_app.c 流程)
 * ========================================================================== */
static int lora_init(void)
{
    int ok = 0;

    printf("[LoRa] Entering AT config mode (MD0=HIGH)...\n");
    md0_at_mode();
    usleep(500000);  /* GD32: cpu_delay_ms(500) */

    /* Step 1: 测试AT通信 (GD32: retry 20次) */
    printf("[LoRa] AT test (up to 20 retries)...\n");
    for (int retry = 0; retry < 20; retry++) {
        if (at("AT")) { ok = 1; break; }
        usleep(300000);
    }
    if (!ok) {
        printf("[LoRa] ERROR: Module not responding!\n");
        goto exit_cfg;
    }
    printf("[LoRa] AT OK\n");

    /* Step 2: 查询当前配置 (GD32: AT+ADDR? + AT+CFG?) */
    printf("[LoRa] Query ADDR...\n");
    at_send("AT+ADDR?", 1000);

    /* Step 3: 配置参数 */
    printf("[LoRa] Configuring (ADDR=0x%04X CHN=%d NETID=%d)...\n",
           LORA_ADDR, LORA_CHN, LORA_NETID);

    at("AT+ADDR=00,0B");      // GD32: LoRa_SetAddr(LORA_ADDR)
    at("AT+NETID=0");          // GD32: LoRa_SetNetid(LORA_NETID)
    at("AT+WLRATE=23,5");     // GD32: WLRATE + CHN (模块实际响应格式)
    at("AT+PACKSIZE=3");       // GD32: LoRa_SetPacksize(LORA_PACKSIZE)
    at("AT+TMODE=1");          // GD32: LoRa_SetTmode(LORA_TMODE)
    at("AT+TPOWER=4");         // GD32: LoRa_SetPower(LORA_TPOWER)
    at("AT+UART=7,0");         // GD32: LoRa_SetBps(LORA_TTLBPS)
    at("AT+FLASH=1");          // 保存到Flash

exit_cfg:
    /* Step 4: 退出AT模式, 等待模块重启 (照搬GD32 LoRa_ExitConfigMode) */
    printf("[LoRa] Exiting AT mode (MD0=LOW), waiting module reboot...\n");
    md0_data_mode();

    /* GD32: LoRa_WaitAuxHigh(5000) — 等5秒让AUX变HIGH */
    int aux_high = 0;
    for (int i = 0; i < 5000; i += 100) {
        usleep(100000);  /* 100ms */
        if (aux_ready()) { aux_high = 1; break; }
    }
    printf("[LoRa] AUX = %s (%s)\n",
           aux_high ? "HIGH" : "LOW",
           aux_high ? "module ready" : "TIMEOUT — may still work");

    return ok ? 0 : -1;
}

/* ==========================================================================
 *  数据解析和显示
 * ========================================================================== */
static volatile int g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

static void print_hex(const unsigned char *data, int len)
{
    printf("  HEX: ");
    for (int i = 0; i < len && i < 64; i++)
        printf("%02X ", data[i]);
    if (len > 64) printf("...(+%d)", len - 64);
    printf("\n");

    int printable = 0;
    for (int i = 0; i < len && i < 64; i++)
        if (data[i] >= 0x20 && data[i] <= 0x7E) printable++;
    if (printable > len * 2 / 3) {
        printf("  ASCII: ");
        for (int i = 0; i < len && i < 64; i++)
            putchar((data[i] >= 0x20 && data[i] <= 0x7E) ? data[i] : '.');
        printf("\n");
    }
}

static void print_frame(const unsigned char *data, int len)
{
    int offset = 0;

    /* 检测并剥离 LoRa 定点传输地址头 (3字节: addr_hi, addr_lo, chn) */
    /* 如果前3字节不是 AA 55, 可能是 LoRa TMODE 地址头 */
    if (len >= 6 && !(data[0] == 0xAA && data[1] == 0x55)) {
        uint16_t lora_addr = ((uint16_t)data[0] << 8) | data[1];
        uint8_t  lora_chn  = data[2];
        if (lora_chn <= 83 && len > 3 && data[3] == 0xAA && data[4] == 0x55) {
            printf("  [LoRa hdr] src=0x%04X chn=%d\n", lora_addr, lora_chn);
            offset = 3;
            len -= 3;
        }
    }

    /* GD32 协议帧解析 */
    if (len >= 10 && data[offset] == 0xAA && data[offset+1] == 0x55) {
        uint16_t data_len = ((uint16_t)data[offset+2] << 8) | data[offset+3];
        uint16_t dev_id   = ((uint16_t)data[offset+4] << 8) | data[offset+5];
        uint32_t ts       = ((uint32_t)data[offset+6] << 24) | ((uint32_t)data[offset+7] << 16)
                          | ((uint32_t)data[offset+8] << 8)  | data[offset+9];
        int header_size = 10; /* AA 55 + len(2) + dev_id(2) + ts(4) */
        int payload_len = data_len - 6; /* len includes header after AA55: 2(len)+2(dev)+4(ts)=8? no: data_len=0x0089=137 */

        printf("  [GD32 Proto] AA 55 | len=%u | dev=0x%04X",
               data_len, dev_id);

        /* 对比: 终端发送格式 [DATA] dest_hi dest_lo chn [AA 55 len dev_id ts payload...] */
        /* 定点模式 LoRa 模块剥掉前3字节地址头后，输出 AA 55 ... */
        printf("\n  [MATCH] ← \033[32mAA 55 %02X %02X %02X %02X ...\033[0m (终端发送的协议帧头)\n",
               data[offset+2], data[offset+3], data[offset+4], data[offset+5]);
    } else {
        printf("  [RAW] No GD32 frame header detected\n");
    }

    print_hex(data, len);
}

/* ==========================================================================
 *  Main
 * ========================================================================== */
int main(void)
{
    unsigned char buf[1024];
    unsigned long pkt_count = 0;
    unsigned long byte_total = 0;
    int n;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    setbuf(stdout, NULL);

    printf("========================================\n");
    printf(" Phytium PE2204 LoRa Receiver (Linux)\n");
    printf(" UART2 (Pin8=TXD, Pin10=RXD) @ 115200\n");
    printf(" GD32 v3 compatible: ADDR=0x%04X CHN=%d\n", LORA_ADDR, LORA_CHN);
    printf("========================================\n\n");

    /* ---- 1. 硬件初始化 (/dev/mem + mmap) ---- */
    hw_init();

    /* ---- 2. FIOPAD IOMUX: 配置 GPIO3_1(MD0) 和 GPIO2_10(AUX) ---- */
    printf("[IOMUX] Setting FIOPAD GPIO mux...\n");
    fiopad_set_gpio(FIOPAD_C49_REG0_OFF);
    printf("[IOMUX]   C49(0x%03X) → GPIO3_1 (MD0) = func%d\n", FIOPAD_C49_REG0_OFF, FIOPAD_FUNC6);
    fiopad_set_gpio(FIOPAD_A37_REG0_OFF);
    printf("[IOMUX]   A37(0x%03X) → GPIO2_10 (AUX) = func%d\n", FIOPAD_A37_REG0_OFF, FIOPAD_FUNC6);

    /* ---- 3. GPIO: 输出MD0低电平, 读取AUX ---- */
    gpio_set_output(gpio3_map, MD0_PIN);
    gpio_set_input(gpio2_map, AUX_PIN);
    md0_data_mode();

    printf("[GPIO] GPIO3_DDR=0x%04X DR=0x%04X EXT=0x%04X\n",
           gpio_read_reg(gpio3_map, GPIO_DDR_OFFSET),
           gpio_read_reg(gpio3_map, GPIO_DR_OFFSET),
           gpio_read_reg(gpio3_map, GPIO_EXT_OFFSET));
    printf("[GPIO] GPIO2_DDR=0x%04X EXT=0x%04X\n",
           gpio_read_reg(gpio2_map, GPIO_DDR_OFFSET),
           gpio_read_reg(gpio2_map, GPIO_EXT_OFFSET));
    printf("[GPIO] MD0=LOW, AUX=%s\n\n", aux_ready() ? "HIGH" : "LOW");

    /* ---- 4. UART2 ---- */
    uart_fd = uart_open("/dev/ttyAMA2", 115200);
    if (uart_fd < 0) {
        fprintf(stderr, "Failed to open /dev/ttyAMA2\n");
        hw_deinit();
        return 1;
    }
    printf("[UART] /dev/ttyAMA2 115200-8N1 OK\n\n");

    /* ---- 5. LoRa 模块配置 ---- */
    if (lora_init() < 0) {
        printf("\n[WARN] LoRa AT config failed, trying passive receive...\n\n");
    }

    uart_flush();

    /* ---- 6. 接收循环 ---- */
    printf("========================================\n");
    printf(" Waiting for LoRa data... (Ctrl+C to stop)\n");
    printf("========================================\n\n");

    while (g_running) {
        n = uart_read_timeout(buf, sizeof(buf), 200);
        if (n > 0) {
            pkt_count++;
            byte_total += n;

            printf("[PKT #%lu] len=%d\n", pkt_count, n);
            print_frame(buf, n);
        }
        usleep(10000);
    }

    printf("\n========================================\n");
    printf(" Done. Received %lu packets, %lu bytes\n", pkt_count, byte_total);
    printf("========================================\n");

    md0_data_mode();
    close(uart_fd);
    hw_deinit();
    return 0;
}
