#include "lora_uart.h"
#include "ftypes.h"
#include "finterrupt.h"
#include <string.h>

/* ==========================================================================
 *  PE2204 UART3 (PL011) + GPIO2/3 寄存器定义
 *
 *  UART3 基址: 0x2800f000, 时钟: 100MHz, IRQ: 118 (GICv3 SPI)
 *  GPIO2 基址: 0x28036000  (AUX: pin 10)
 *  GPIO3 基址: 0x28037000  (MD0: pin 1)
 *
 *  Phytium GPIO 寄存器偏移:
 *    DATA:  0x000 (读写引脚电平)
 *    DIR:   0x400 (1=输出, 0=输入)
 * ========================================================================== */

#define UART3_BASE         0x2800f000U
#define GPIO3_BASE         0x28037000U
#define GPIO2_BASE         0x28036000U

/* Phytium GPIO registers (Port A only on PE2204) */
#define GPIO_DR_OFFSET     0x00  /* Port A Output Data Register */
#define GPIO_DDR_OFFSET    0x04  /* Port A Data Direction Register (1=out, 0=in) */
#define GPIO_EXT_OFFSET    0x08  /* Port A Input Data Register (read pin level) */

#define MD0_PIN             1
#define AUX_PIN             10

/* PL011 寄存器偏移 */
#define UART_DR            0x000
#define UART_FR            0x018
#define UART_IBRD          0x024
#define UART_FBRD          0x028
#define UART_LCR_H         0x02C
#define UART_CR            0x030
#define UART_IFLS          0x034
#define UART_IMSC          0x038
#define UART_RIS           0x03C
#define UART_MIS           0x040
#define UART_ICR           0x044

/* FR 标志位 */
#define UART_FR_TXFE       0x80
#define UART_FR_RXFF       0x40
#define UART_FR_TXFF       0x20
#define UART_FR_RXFE       0x10
#define UART_FR_BUSY       0x08

/* LCR_H */
#define UART_LCRH_FEN      0x10
#define UART_LCRH_WLEN_8   0x60

/* CR */
#define UART_CR_UARTEN     0x0001
#define UART_CR_RXE        0x0200
#define UART_CR_TXE        0x0100

/* IMSC/MIS */
#define UART_IMSC_RXIM     0x10
#define UART_IMSC_RTIM     0x40
#define UART_MIS_RXMIS     0x10
#define UART_MIS_RTMIS     0x40

/* IRQ */
#define FUART3_IRQ_NUM     118

#define UART_REG_RD(off)   (*(volatile unsigned int *)((UART3_BASE) + (off)))
#define UART_REG_WR(off, v) (*(volatile unsigned int *)((UART3_BASE) + (off)) = (v))

/* ==========================================================================
 *  RX 环形缓冲区
 * ========================================================================== */
#define RX_RING_SIZE        4096

static unsigned char rx_ring[RX_RING_SIZE];
static volatile unsigned int  rx_head = 0;
static volatile unsigned int  rx_tail = 0;
static volatile unsigned int  rx_isr_count = 0;
static volatile unsigned int  rx_byte_total = 0;

unsigned int lora_uart_get_isr_count(void) { return rx_isr_count; }
unsigned int lora_uart_get_byte_total(void) { return rx_byte_total; }

static void ring_put(unsigned char byte)
{
    unsigned int next = (rx_head + 1) % RX_RING_SIZE;
    if (next != rx_tail) {
        rx_ring[rx_head] = byte;
        rx_head = next;
    }
}

static int ring_get(unsigned char *byte)
{
    if (rx_tail == rx_head) return -1;
    *byte = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1) % RX_RING_SIZE;
    return 0;
}

static unsigned int ring_avail(void)
{
    return (rx_head - rx_tail + RX_RING_SIZE) % RX_RING_SIZE;
}

/* ==========================================================================
 *  帧边界队列 (GD32 兼容 API)
 * ========================================================================== */
#define LORA_FRAME_QUEUE    8

static volatile uint16_t frame_mark[LORA_FRAME_QUEUE];
static volatile uint8_t  frame_head_mark;
static volatile uint8_t  frame_tail_mark;

/* ==========================================================================
 *  UART3 接收中断 ISR (不调用 FreeRTOS API)
 * ========================================================================== */
static void lora_uart_isr(s32 vector, void *param)
{
    uint32_t mis;
    (void)vector;
    (void)param;

    rx_isr_count++;

    mis = UART_REG_RD(UART_MIS);

    if (mis & UART_MIS_RXMIS) {
        while (!(UART_REG_RD(UART_FR) & UART_FR_RXFE)) {
            uint8_t byte = (uint8_t)(UART_REG_RD(UART_DR) & 0xFF);
            ring_put(byte);
            rx_byte_total++;
        }
    }

    if (mis & UART_MIS_RTMIS) {
        while (!(UART_REG_RD(UART_FR) & UART_FR_RXFE)) {
            uint8_t byte = (uint8_t)(UART_REG_RD(UART_DR) & 0xFF);
            ring_put(byte);
            rx_byte_total++;
        }
    }

    UART_REG_WR(UART_ICR, mis);
}

/* ==========================================================================
 *  GPIO 初始化
 * ========================================================================== */
static void md0_init(void)
{
    volatile unsigned int *gpio3_ddr  = (volatile unsigned int *)(GPIO3_BASE + GPIO_DDR_OFFSET);
    volatile unsigned int *gpio3_dr   = (volatile unsigned int *)(GPIO3_BASE + GPIO_DR_OFFSET);
    unsigned int val;

    /* Read current DDR, set MD0 bit as output, write back */
    val = *gpio3_ddr;
    val |= (1U << MD0_PIN);
    *gpio3_ddr = val;
    __asm__ volatile("DSB SY" ::: "memory");

    /* Read current DR, clear MD0 bit (LOW = transparent mode), write back */
    val = *gpio3_dr;
    val &= ~(1U << MD0_PIN);
    *gpio3_dr = val;
    __asm__ volatile("DSB SY" ::: "memory");
}

static void aux_init(void)
{
    volatile unsigned int *gpio2_ddr = (volatile unsigned int *)(GPIO2_BASE + GPIO_DDR_OFFSET);
    unsigned int val;

    val = *gpio2_ddr;
    val &= ~(1U << AUX_PIN);
    *gpio2_ddr = val;
    __asm__ volatile("DSB SY" ::: "memory");
}

int lora_aux_is_busy(void)
{
    /* Read actual pin level via EXT register (input data) */
    volatile unsigned int *gpio2_ext = (volatile unsigned int *)(GPIO2_BASE + GPIO_EXT_OFFSET);
    return !(*gpio2_ext & (1U << AUX_PIN));
}

/* ==========================================================================
 *  lora_uart_init: 仅初始化 UART3 硬件, 不发送 AT 命令
 *  波特率: 115200-8N1 (匹配 GD32 端)
 * ========================================================================== */
int lora_uart_init(void)
{
    unsigned int tmp;

    /* 1. 禁用 UART3 */
    UART_REG_WR(UART_CR, 0);

    /* 2. 波特率: 115200 @ 100MHz */
    UART_REG_WR(UART_IBRD, 54);
    UART_REG_WR(UART_FBRD, 16);

    /* 3. 8N1 + FIFO */
    UART_REG_WR(UART_LCR_H, UART_LCRH_WLEN_8 | UART_LCRH_FEN);

    /* 4. FIFO 触发级别 */
    UART_REG_WR(UART_IFLS, 0x00);

    /* 5. 使能 UART + RX + TX */
    UART_REG_WR(UART_CR, UART_CR_UARTEN | UART_CR_RXE | UART_CR_TXE);

    /* 6. 清空残留 RX 数据 */
    while (!(UART_REG_RD(UART_FR) & UART_FR_RXFE)) {
        tmp = UART_REG_RD(UART_DR);
        (void)tmp;
    }

    /* 7. GPIO 初始化 */
    md0_init();
    aux_init();

    /* 8. 环形缓冲区重置 */
    rx_head = 0;
    rx_tail = 0;
    rx_isr_count = 0;
    rx_byte_total = 0;
    frame_head_mark = 0;
    frame_tail_mark = 0;

    /* 9. 清除所有挂起中断 */
    UART_REG_WR(UART_ICR, 0xFFFFFFFF);

    /* 10. 使能 RX 中断 + RX 超时中断 */
    UART_REG_WR(UART_IMSC, UART_IMSC_RXIM | UART_IMSC_RTIM);

    /* 11. 注册 ISR 到 GICv3 */
    InterruptInstall(FUART3_IRQ_NUM, lora_uart_isr, NULL, "lora_uart");
    InterruptSetPriority(FUART3_IRQ_NUM, 0x80);
    InterruptUmask(FUART3_IRQ_NUM);

    return 0;
}

/* ==========================================================================
 *  lora_uart_poll: 轮询读取 UART RX FIFO 到环形缓冲区
 * ========================================================================== */
void lora_uart_poll(void)
{
    while (!(UART_REG_RD(UART_FR) & UART_FR_RXFE)) {
        ring_put((unsigned char)(UART_REG_RD(UART_DR) & 0xFF));
        rx_byte_total++;
    }
}

/* ==========================================================================
 *  GD32 兼容 API
 * ========================================================================== */
uint16_t lora_uart_get_rx_count(void)
{
    return ring_avail();
}

void lora_uart_clear_buffer(void)
{
    rx_tail = rx_head;
    frame_head_mark = 0;
    frame_tail_mark = 0;
}

void lora_uart_mark_frame(void)
{
    uint8_t next = (frame_head_mark + 1) % LORA_FRAME_QUEUE;
    if (next != frame_tail_mark) {
        frame_mark[frame_head_mark] = rx_head;
        frame_head_mark = next;
    }
}

uint16_t lora_uart_read_frame(uint8_t *buf, uint16_t max_len)
{
    if (frame_head_mark == frame_tail_mark) return 0;

    uint16_t end   = frame_mark[frame_tail_mark];
    uint16_t start = rx_tail;

    frame_tail_mark = (frame_tail_mark + 1) % LORA_FRAME_QUEUE;

    uint16_t len;
    if (end >= start) {
        len = end - start;
    } else {
        len = RX_RING_SIZE - start + end;
    }
    if (len == 0) return 0;
    if (len > max_len) len = max_len;

    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rx_ring[start];
        start = (start + 1) % RX_RING_SIZE;
    }
    rx_tail = end;

    return len;
}

/* ==========================================================================
 *  帧提取状态机 (GD32 兼容)
 * ========================================================================== */
typedef enum {
    SYNC_HDR1, SYNC_HDR2, SYNC_LEN_H, SYNC_LEN_L,
    SYNC_DATA, SYNC_CRC, SYNC_TAIL1, SYNC_TAIL2
} sync_state_t;

uint16_t lora_uart_recv_frame(uint8_t *buf, uint16_t max_len)
{
    static sync_state_t s_state    = SYNC_HDR1;
    static uint16_t     s_data_len = 0;
    static uint16_t     s_idx      = 0;
    static uint8_t      s_crc      = 0;
    uint8_t             byte;
    uint16_t            total;

    while (ring_get(&byte) == 0) {
        switch (s_state) {
        case SYNC_HDR1:
            if (byte == 0xAA) s_state = SYNC_HDR2;
            break;
        case SYNC_HDR2:
            if (byte == 0x55) s_state = SYNC_LEN_H;
            else if (byte != 0xAA) s_state = SYNC_HDR1;
            break;
        case SYNC_LEN_H:
            s_data_len = ((uint16_t)byte << 8);
            s_state = SYNC_LEN_L;
            break;
        case SYNC_LEN_L:
            s_data_len |= byte;
            if (s_data_len == 0 || s_data_len > (max_len - 7)) {
                s_state = SYNC_HDR1;
                s_data_len = 0;
                break;
            }
            s_idx = 0;
            s_crc = 0;
            s_state = SYNC_DATA;
            break;
        case SYNC_DATA:
            buf[4 + s_idx] = byte;
            s_crc ^= byte;
            s_idx++;
            if (s_idx >= s_data_len) s_state = SYNC_CRC;
            break;
        case SYNC_CRC:
            if (s_crc == byte) s_state = SYNC_TAIL1;
            else { s_state = SYNC_HDR1; s_data_len = 0; }
            break;
        case SYNC_TAIL1:
            if (byte == 0x55) s_state = SYNC_TAIL2;
            else { s_state = SYNC_HDR1; s_data_len = 0; }
            break;
        case SYNC_TAIL2:
            if (byte == 0xAA) {
                total = 4 + s_idx + 3;
                buf[0] = 0xAA;
                buf[1] = 0x55;
                buf[2] = (uint8_t)(s_idx >> 8);
                buf[3] = (uint8_t)(s_idx);
                s_state = SYNC_HDR1;
                s_data_len = 0;
                return total;
            }
            s_state = SYNC_HDR1;
            s_data_len = 0;
            break;
        }
    }
    return 0;
}

/* ==========================================================================
 *  lora_uart_send: 阻塞发送
 * ========================================================================== */
void lora_uart_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (UART_REG_RD(UART_FR) & UART_FR_TXFF)
            ;
        UART_REG_WR(UART_DR, data[i]);
    }
    while (UART_REG_RD(UART_FR) & UART_FR_BUSY)
        ;
}

/* ==========================================================================
 *  lora_uart_send_str: 发送字符串 (调试用)
 * ========================================================================== */
void lora_uart_send_str(const char *str)
{
    while (*str) {
        while (UART_REG_RD(UART_FR) & UART_FR_TXFF)
            ;
        UART_REG_WR(UART_DR, (unsigned int)(*str));
        str++;
    }
    while (UART_REG_RD(UART_FR) & UART_FR_BUSY)
        ;
}
