#ifndef __WAVE_MONITOR_H
#define __WAVE_MONITOR_H

#include <stdint.h>
#include "data_monitor.h"

/*============================================================================
 *                          对外接口函数
 *============================================================================*/

/*--- 初始化 ---*/
void wave_monitor_init(void);

/*--- Flash读写 ---*/
void wave_flash_init(void);                    
void wave_flash_write_bytes(uint32_t off, const uint8_t *data, uint16_t len);

/*--- 波形DMA控制 ---*/
void dma_wave_buf_done(uint8_t buf_idx);
void dma_wave_start(uint16_t rate);
void dma_wave_stop(void);
void *dma_wave_buf_ptr(void);


/*--- 模式切换 ---*/
void switch_to_warning_mode(void);              /* 切换到预警: 12kHz×250ms录波 */
void switch_to_danger_mode(void);               /* 切换到紧急: 15kHz×450ms录波 */
void switch_to_normal_mode(void);               /* 恢复到正常: 停止录波 */

/*--- 波形记录回调 (由data_monitor调用) ---*/
void wave_set_active_node(uint8_t node_idx);        /* 设置当前采样节点 */
void wave_set_fault_detect_time(uint32_t fault_time); /* 设置故障检测时间 */
void wave_set_fault_rms(float rms);                    /* 设置故障RMS, 供录完时保存用 */
void wave_save_fault_record(uint32_t fault_time, SeverityLevel_t severity, float rms); /* 保存故障记录 */

#endif
