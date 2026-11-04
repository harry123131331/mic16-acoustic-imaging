#ifndef MIC16_ENH_SPECTRUM_TASK_H
#define MIC16_ENH_SPECTRUM_TASK_H
#include "mic16_extended.h"
#include "mic16_complex_fft_task.h"
#include "mic16_resample_task.h"
enum {M16_ES_OBSERVED16=0,M16_ES_QUIET16=1,M16_ES_BEAM64=2};
enum {M16_ES_IDLE=0,M16_ES_CLEAR=1,M16_ES_RESAMPLE=2,M16_ES_LOAD=3,M16_ES_FFT=4,
 M16_ES_SPECTRUM=5,M16_ES_CONSUME=6,M16_ES_NEXT=7,M16_ES_POWER=8,M16_ES_MEDIAN=9,
 M16_ES_COMPLETE=10,M16_ES_STOPPED=11,M16_ES_FAILED=12};
enum {M16_ES_INVALID=-860,M16_ES_BUSY=-861,M16_ES_INPUT=-862,M16_ES_OUTPUT=-863,M16_ES_FLOAT_ENV=-864};
typedef struct {
 unsigned magic,phase,mode,candidate,channel,frame,cursor,band,length,frames,last_items,max_items,transforms;
 int error;
 uint64_t steps;
 const float *input32;
 const double *input64;
 mic16_extended_workspace *work;
 m16_resample_task resample;
 m16_complex_fft_task fft;
} m16_enh_spectrum_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 增强分支原FP64复数512 FFT；不能用定位特征的混合精度FFT代替。只借用既有extended工作区。 */
int m16_enh_spectrum_init(m16_enh_spectrum_task *,mic16_extended_workspace *);
/* OBSERVED16输入40960×16，QUIET16输入8192×16，BEAM64为原加权波束40960个F64。
 * 输入与task及整个work分离。三类输出分别是observed/channel_power、quiet_power、某候选beam_power。
 * 工作区由上层独占，输出仅COMPLETE有效，不与检测任务同时借用。 */
int m16_enh_spectrum_begin32(m16_enh_spectrum_task *,unsigned mode,const float *,unsigned values);
int m16_enh_spectrum_begin64(m16_enh_spectrum_task *,const double *,unsigned candidate);
/* 每步至多64项装载/清零、32个复数频谱、64通道功率求和或15项中位数；子节点保持原归约顺序。 */
int m16_enh_spectrum_step(m16_enh_spectrum_task *);
int m16_enh_spectrum_stop(m16_enh_spectrum_task *);
int m16_enh_spectrum_releasable(const m16_enh_spectrum_task *);
#ifdef __cplusplus
}
#endif
#endif
