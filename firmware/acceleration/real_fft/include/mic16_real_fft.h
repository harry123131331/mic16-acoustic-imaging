#ifndef MIC16_REAL_FFT_H
#define MIC16_REAL_FFT_H
#include "mic16_cooperate.h"
#include "mic16_fft_fast.h"
extern unsigned m16_real_fft_mode,m16_real_fft_calls[4];
int m16_real_fft_select(unsigned);
/* 输入为实数，长度512或4096；旋转表仍是对应完整长度的原FP64表。
 * 半长复数FFT后恢复正频谱；这改变舍入路径，必须按数值迁移核验。
 * 不裁剪频带、不减少时间帧、不改特征、阈值或模型。 */
void m16_real_fft_transform(double *,double *,const double *,const double *,unsigned,unsigned);
#endif
