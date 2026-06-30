#ifndef __DATA_MONITOR_H
#define __DATA_MONITOR_H

#include <stdint.h>
#include "data_frame.h"
#include "zdata_adaptive.h"

/*============================================================================
 *  GD32L233C: SRAM 32KB, Flash 256KB   LoRa: max kbps
 *============================================================================*/

#define NODE_SAMPLE_RATE         1000       /* 节点采样率 1kHz */
#define SAMPLES_PER_CYCLE        20         /* 50Hz每周期20点 */
#define NODE_BUFFER_CYCLES       4         /* 环缓冲4周期 = 80点, 装下完整 60+20 故障周期 */
#define NODE_BUFFER_SIZE         (SAMPLES_PER_CYCLE * NODE_BUFFER_CYCLES)   //60点

#define NODE_FAULT_SNAP_FLASH_SIZE  (40 * 1024) /* 故障快照Flash 40KB */
#define NODE_FAULT_SNAP_FLASH_ADDR  0x08030000  /* 0x08020000 + 64KB波形 */
#define NODE_FAULT_SNAP_PAGE_SIZE   4096

#define FAULT_SNAP_POINTS        60
#define FAULT_SNAP_RAW_BYTES     (FAULT_SNAP_POINTS * sizeof(NodeSample_t)) /* 3120B (60×52) */
#define FAULT_SNAP_HEADER_BYTES  12    /* ts4+sev1+type1+pts2+res4 */
#define FAULT_SNAP_TOTAL_BYTES   (FAULT_SNAP_HEADER_BYTES + FAULT_SNAP_RAW_BYTES) /* 3132B */
#define MAX_PENDING_FAULTS       5
#define MID_FREQ_WINDOW_MS       10         /* 滑动检测窗口 (每采样点滑窗一次) */
#define MID_FREQ_WINDOW_SAMPLES  (NODE_SAMPLE_RATE * MID_FREQ_WINDOW_MS / 1000) /* 10点 */

#define NORMAL_UPLOAD_CYCLES     1          /* 正常上传1周期 (20点) */
#define NORMAL_UPLOAD_POINTS     (SAMPLES_PER_CYCLE * NORMAL_UPLOAD_CYCLES)

#define FAULT_UPLOAD_CYCLES      2          /* 故障上传前后2周期 (40点) */
#define FAULT_UPLOAD_POINTS      (SAMPLES_PER_CYCLE * FAULT_UPLOAD_CYCLES)

#define WAVE_SAMPLE_RATE_WARN    12000      /* 预警波形12kHz */
#define WAVE_SAMPLE_RATE_DANGER  15000      /* 紧急波形15kHz */
#define WARNING_WAVE_MS          250        /* 预警录制时长 */
#define DANGER_WAVE_MS           450        /* 紧急录制时长 */
#define WARNING_WAVE_SAMPLES     (WAVE_SAMPLE_RATE_WARN * WARNING_WAVE_MS / 1000)   /* 3000点 */
#define DANGER_WAVE_SAMPLES      (WAVE_SAMPLE_RATE_DANGER * DANGER_WAVE_MS / 1000)  /* 5400点 */

#define DMA_PINGPONG_SIZE        64         /* DMA双缓冲大小 (样本数, 2×64×2B=256B) */

#define WAVE_FLASH_SIZE          (64 * 1024)      /* Flash波形存储区 64KB */
#define WAVE_FLASH_ADDR          0x08020000
#define WAVE_FLASH_PAGE_SIZE     4096   //Flash页大小

#define MAX_FAULT_RECORDS         5          /* Flash即传即清, 5条够用 */

#define VOLTAGE_NOMINAL          1.0f  /* 归一化后的值*/
#define VOLTAGE_OVER_LIMIT       (VOLTAGE_NOMINAL * 1.05f)   /* 过压阈值 */
#define VOLTAGE_UNDER_LIMIT      (VOLTAGE_NOMINAL * 0.95f)   /* 欠压阈值 */
#define VOLTAGE_SAG_THRESHOLD    (VOLTAGE_NOMINAL * 0.90f)   /* 暂降阈值 */
#define VOLTAGE_SWELL_THRESHOLD  (VOLTAGE_NOMINAL * 1.10f)   /* 暂升阈值 */

/*============================================================================
 *  故障类型枚举: 覆盖电压暂升/暂降/过压/欠压
 *============================================================================*/
typedef enum {
    FAULT_NONE = 0,              /* 无故障 */
    FAULT_OVER_VOLTAGE,          /* 过压: Vrms > 1.10pu */
    FAULT_UNDER_VOLTAGE,         /* 欠压: Vrms < 0.90pu */
    FAULT_VOLTAGE_SAG,           /* 电压暂降: Vrms < 0.80pu */
    FAULT_VOLTAGE_SWELL,         /* 电压暂升: Vrms > 1.15pu */
    FAULT_TRANSIENT              /* 暂态突变 (预留) */
} FaultType_t;

/*============================================================================
 *  故障级别: 正常 / 预警 / 紧急
 *============================================================================*/
typedef enum {
    SEVERITY_NORMAL = 0,         /* 正常模式 */
    SEVERITY_WARNING,            /* 预警模式 (6kHz录波) */
    SEVERITY_DANGER              /* 紧急模式 (12kHz录波, 切备用电源) */
} SeverityLevel_t;

/*============================================================================
 *  DMA乒乓缓冲状态
 *============================================================================*/
typedef enum {
    DMA_BUF_0 = 0,               /* 前半缓冲 */
    DMA_BUF_1 = 1                /* 后半缓冲 */
} DmaBufIndex_t;

/*============================================================================
 *  NodeSample_t: 节点采样数据 (1kHz环缓冲, int32×10000保留四位小数)
 *============================================================================*/
typedef struct {
    int16_t pg1;         int16_t pg2;         int16_t pg3;
    int16_t qg1;         int16_t qg2;         int16_t qg3;
    int16_t vmag1;       int16_t vmag2;       int16_t vmag3;
    int16_t vmag4;       int16_t vmag5;       int16_t vmag6;
    int16_t vmag7;       int16_t vmag8;       int16_t vmag9;
    int16_t vangle1;     int16_t vangle2;     int16_t vangle3;
    int16_t vangle4;     int16_t vangle5;     int16_t vangle6;
    int16_t vangle7;     int16_t vangle8;     int16_t vangle9;
    uint32_t timestamp;
} NodeSample_t;

typedef struct {
    NodeSample_t buffer[NODE_BUFFER_SIZE]; 
    uint16_t write_index;        /* 当前写入位置 */
    uint16_t cycle_count;        /* 累计采样点计数 */
} NodeBuffer_t;

typedef struct {
    uint32_t fault_time;         /* 故障时间戳 */
    uint32_t flash_off;          /* Flash快照偏移 */
    uint16_t point_count;        /* 实际故障点数 */
    uint8_t  severity;
    uint8_t  fault_type;
    uint8_t  valid;
} PendingFault_t;

/*============================================================================
 *  MidFreqWindow_t: 中频故障检测滑动窗口 (10ms, 20点)
 *  每次写入一个新点, 窗口满时计算 RMS/dv_dt/Peak
 *============================================================================*/
typedef struct {
    float voltage_mag[MID_FREQ_WINDOW_SAMPLES]; /* 电压幅值滑动窗口 */
    uint16_t index;             /* 窗口写入索引 (0~19) */
    float rms;                  /* 窗口RMS (最近一次计算) */
    float dv_dt;                /* 最大相邻差分斜率 (最近一次计算) */
    uint16_t dv_dt_index;       /* 最大差分斜率发生的窗口内索引 */
    float peak;                 /* 窗口峰峰值 (最近一次计算) */
    float thd;                  /* THD (预留) */
} MidFreqWindow_t;

/*============================================================================
 *  WaveBuffer_t: 波形DMA乒乓缓冲 + Flash管理
 *  ADC → DMA → dma_buf[2]乒乓 → 半满中断 → CPU写Flash
 *============================================================================*/
typedef struct {
    int16_t dma_buf[2][DMA_PINGPONG_SIZE]; /* DMA乒乓缓冲 (2×64×2B=256B) */
    
    uint32_t flash_offset;        /* Flash写入偏移 (环形) */
    uint32_t flash_offset_start; /* 本次录波起始偏移 */
    uint32_t total_recorded;     /* 已录制总点数 */
    uint32_t max_samples;       /* 本次录制目标点数 */
    uint32_t record_start_time; /* 录制起始时间戳 */
    uint32_t fault_detect_time; /* 故障检测时间 (2kHz采样时的故障点时间) */
    float    fault_rms;         /* 故障RMS值, 录完时用于保存记录 */
    uint8_t  is_recording;      /* 录制中标志 */
    SeverityLevel_t severity;    /* 当前故障级别 */
    uint16_t sample_rate;       /* 当前采样率 (12000Hz) */
} WaveBuffer_t; 

/*============================================================================
 *  FaultRecord_t: 故障记录 (每次录波完成保存一条, 最多8条)
 *  记录Flash偏移和点数, 供 wave_retrieve_by_index 按指令读取
 *============================================================================*/
typedef struct {
    uint32_t timestamp;         /* 故障检测时间 (2kHz采样时的故障点时间) */
    FaultType_t fault_type;     /* 故障类型 */
    SeverityLevel_t severity;   /* 故障级别 */
    float fault_value;          /* 故障值 (RMS) */
    uint32_t flash_offset;       /* 对应波形Flash起始偏移 */
    uint32_t sample_count;      /* 波形采样点数 */
    uint16_t sample_rate;       /* 波形采样率 (12000Hz) */
    uint8_t  valid;             /* 记录有效标志 (1=有效) */
    uint8_t  node_index;        /* 故障节点号 (0~9) */
} FaultRecord_t; 

/*============================================================================
 *  NodeUploadHeader_t: 节点状态头 (主控轮询 / 故障触发 统一使用)
 *  正常轮询: 后续紧跟 NORMAL_UPLOAD_POINTS 个 NodeSample_t 原始数据
 *  故障快照: 后续紧跟 FAULT_UPLOAD_POINTS 个 NodeSample_t 原始数据
 *============================================================================*/
typedef struct {
    uint8_t  data_type;         /* DATA_TYPE_NODE_HEAD */
    uint8_t  severity;          /* 故障级别 */
    uint8_t  fault_type;        /* 故障类型 (FAULT_NONE=正常) */
    uint8_t  fault_pending;     /* 是否有待上传故障快照 (1=有) */
    uint8_t  node_index;        /* 节点号 (0~9) */
    uint32_t timestamp;         /* 时间戳 (ms) */
    uint16_t sample_rate;       /* 采样率 (1000Hz) */
    uint16_t total_points;      /* 后续raw数据总点数 */
    float    health_score;      /* 健康度 */
} NodeUploadHeader_t;

/*============================================================================
 *  WaveChunkHeader_t: 波形数据包头 (按指令上传时先发此头, 再发int16数据)
 *============================================================================*/
typedef struct {
    uint8_t  data_type;         /* 数据类型: DATA_TYPE_WAVE */
    uint8_t  node_index;        /* 故障节点编号 (0~9) */
    uint8_t  severity;          /* 故障级别 */
    uint8_t  fault_idx;         /* 故障序号 (该节点的第几次故障) */
    uint32_t fault_timestamp;   /* 故障发生时间戳 (ms) */
    uint32_t sample_rate;       /* 采样率 (12000Hz) */
    uint16_t sample_count;      /* 后续数据总点数 */
} WaveChunkHeader_t;

/*============================================================================
 *  WaveEndMarker_t: 波形传输结束标志
 *  波形差分编码数据全部发送完毕后发送, 告知接收端采样总点数
 *============================================================================*/
typedef struct {
    uint32_t total_samples;
} WaveEndMarker_t;

/*============================================================================
 *                          对外接口函数
 *============================================================================*/

/*--- 初始化 ---*/
void data_monitor_init(void);    /* 全局初始化: 缓冲区清零 */
void fault_snap_flash_init(void); /* 故障快照Flash初始化 */

/*--- 数据输入 (由ISR/task调用) ---*/
void node_sample_process(const ZDataPoint_t *zdata, uint32_t timestamp);

/*--- 故障检测与触发 ---*/
void set_active_node(uint8_t node_idx);        /* 设置当前采样节点 (0~9) */
uint8_t get_active_node(void);                /* 获取当前节点号 */           

FaultType_t detect_mid_freq_fault(MidFreqWindow_t *w); /* 滑动窗口故障检测 */
SeverityLevel_t classify_severity(FaultType_t f, float rms); /* 判定预警/紧急 */
void trigger_fault(FaultType_t f, SeverityLevel_t s, uint32_t fault_time);       /* 触发故障 */
void node_upload_by_timestamp(uint32_t poll_ts); /* 按主控时间戳上传1周期节点数据 */

/*--- 故障快照 ---*/
void upload_fault_snaps(void);                            /* 按指令上传已保存的故障快照 */

/*--- 辅助查询 ---*/
NodeSample_t *get_node_sample(uint16_t index); /* 获取环缓冲中的采样点 */

/*--- 底层发送 ---*/
void send_normal_data(DataType_t type, void *data, uint16_t len);   /* 非阻塞发送 */
void send_waveform_packet(const uint8_t *data, uint16_t len, DataType_t type);       /* 阻塞发送 */

#endif
