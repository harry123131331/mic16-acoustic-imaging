#ifndef MIC16_FEATURE_FAST_H
#define MIC16_FEATURE_FAST_H
#include "mic16_physical.h"
/* 仅在窗口之间设置；0保留原实现，1波束，2频带，3两项。原发布入口不引用本模块。 */
extern unsigned m16_feature_fast_mode,m16_feature_fast_calls[3];
int m16_feature_fast_select(unsigned);
int mic16_candidate_beam_phase_fast(const float *,unsigned,const double *,float *,unsigned *);
int mic16_mono_bands_fast(const float *,unsigned,double *,double *,mic16_physical_workspace *);
#endif
