#ifndef MIC16_FEATURE_HOTSPOTS_H
#define MIC16_FEATURE_HOTSPOTS_H
#include <stdint.h>
/* 只用于诊断构建；编号顺序是二进制计时报告的合同。 */
enum {
 M16_HOT_FEATURES,M16_HOT_PHYSICAL,M16_HOT_BEAMS,M16_HOT_MONO,
 M16_HOT_BEAM_STATS,M16_HOT_RESPONSE,M16_HOT_LOCAL,M16_HOT_STATS,
 M16_HOT_OBSERVED,M16_HOT_REFERENCE,M16_HOT_CHANNEL_POWER,M16_HOT_PAIRS,
 M16_HOT_SIGNATURES,M16_HOT_NORMALIZE,M16_HOT_PROJECT,M16_HOT_POWER_LOGS,
 M16_HOT_COMPRESS,M16_HOT_REDUCE,M16_HOT_LAYOUT,M16_HOT_RESAMPLE,
 M16_HOT_STFT,M16_HOT_STFT_VALIDATE,M16_HOT_STFT_FFT,M16_HOT_COUNT
};
typedef struct {uint64_t inclusive_ticks,exclusive_ticks,calls;} mic16_hotspot_item;
typedef struct {
 uint64_t reads,last_tick;
 unsigned errors,depth,max_depth,enabled;
 mic16_hotspot_item items[M16_HOT_COUNT];
} mic16_hotspot_report;
typedef uint64_t (*mic16_hotspot_clock)(void *);
void mic16_hotspot_reset(mic16_hotspot_clock,void *);
void mic16_hotspot_begin(unsigned);
void mic16_hotspot_end(unsigned);
int mic16_hotspot_finish(mic16_hotspot_report *);
#endif
