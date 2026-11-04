#ifndef MIC16_SCANNER_ACCUMULATORS_H
#define MIC16_SCANNER_ACCUMULATORS_H
#include "mic16_scanner_hls.h"
/* 显式可选分支；输入、工作区、保护带与回退合同沿用原精算器。 */
int m16_scanner_refine_accumulators(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
#endif
