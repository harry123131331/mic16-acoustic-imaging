#ifndef MIC16_STATISTICS_NEON_H
#define MIC16_STATISTICS_NEON_H
#include "mic16_extended.h"
extern unsigned m16_statistics_neon_mode,m16_statistics_neon_calls[4];
int m16_statistics_neon_select(unsigned mode);
int m16_statistics_neon(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
#endif
