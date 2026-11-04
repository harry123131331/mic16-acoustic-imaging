#ifndef MIC16_STATISTICS_FRONT_TASK_H
#define MIC16_STATISTICS_FRONT_TASK_H
#include "mic16_extended.h"
#include "mic16_fft_provider.h"
#include "mic16_fir_provider.h"
#include <stdint.h>
enum {M16_SF_QUIET16=0,M16_SF_OBSERVED16=1,M16_SF_BEAM1=2};
enum {M16_SFT_IDLE=0,M16_SFT_VALIDATE=1,M16_SFT_CLEAR=2,M16_SFT_FIR_SETUP=3,M16_SFT_FIR=4,
 M16_SFT_FIR_STORE=5,M16_SFT_SNAPSHOT=6,M16_SFT_LOAD=7,M16_SFT_COMPRESS=8,M16_SFT_REVERSE=9,
 M16_SFT_FFT=10,M16_SFT_RECOVER=11,M16_SFT_CONJUGATE=12,M16_SFT_SPECTRUM=13,M16_SFT_CONSUME=14,
 M16_SFT_NEXT=15,M16_SFT_MEDIAN=16,M16_SFT_CHANNEL_POWER=17,M16_SFT_COMPLETE=18,M16_SFT_STOPPED=19,M16_SFT_FAILED=20,M16_SFT_DISPATCH=21,M16_SFT_DEVICE=22,M16_SFT_FIR_DISPATCH=23,M16_SFT_FIR_DEVICE=24};
enum {M16_SFT_INVALID=-660,M16_SFT_BUSY=-661,M16_SFT_INPUT=-662,M16_SFT_OUTPUT=-663,M16_SFT_FLOAT_ENV=-664};
typedef struct {
 unsigned magic,phase,mode,samples,channels,length,frames,candidate,resample_index,time;
 unsigned frame,channel,cursor,reverse,fft_length,fp64,last_items,max_items;
 unsigned resampled_values,FP32_frames,FP64_frames,vector_groups,snapshots;
 int tap,high,error;
 uint64_t steps;
 double sum[16],maximum;
 float real32[256] __attribute__((aligned(16)));
 float imaginary32[256] __attribute__((aligned(16)));
 float twiddle_real32[256] __attribute__((aligned(16)));
 float twiddle_imaginary32[256] __attribute__((aligned(16)));
 const float *input;
 mic16_extended_workspace *work;
 const double *twiddle_real,*twiddle_imaginary;
 const m16_fft_provider *provider;
 const m16_fir_provider *fir_provider;
} m16_statistics_front_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 首次零对象；只准备255对FP32旋转因子，不运行整窗计算。task与work分离。 */
int m16_statistics_front_init(m16_statistics_front_task *,mic16_extended_workspace *);
/* 只在空闲终态绑定；只迁移原FP64分支，FP32计算及弱值判定保持。 */
int m16_statistics_front_bind_fft64(m16_statistics_front_task *,const m16_fft_provider *);
/* 迁移完整1/16路FIR；其后STFT、统计归约、候选定义仍保持原逻辑。 */
int m16_statistics_front_bind_fir(m16_statistics_front_task *,const m16_fir_provider *);
/* 三种模式分别生成参考功率、公共统计频谱/通道功率、某候选波束功率。
 * QUIET16输入[8192,16]，OBSERVED16输入[40960,16]，BEAM1输入[40960]。
 * BEAM1允许读取work.physical.beam，其它输入须与work分离；输入至终态保持只读。
 * 参考应先完成，再生成公共频谱；16路重采样临时复用observed，处理期间不能读取旧频谱。
 * 输出只在COMPLETE有效；不产生时间戳或声源判定，不把该节点称持续实时主循环。 */
int m16_statistics_front_begin(m16_statistics_front_task *,unsigned mode,const float *,unsigned values,unsigned candidate);
/* 1继续、0完成、2停止、负数失败。每次为64项验证/复制、最多64次FIR通道乘加、
 * 32个蝶形、32对频谱恢复、一次频带功率或15点中位数。不是实际ARM耗时保证。 */
int m16_statistics_front_step(m16_statistics_front_task *);
int m16_statistics_front_stop(m16_statistics_front_task *);
int m16_statistics_front_releasable(const m16_statistics_front_task *);
#ifdef __cplusplus
}
#endif
#endif
