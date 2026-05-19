#include "lora_uart.h"
#include <string.h>

/* ==========================================================================
 *  PE2204 UART3 (PL011) 寄存器定义
 *  基址: 0x2800f000, 时钟: 100MHz
 * ========================================================================== */

#define UART3_BASE         0x2800f000U

#define UART_DR            0x000
#define UART_FR            0x018
#define UART_IBRD          0x024
#define UART_FBRD          0x028
#define UART_LCR_H         0x02C
#define UART_CR            0x030
#define UART_IFLS          0x034

#define UART_FR_TXFE       0x80
#define UART_FR_RXFF       0x40
#define UART_FR_TXFF       0x20
#define UART_FR_RXFE       0x10
#define UART_FR_BUSY       0x08

#define UART_LCRH_FEN      0x10
#define UART_LCRH_WLEN_8   0x60

#define UART_CR_UARTEN     0x0001
#define UART_CR_RXE        0x0200
#define UART_CR_TXE        0x0100

#define UART_REG_RD(off)   (*(volatile unsigned int *)((UART3_BASE) + (off)))
#define UART_REG_WR(off, v) (*(volatile unsigned int *)((UART3_BASE) + (off)) = (v))

/* ==========================================================================
 *  RX 环形缓冲区
 * ========================================================================== */

#define RX_RING_SIZE        4096

static unsigned char rx_ring[RX_RING_SIZE];
static unsigned int  rx_head = 0;
static unsigned int  rx_tail = 0;

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
 *  UART3 初始化: 115200-8N1, FIFO 使能, 轮询模式
 * ========================================================================== */

int lora_uart_init(void)
{
    unsigned int tmp;

    UART_REG_WR(UART_CR, 0);

    /* 波特率: 100MHz / (16 * 115200) = 54.2535 → IBRD=54, FBRD=16 */
    UART_REG_WR(UART_IBRD, 54);
    UART_REG_WR(UART_FBRD, 16);

    /* 8N1 + FIFO */
    UART_REG_WR(UART_LCR_H, UART_LCRH_WLEN_8 | UART_LCRH_FEN);

    /* FIFO 触发级别: RX=1/8, TX=1/8 */
    UART_REG_WR(UART_IFLS, 0x00);

    /* 使能 UART + RX + TX, 不使能中断 */
    UART_REG_WR(UART_CR, UART_CR_UARTEN | UART_CR_RXE | UART_CR_TXE);

    /* 清空可能残留的数据 */
    while (!(UART_REG_RD(UART_FR) & UART_FR_RXFE)) {
        tmp = UART_REG_RD(UART_DR);
        (void)tmp;
    }

    rx_head = 0;
    rx_tail = 0;

    return 0;
}

/* ==========================================================================
 *  lora_uart_poll: 从 UART RX FIFO 读取所有可用字节到环形缓冲区
 *  由 master_recv_task 每周期调用
 * ========================================================================== */

void lora_uart_poll(void)
{
    unsigned int fr;
    unsigned char byte;

    while (1) {
        fr = UART_REG_RD(UART_FR);
        if (fr & UART_FR_RXFE) break;
        byte = (unsigned char)(UART_REG_RD(UART_DR) & 0xFF);
        ring_put(byte);
    }
}

/* ==========================================================================
 *  lora_uart_recv_frame: 从环形缓冲区提取一个完整 LoRa 帧
 *
 *  帧格式 (与 GD32 一致):
 *    [0xAA][0x55][LEN_H][LEN_L][ DATA(N 字节) ][CRC8][0x55][0xAA]
 *    帧总长 = 7 + N
 *
 *  返回值: 帧总长度 (含帧头帧尾), 无完整帧返回 0
 * ========================================================================== */

typedef enum {
    SYNC_HDR1,
    SYNC_HDR2,
    SYNC_LEN_H,
    SYNC_LEN_L,
    SYNC_DATA,
    SYNC_CRC,
    SYNC_TAIL1,
    SYNC_TAIL2
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
            if (byte == 0x55) {
                s_state = SYNC_LEN_H;
            } else if (byte != 0xAA) {
                s_state = SYNC_HDR1;
            }
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
            if (s_idx >= s_data_len) {
                s_state = SYNC_CRC;
            }
            break;

        case SYNC_CRC:
            if (s_crc == byte) {
                s_state = SYNC_TAIL1;
            } else {
                s_state = SYNC_HDR1;
                s_data_len = 0;
            }
            break;

        case SYNC_TAIL1:
            if (byte == 0x55) {
                s_state = SYNC_TAIL2;
            } else {
                s_state = SYNC_HDR1;
                s_data_len = 0;
            }
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
 *  lora_uart_send: 阻塞发送原始字节到 LoRa 模块
 *
 *  MD0 引脚未接, 不做 AUX 忙检测, 仅等待 TX FIFO 非满
 * ========================================================================== */

void lora_uart_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        while (UART_REG_RD(UART_FR) & UART_FR_TXFF)
            ;
        UART_REG_WR(UART_DR, data[i]);
    }

    /* 等待发送完成 (BUSY 标志清除) */
    while (UART_REG_RD(UART_FR) & UART_FR_BUSY)
        ;
}
