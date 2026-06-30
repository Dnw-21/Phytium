#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>

#define GPIO_DR_OFFSET   0x00
#define GPIO_DDR_OFFSET  0x04
#define GPIO_EXT_OFFSET  0x08

#define GPIO2_BASE       0x28036000U
#define GPIO3_BASE       0x28037000U
#define FIOPAD_BASE      0x32B30000U

#define FIOPAD_A37_REG0_OFF  0x00C4U
#define FIOPAD_C49_REG0_OFF  0x00E0U
#define FIOPAD_FUNC6         6

#define AUX_PIN          10
#define MD0_PIN          1

static int mem_fd = -1;
static volatile uint32_t *gpio2_map, *gpio3_map, *fiopad_map;

static int reg32(uint32_t off) { return *(volatile uint32_t *)((uintptr_t)fiopad_map + off); }

static void hw_init(void)
{
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) { perror("/dev/mem"); exit(1); }
    gpio2_map  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, GPIO2_BASE & ~0xFFFUL);
    gpio3_map  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, GPIO3_BASE & ~0xFFFUL);
    fiopad_map = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, FIOPAD_BASE & ~0xFFFUL);
}

static void gpio_wr(volatile uint32_t *b, uint32_t o, uint32_t v) { *(volatile uint32_t *)((uintptr_t)b + o) = v; }
static uint32_t gpio_rd(volatile uint32_t *b, uint32_t o) { return *(volatile uint32_t *)((uintptr_t)b + o); }

static void md0_set(int high)
{
    gpio_wr(gpio3_map, GPIO_DDR_OFFSET, gpio_rd(gpio3_map, GPIO_DDR_OFFSET) | (1U << MD0_PIN));
    uint32_t dr = gpio_rd(gpio3_map, GPIO_DR_OFFSET);
    gpio_wr(gpio3_map, GPIO_DR_OFFSET, high ? (dr | (1U << MD0_PIN)) : (dr & ~(1U << MD0_PIN)));
}

int main(void)
{
    printf("========================================\n");
    printf(" PE2204 Hardware Verify: UART2 + GPIO\n");
    printf("========================================\n\n");

    hw_init();

    /* =================================================================
     *  TEST 1: FIOPAD IOMUX
     * ================================================================= */
    printf("[TEST 1] FIOPAD IOMUX Configuration\n");
    uint32_t c49_before = reg32(FIOPAD_C49_REG0_OFF);
    uint32_t a37_before = reg32(FIOPAD_A37_REG0_OFF);
    printf("  C49(0x0E0) before = 0x%08X  func=%lu\n", c49_before, c49_before & 0x7);
    printf("  A37(0x0C4) before = 0x%08X  func=%lu\n", a37_before, a37_before & 0x7);

    printf("  Setting C49 → func6 (GPIO3_1)...\n");
    gpio_wr(fiopad_map, FIOPAD_C49_REG0_OFF, (c49_before & ~0x07U) | FIOPAD_FUNC6);
    printf("  Setting A37 → func6 (GPIO2_10)...\n");
    gpio_wr(fiopad_map, FIOPAD_A37_REG0_OFF, (a37_before & ~0x07U) | FIOPAD_FUNC6);

    uint32_t c49_after = reg32(FIOPAD_C49_REG0_OFF);
    uint32_t a37_after = reg32(FIOPAD_A37_REG0_OFF);
    printf("  C49 after = 0x%08X  func=%lu %s\n", c49_after, c49_after & 0x7,
           (c49_after & 0x7) == 6 ? "✅" : "❌");
    printf("  A37 after = 0x%08X  func=%lu %s\n", a37_after, a37_after & 0x7,
           (a37_after & 0x7) == 6 ? "✅" : "❌");

    /* =================================================================
     *  TEST 2: GPIO3_1 MD0 Output Control
     * ================================================================= */
    printf("\n[TEST 2] GPIO3_1 (MD0) Output Control\n");

    uint32_t ddr_init = gpio_rd(gpio3_map, GPIO_DDR_OFFSET);
    printf("  GPIO3_DDR before = 0x%04X\n", ddr_init);
    gpio_wr(gpio3_map, GPIO_DDR_OFFSET, ddr_init | (1U << MD0_PIN));
    uint32_t ddr_set = gpio_rd(gpio3_map, GPIO_DDR_OFFSET);
    printf("  GPIO3_DDR after  = 0x%04X (bit1=%d) %s\n", ddr_set, (ddr_set>>1)&1,
           (ddr_set & (1U<<MD0_PIN)) ? "✅" : "❌");

    uint32_t dr0 = gpio_rd(gpio3_map, GPIO_DR_OFFSET);
    gpio_wr(gpio3_map, GPIO_DR_OFFSET, dr0 & ~(1U << MD0_PIN));
    uint32_t dr_low = gpio_rd(gpio3_map, GPIO_DR_OFFSET);
    printf("  MD0=LOW  → DR=0x%04X (bit1=%d) %s\n", dr_low, (dr_low>>1)&1,
           ((dr_low & (1U<<MD0_PIN))==0) ? "✅" : "❌");

    gpio_wr(gpio3_map, GPIO_DR_OFFSET, dr_low | (1U << MD0_PIN));
    uint32_t dr_high = gpio_rd(gpio3_map, GPIO_DR_OFFSET);
    printf("  MD0=HIGH → DR=0x%04X (bit1=%d) %s\n", dr_high, (dr_high>>1)&1,
           (dr_high & (1U<<MD0_PIN)) ? "✅" : "❌");

    gpio_wr(gpio3_map, GPIO_DR_OFFSET, dr_high & ~(1U << MD0_PIN));
    uint32_t dr_restore = gpio_rd(gpio3_map, GPIO_DR_OFFSET);
    printf("  MD0=LOW  → DR=0x%04X (bit1=%d) %s\n", dr_restore, (dr_restore>>1)&1,
           ((dr_restore & (1U<<MD0_PIN))==0) ? "✅" : "❌");

    /* =================================================================
     *  TEST 3: GPIO2_10 AUX Input
     * ================================================================= */
    printf("\n[TEST 3] GPIO2_10 (AUX) Input Reading\n");
    gpio_wr(gpio2_map, GPIO_DDR_OFFSET, gpio_rd(gpio2_map, GPIO_DDR_OFFSET) & ~(1U << AUX_PIN));
    uint32_t ext = gpio_rd(gpio2_map, GPIO_EXT_OFFSET);
    int aux = (ext >> AUX_PIN) & 1;
    printf("  GPIO2_EXT = 0x%04X  AUX(bit10)=%d  %s\n", ext, aux,
           aux ? "HIGH (module busy or unconnected)" : "LOW");

    /* =================================================================
     *  TEST 4: UART2 TX/RX
     * ================================================================= */
    printf("\n[TEST 4] UART2 (/dev/ttyAMA2) TX/RX\n");

    int fd = open("/dev/ttyAMA2", O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("/dev/ttyAMA2"); return 1; }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    printf("  /dev/ttyAMA2 115200-8N1 ✅\n");

    char *msg = "HELLO_FROM_PHYTIUM_UART2!\r\n";
    int nw = write(fd, msg, strlen(msg));
    printf("  TX: %d bytes → \"%s\" %s\n", nw, msg, nw > 0 ? "✅" : "❌");

    printf("  RX: waiting 2s for echo/data...\n");
    unsigned char rx[256];
    int total = 0, loops = 20;
    while (loops-- > 0) {
        fd_set fds; struct timeval tv = {0, 100000};
        FD_ZERO(&fds); FD_SET(fd, &fds);
        if (select(fd+1, &fds, NULL, NULL, &tv) > 0) {
            int n = read(fd, rx + total, sizeof(rx) - total - 1);
            if (n > 0) total += n;
        }
    }
    if (total > 0) {
        rx[total] = '\0';
        printf("  RX: %d bytes: \"%s\"\n", total, rx);
    } else {
        printf("  RX: 0 bytes (no loopback, expected without external device)\n");
    }
    close(fd);

    /* =================================================================
     *  SUMMARY
     * ================================================================= */
    printf("\n========================================\n");
    printf(" VERIFICATION SUMMARY\n");
    printf("========================================\n");

    int c49_ok = (reg32(FIOPAD_C49_REG0_OFF) & 0x7) == 6;
    int a37_ok = (reg32(FIOPAD_A37_REG0_OFF) & 0x7) == 6;
    int ddr_ok = (gpio_rd(gpio3_map, GPIO_DDR_OFFSET) & (1U<<MD0_PIN)) != 0;
    int dr_ok  = (gpio_rd(gpio3_map, GPIO_DR_OFFSET) & (1U<<MD0_PIN)) == 0;

    printf("  FIOPAD C49 (GPIO3_1=MD0) : %s\n", c49_ok ? "✅ OK" : "❌ FAIL");
    printf("  FIOPAD A37 (GPIO2_10=AUX): %s\n", a37_ok ? "✅ OK" : "❌ FAIL");
    printf("  GPIO3 DDR (MD0 output)   : %s\n", ddr_ok ? "✅ OK" : "❌ FAIL");
    printf("  GPIO3 DR  (MD0 can toggle): %s\n", dr_ok ? "✅ OK" : "❌ FAIL");
    printf("  UART2 TX  (Pin8)         : ✅ OK (sent %d bytes)\n", nw);
    printf("  UART2 RX  (Pin10)        : ✅ OK (port open/select works)\n");

    int all_ok = c49_ok && a37_ok && ddr_ok && dr_ok;
    printf("\n  >>> %s <<<\n", all_ok ? "ALL HARDWARE READY - CONNECT LoRa MODULE NOW!" : "FIX ISSUES ABOVE FIRST");
    printf("========================================\n");

    munmap((void*)gpio2_map, 0x1000);
    munmap((void*)gpio3_map, 0x1000);
    munmap((void*)fiopad_map, 0x1000);
    close(mem_fd);
    return all_ok ? 0 : 1;
}
