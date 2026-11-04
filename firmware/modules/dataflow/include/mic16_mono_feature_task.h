#ifndef MIC16_MONO_FEATURE_TASK_H
#define MIC16_MONO_FEATURE_TASK_H
#include "mic16_shared_spectrum_task.h"
#include "mic16_spectral_front.h"
#include "mic16_fft_provider.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {M16_MB_IDLE=0,M16_MB_VALIDATE=1,M16_MB_ACQUIRE=2,M16_MB_LOAD=3,M16_MB_SUM=4,
 M16_MB_REDUCE=5,M16_MB_WINDOW=6,M16_MB_PACK=7,M16_MB_SUBMIT=8,M16_MB_DEVICE=9,
 M16_MB_READ=10,M16_MB_POWERS=11,M16_MB_BANDS=12,M16_MB_ZERO=13,M16_MB_COMPRESS=14,
 M16_MB_REVERSE=15,M16_MB_BUTTERFLIES=16,M16_MB_RECOVER=17,M16_MB_CONJUGATE=18,
 M16_MB_CHECK_FFT=19,M16_MB_RETIRE=20,M16_MB_FALLBACK=21,M16_MB_STOPPING=22,
 M16_MB_COMPLETE=23,M16_MB_STOPPED=24,M16_MB_FAILED=25,M16_MB_FP64_DISPATCH=26,M16_MB_FP64_DEVICE=27};
enum {M16_MB_INVALID=-560,M16_MB_BUSY=-561,M16_MB_INPUT=-562,M16_MB_FLOAT_ENV=-563,
 M16_MB_DEVICE_ERROR=-564,M16_MB_OUTPUT=-565};
typedef struct {
 unsigned magic,phase,frames,levels_only,base,active,frame,cursor,block,reduce_n,band;
 unsigned fft_reverse,fft_len,owned,cpu_fallback,range_fallback,stop_requested,last_items,max_items;
 int error;
 uint64_t epoch,first_job,job,steps,completed_batches;
 double mean,maximum,mean_leaf[16],energy_leaf[16],mean_lane[8],energy_lane[8];
 const float *beam;
 double *bands,*levels,*scratch,*powers;
 mic16_spectral_workspace *work;
 m16_shared_spectrum_task *device;
 const m16_fft_provider *provider;
} m16_mono_feature_task;
/* 复用已初始化的FP64窗/旋转表；scratch[2048]、powers[807]与FFT工作区互不重叠。
 * beam可来自physical_workspace.beam/quiet_beam，但不能与上述实际写入区重叠。
 * 节点独占工作区，不能与公共定位频谱或其它单通道任务并行使用同一工作区。 */
int m16_mono_feature_init(m16_mono_feature_task *,m16_shared_spectrum_task *,mic16_spectral_workspace *,double *scratch,double *powers);
/* 只替换原精确回退的复FFT主体；普通FP32核及原回退判定保持。 */
int m16_mono_feature_bind_fft64(m16_mono_feature_task *,const m16_fft_provider *);
/* 4或20帧；levels_only用于原独立参考的能量路径，bands须为NULL且不启动FFT。
 * 普通波束保留原16+4打包以及极弱值整候选FP64回退；所有部分结果在COMPLETE前无效。
 * 工作量/标签均非物理采样时间。first_job要为最多两批提供新的本地事务序号。 */
int m16_mono_feature_begin(m16_mono_feature_task *,const float *,unsigned beam_values,
 unsigned frames,unsigned levels_only,double *bands,unsigned band_values,double *levels,unsigned level_values,
 uint64_t epoch,uint64_t first_job);
/* 1继续，0有效完成，2停止，负数错误；每次最多64项/蝶形或一个不超过26项的频带归约。 */
int m16_mono_feature_step(m16_mono_feature_task *);
int m16_mono_feature_stop(m16_mono_feature_task *);
int m16_mono_feature_releasable(const m16_mono_feature_task *);
#ifdef __cplusplus
}
#endif
#endif
