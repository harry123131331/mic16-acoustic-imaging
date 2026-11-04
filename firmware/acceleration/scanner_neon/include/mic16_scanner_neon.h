#ifndef MIC16_SCANNER_NEON_H
#define MIC16_SCANNER_NEON_H
#include "mic16_scanner_hls.h"
extern unsigned m16_scanner_neon_mode,m16_scanner_neon_calls[6];
int m16_scanner_neon_select(unsigned mode);
int m16_scanner_refine_neon(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
int m16_scanner_neon_probe(const float *,unsigned,unsigned,float *);
#endif
