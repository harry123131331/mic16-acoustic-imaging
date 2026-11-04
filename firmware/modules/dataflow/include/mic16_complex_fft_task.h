#ifndef MIC16_COMPLEX_FFT_TASK_H
#define MIC16_COMPLEX_FFT_TASK_H
#include <stdint.h>
#include "mic16_fft_provider.h"
enum {M16_CF_IDLE=0,M16_CF_VALIDATE=1,M16_CF_REVERSE=2,M16_CF_BUTTERFLIES=3,M16_CF_SCALE=4,
 M16_CF_CHECK=5,M16_CF_COMPLETE=6,M16_CF_STOPPED=7,M16_CF_FAILED=8,M16_CF_DISPATCH=9,M16_CF_DEVICE=10};
enum {M16_CF_INVALID=-760,M16_CF_BUSY=-761,M16_CF_INPUT=-762,M16_CF_OUTPUT=-763,M16_CF_FLOAT_ENV=-764};
typedef struct {
 unsigned magic,phase,n,capacity,table_length,inverse,cursor,reverse,length,last_items,max_items;
 int error;
 uint64_t steps;
 double *real,*imaginary;
 const double *twiddle_real,*twiddle_imaginary;
 const m16_fft_provider *provider;
} m16_complex_fft_task;
#ifdef __cplusplus
extern "C" {
#endif
/* 原复数FP64 FFT的可恢复实现，支持512/4096及对应512/4096旋转表。
 * 双数组/表/task互不重叠；旋转表由常驻初始化准备，不在每次begin重建。 */
int m16_complex_fft_init(m16_complex_fft_task *,double *,double *,unsigned capacity,const double *,const double *,unsigned table_length);
/* 只在终态且未持有设备时绑定一次。原CPU路径作为未绑定参考保留。 */
int m16_complex_fft_bind(m16_complex_fft_task *,const m16_fft_provider *);
int m16_complex_fft_begin(m16_complex_fft_task *,unsigned length,unsigned inverse);
/* 每步64项校验/交换/缩放或64个蝶形，原base/k顺序及逆变换逐项除法保持。 */
int m16_complex_fft_step(m16_complex_fft_task *);
int m16_complex_fft_stop(m16_complex_fft_task *);
int m16_complex_fft_releasable(const m16_complex_fft_task *);
#ifdef __cplusplus
}
#endif
#endif
