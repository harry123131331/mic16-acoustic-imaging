#ifndef MIC16_STATISTICS_MEASUREMENT_H
#define MIC16_STATISTICS_MEASUREMENT_H
#include "mic16_statistics_tile_dispatch.h"
#include <stddef.h>
/* 仅诊断记录，不参与检测。DDR副本用于独立核对串口逐窗数据。 */
typedef struct {
    unsigned rep,fixture,mode,candidates;
    m16_tile_batch_report batch;
    unsigned reserved;
    m16_tile_runtime_report device;
    unsigned index_errors,finite_errors,migration_errors,reserved2;
    double migration[5];
} m16_statistics_measurement_run;
typedef struct {
    unsigned signature,version,stage,completed;
    int init_status;
    unsigned errors;
    m16_statistics_measurement_run runs[20];
} m16_statistics_measurement;
#endif
