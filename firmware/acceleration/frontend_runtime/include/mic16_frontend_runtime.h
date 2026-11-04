#ifndef MIC16_FRONTEND_RUNTIME_H
#define MIC16_FRONTEND_RUNTIME_H
#include "mic16_frontend_bridge.h"
#include "mic16_scanner_hls.h"
#define M16_FRONT_ARENA_WORDS 80944u
enum {M16_FRONT_INVALID=-1,M16_FRONT_BUSY=-2,M16_FRONT_TIMEOUT=-3,M16_FRONT_GUARD=-4,M16_FRONT_CLOCK=-5,M16_FRONT_RANGE=-6};
typedef struct {
 uint64_t before[8],words[M16_FRONT_ARENA_WORDS],after[8];
 m16_scanner_io io;unsigned initialized,quarantined;
} m16_front_runtime;
typedef struct {
 uint64_t packing_ticks,device_ticks,decoding_ticks,total_ticks;
 unsigned completed_frames,last_control,quarantined,input_scale;
} m16_front_report;
/* 输入是归一化逻辑通道PCM；31位量化只供FP32 FFT使用，原固定点PHAT不读取。
 * +1无法放入有符号Q31，启动之前返回RANGE，由上层显式退回原CPU。 */
int m16_front_runtime_init(m16_front_runtime *,const m16_scanner_io *);
int m16_front_runtime_frame(m16_front_runtime *,const float *,mic16_spectral_frame *,m16_front_report *);
int m16_front_runtime_platform_init(m16_front_runtime *);
extern unsigned m16_front_hardware_enabled;
int m16_front_hardware_select(unsigned);
#endif
