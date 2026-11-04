#ifndef MIC16_BEAM_NEON_H
#define MIC16_BEAM_NEON_H
#include "mic16_feature_fast.h"
/* 0原路径，1通道布局+原FP64插值，2通道布局+四采样点FP32 NEON。
 * begin/end包围一个只读输入窗口；缺少活动窗时明确退回原函数。 */
extern unsigned m16_beam_neon_mode,m16_beam_neon_calls[6];
int m16_beam_neon_select(unsigned mode);
void m16_beam_neon_begin(void);
void m16_beam_neon_end(void);
int m16_beam_neon(const float *,unsigned,const double *,float *,unsigned *);
#endif
