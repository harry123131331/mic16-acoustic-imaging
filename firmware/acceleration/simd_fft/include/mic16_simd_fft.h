#ifndef MIC16_SIMD_FFT_H
#define MIC16_SIMD_FFT_H
#include "mic16_real_fft.h"
extern unsigned m16_simd_fft_enabled,m16_simd_fft_calls[4];
/* 0保留FP64，1单精度标量语义模型，2显式NEON；只在窗口边界切换。 */
extern unsigned m16_simd_fft_vector_groups,m16_simd_fft_preserve_public;
int m16_simd_fft_select(unsigned);
void m16_simd_fft_clear_tables(void);
void m16_simd_fft_transform(double *,double *,const double *,const double *,unsigned,unsigned);
#endif
