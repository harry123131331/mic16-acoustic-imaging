#ifndef MIC16_APPLICATION_ACCELERATION_H
#define MIC16_APPLICATION_ACCELERATION_H
#ifndef M16_APP_STATISTICS_NEON
#define M16_APP_STATISTICS_NEON 0
#endif
#include "mic16_pipeline.h"
#include <stdint.h>
/* 只对检测调用启用加速；离开检测后恢复原算术，避免改变逐源增强。 */
typedef struct {
    unsigned version,mode,active,initialized,calls,completed,errors;
    int initialization_code,last_code;
    unsigned fpscr,cpacr;
    unsigned fft[3],dataflow[6],simd[4],resample[4],vector_groups;
    unsigned statistics_enabled,statistics[4];
    unsigned shared_calls,shared_completed,shared_batches,shared_errors;
    uint64_t shared_cache_ticks,shared_device_ticks,shared_total_ticks;
} mic16_application_acceleration_report;
extern volatile mic16_application_acceleration_report g_mic16_application_acceleration;
int __wrap_mic16_pipeline_observe_batched_timed(const float *,const float *,
    mic16_pipeline_observation *,mic16_pipeline_workspace *,
    const mic16_profile_clock *,mic16_pipeline_profile *);
#endif
