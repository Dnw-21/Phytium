#include "wave_decode.h"
#include <stddef.h>

uint16_t wave_decode_packet(const uint8_t *pkt, uint16_t pkt_len,
                            int16_t *out, uint16_t out_capacity)
{
    if (pkt_len < 2 || out_capacity < 1)
        return 0;

    uint16_t pi = 0;   /* pkt 读取位置 */
    uint16_t oi = 0;   /* out 写入位置 */

    /* 第1个采样点: 原始 int16 大端序 */
    out[oi] = (int16_t)(((uint16_t)pkt[pi] << 8) | pkt[pi + 1]);
    pi += 2;
    oi++;

    /* 后续采样点 */
    while (pi < pkt_len && oi < out_capacity) {
        if (pkt[pi] == 0x80) {
            /* 0x80 标记: 后面 2 字节是原始 int16 大端序 */
            if (pi + 3 > pkt_len) break;
            out[oi] = (int16_t)(((uint16_t)pkt[pi + 1] << 8) | pkt[pi + 2]);
            pi += 3;
        } else {
            /* 差分编码: 1 字节有符号增量 */
            out[oi] = out[oi - 1] + (int16_t)((int8_t)pkt[pi]);
            pi += 1;
        }
        oi++;
    }

    return oi;
}
