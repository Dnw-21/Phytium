#ifndef __WAVE_DECODE_H
#define __WAVE_DECODE_H

#include <stdint.h>

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
 * @param pkt         输入: 编码后的数据包 (字节流)
 * @param pkt_len     输入: 数据包字节长度
 * @param out         输出: 解码后的 int16 采样点数组 (调用者分配)
 * @param out_capacity 输入: out 数组的最大容量
 * @return            实际解码出的采样点数量; 0 表示输入太短无法解码
 */
uint16_t wave_decode_packet(const uint8_t *pkt, uint16_t pkt_len,
                            int16_t *out, uint16_t out_capacity);

#endif /* __WAVE_DECODE_H */
