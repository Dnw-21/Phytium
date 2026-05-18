#ifndef __LORA_FRAME_H
#define __LORA_FRAME_H

#include <stdint.h>

typedef enum {
    DATA_TYPE_STATUS     = 0x01,
    DATA_TYPE_WAVE       = 0x02,
    DATA_TYPE_POWER      = 0x03,
    DATA_TYPE_NODE_RAW   = 0x04,
    DATA_TYPE_FLASH_WAVE = 0x05,
    DATA_TYPE_FAULT_LIST = 0x06
} DataType_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_OVER_VOLTAGE,
    FAULT_UNDER_VOLTAGE,
    FAULT_VOLTAGE_SAG,
    FAULT_VOLTAGE_SWELL,
    FAULT_TRANSIENT
} FaultType_t;

typedef enum {
    SEVERITY_NORMAL = 0,
    SEVERITY_WARNING,
    SEVERITY_DANGER
} SeverityLevel_t;

typedef struct {
    int32_t active_power;
    int32_t reactive_power;
    int32_t voltage_angle;
    int32_t voltage_mag;
} NodeSample_t;

typedef struct {
    uint8_t  data_type;
    uint8_t  severity;
    uint8_t  node_index;
    uint16_t sample_rate;
    float    health_score;
    uint16_t total_points;
} NodeUploadData_t;

typedef struct {
    uint8_t      data_type;
    uint8_t      severity;
    uint32_t     timestamp;
    FaultType_t  fault_type;
    uint8_t      node_index;
    uint16_t     total_points;
    uint16_t     sample_rate;
} FaultUploadHeader_t;

typedef struct {
    uint8_t  data_type;
    uint8_t  severity;
    uint32_t sample_index;
    uint32_t sample_rate;
    uint16_t sample_count;
} WaveChunkHeader_t;

#define NODE_SAMPLE_RATE        2000
#define SAMPLES_PER_CYCLE       40
#define FAULT_UPLOAD_CYCLES     2
#define FAULT_UPLOAD_POINTS     (SAMPLES_PER_CYCLE * FAULT_UPLOAD_CYCLES)

#endif /* __LORA_FRAME_H */