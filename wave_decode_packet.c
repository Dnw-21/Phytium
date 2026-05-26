#include <stdint.h>
#include <stddef.h>

/**
 * wave_decode_packet - 解码一帧差分编码的波形数据包
 *
 * 编码规则:
 *   - 第1个采样点: 原始 int16，大端序 2 字节
 *   - 后续采样点:
 *       delta = cur - prev
 *       若 -128 < delta < 127:  1 字节 (int8_t)delta
 *       否则:                   0x80 + 原始 int16 大端序 2 字节
 *
 * @param pkt        输入: 编码后的数据包 (字节流)
 * @param pkt_len    输入: 数据包字节长度
 * @param out        输出: 解码后的 int16 采样点数组 (调用者分配, 建议至少 219 个)
 * @param out_capacity 输入: out 数组的最大容量
 * @return           实际解码出的采样点数量; 0 表示输入太短无法解码
 */
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