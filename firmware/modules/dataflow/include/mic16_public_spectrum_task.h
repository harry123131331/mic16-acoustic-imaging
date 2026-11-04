#ifndef MIC16_PUBLIC_SPECTRUM_TASK_H
#define MIC16_PUBLIC_SPECTRUM_TASK_H
#include "mic16_spectral_front.h"
#include <stdint.h>
#include "mic16_fft_provider.h"
enum {M16_PS_IDLE=0,M16_PS_VALIDATE=1,M16_PS_MEAN=2,M16_PS_WINDOW=3,
 M16_PS_COMPRESS=4,M16_PS_REVERSE=5,M16_PS_BUTTERFLIES=6,M16_PS_RECOVER=7,
 M16_PS_CONJUGATE=8,M16_PS_STORE=9,M16_PS_CHECK_SPECTRUM=10,M16_PS_OBSERVE=11,M16_PS_ZONES=12,
 M16_PS_COMPLETE=13,M16_PS_STOPPED=14,M16_PS_FAILED=15,M16_PS_DISPATCH=16,M16_PS_DEVICE=17};
enum {M16_PS_INVALID=-420,M16_PS_BUSY=-421,M16_PS_INPUT=-422,M16_PS_FLOAT_ENV=-423};
/* 公共定位频谱保留既有FP64实FFT数值路径，仅把执行拆成可恢复工作片段。
 * 单节点持有一个2048×16输入帧、输出帧及工作区的独占租约；不能并行调用
 * 其它使用同一FFT工作区的函数。不持有全局临时数组，不调用旧整窗compute。 */
typedef struct {
 uint32_t magic,phase,channel,cursor,reverse,len,last_items,max_items;
 int error;
 uint64_t epoch,frame,steps;
 double mean;
 const float *pcm;
 mic16_spectral_frame *output;
 mic16_spectral_workspace *work;
 const m16_fft_provider *provider;
} m16_public_spectrum_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 工作区的原FP64 Hann/旋转表须在常驻启动时用mic16_spectral_init准备一次，
 * 不能每次开始定位重新构造。task首次为零对象。 */
int m16_public_spectrum_init(m16_public_spectrum_task *,mic16_spectral_frame *,mic16_spectral_workspace *);
/* 可选共享FP64后端只替换压缩后的2048点复FFT；均值、Hann、恢复和PHAT不变。 */
int m16_public_spectrum_bind(m16_public_spectrum_task *,const m16_fft_provider *);
/* 仅终态且设备无借用时更换输出槽，不重新构造FFT表。 */
int m16_public_spectrum_retarget(m16_public_spectrum_task *,mic16_spectral_frame *);
int m16_public_spectrum_begin(m16_public_spectrum_task *,const float *,unsigned values,uint64_t epoch,uint64_t frame);
/* 每步最多64个标量项/蝶形，或1个频点的16路功率和32对PHAT，或1个频带归约。
 * 1进行中，0整帧有效，2已停止，负数失败；部分输出不得发布。 */
int m16_public_spectrum_step(m16_public_spectrum_task *);
/* 纯PS节点在step之间停止后不再访问借用缓冲；不替调用方停止DMA或其它PL节点。 */
int m16_public_spectrum_stop(m16_public_spectrum_task *);
#ifdef __cplusplus
}
#endif
#endif
