#ifndef MIC16_CHANNEL_WEIGHTS_TASK_H
#define MIC16_CHANNEL_WEIGHTS_TASK_H
#include "mic16_enhancement.h"
#include "mic16_complex_fft_task.h"
enum {M16_CW_IDLE=0,M16_CW_VALIDATE=1,M16_CW_MEAN=2,M16_CW_CENTER1=3,M16_CW_CENTER2=4,
 M16_CW_FFT=5,M16_CW_SPECTRUM=6,M16_CW_PAIRS=7,M16_CW_CONJUGATE=8,M16_CW_IFFT=9,
 M16_CW_RELIABILITY=10,M16_CW_ACCUMULATE=11,M16_CW_NORMALIZE=12,
 M16_CW_COMPLETE=13,M16_CW_STOPPED=14,M16_CW_FAILED=15};
enum {M16_CW_INVALID=-780,M16_CW_BUSY=-781,M16_CW_INPUT=-782,M16_CW_OUTPUT=-783,M16_CW_FLOAT_ENV=-784};
typedef struct {
 unsigned magic,phase,frame,channel,pair,cursor,last_items,max_items;
 int error;
 uint64_t steps,forward_transforms,inverse_transforms;
 float mean,mean2,reliability[32],acc[16],counts[16];
 const float *pcm;
 m16_enh_workspace *work;
 m16_complex_fft_task fft;
} m16_channel_weights_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象；work在DDR，初始化时一次准备2048对原FP64旋转因子。 */
int m16_channel_weights_init(m16_channel_weights_task *,m16_enh_workspace *);
/* 固定40960×16 PCM，输出work.weights[20,16]。保持原双次FP32去均值、
 * 4096点FP64 GCC-PHAT、41点可靠度及通道归约，不从检测谱替代计算。 */
int m16_channel_weights_begin(m16_channel_weights_task *,const float *,unsigned);
/* 64项输入/FFT片段、32个交叉频点，或一对41点可靠度/一帧32对归约。
 * 不调用原整窗channel_weights，输出只在COMPLETE有效。 */
int m16_channel_weights_step(m16_channel_weights_task *);
int m16_channel_weights_stop(m16_channel_weights_task *);
int m16_channel_weights_releasable(const m16_channel_weights_task *);
#ifdef __cplusplus
}
#endif
#endif
