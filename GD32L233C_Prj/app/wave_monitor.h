#ifndef __WAVE_MONITOR_H
#define __WAVE_MONITOR_H

#include <stdint.h>
#include "data_frame.h"
#include "data_monitor.h"
#include "FreeRTOS.h"
#include "semphr.h"

extern int16_t           g_flush_buf[2][DMA_PINGPONG_SIZE];
extern volatile uint8_t  g_flush_pending;
extern SemaphoreHandle_t g_wave_flush_sem;

/*============================================================================
 *                          对外接口函数
 *============================================================================*/

/*--- 初始化 ---*/
void wave_monitor_init(void);

/*--- Flash读写 ---*/
void wave_flash_init(void);                    
void wave_flash_write_bytes(uint32_t off, const uint8_t *data, uint16_t len);
void wave_flash_read_bytes(uint32_t off, int16_t *data, uint16_t len);

/*--- 波形DMA控制 ---*/
void dma_wave_buf_done(uint8_t buf_idx);
void dma_wave_start(uint16_t rate);
void dma_wave_stop(void);
void *dma_wave_buf_ptr(void);


/*--- 模式切换 ---*/
void switch_to_warning_mode(void);              /* 切换到预警: 6kHz×250ms录波 */
void switch_to_danger_mode(void);               /* 切换到紧急: 12kHz×450ms录波 */
void switch_to_normal_mode(void);               /* 恢复到正常: 停止录波 */

/*--- 故障记录查询 (按指令驱动) ---*/
uint8_t wave_retrieve_by_node_fault(uint8_t node_idx, uint8_t fault_idx); /* Flash读波形并发送 */
FaultRecord_t *get_fault_record(uint8_t index);      /* 获取故障记录 */
uint8_t get_fault_record_count(void);                /* 获取故障记录总数 */

/*--- 辅助查询 ---*/
uint16_t get_current_sample_rate(void);        /* 当前采样率 */
uint8_t is_wave_recording(void);               /* 是否正在录波 */

/*--- 波形记录回调 (由data_monitor调用) ---*/
void wave_set_active_node(uint8_t node_idx);        /* 设置当前采样节点 */
void wave_set_fault_detect_time(uint32_t fault_time); /* 设置故障检测时间 */
void wave_set_fault_rms(float rms);                    /* 设置故障RMS, 供录完时保存用 */
void wave_save_fault_record(uint32_t fault_time, SeverityLevel_t severity, float rms); /* 保存故障记录 */

#endif
