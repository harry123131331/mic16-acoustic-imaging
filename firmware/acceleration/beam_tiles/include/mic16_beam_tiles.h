#ifndef MIC16_BEAM_TILES_H
#define MIC16_BEAM_TILES_H
#include "mic16_feature_fast.h"
extern unsigned m16_beam_tiles_enabled,m16_beam_tiles_calls[3];
int m16_beam_tiles_select(unsigned);
int m16_beam_tiles(const float *,unsigned,const double *,float *,unsigned *);
#endif
