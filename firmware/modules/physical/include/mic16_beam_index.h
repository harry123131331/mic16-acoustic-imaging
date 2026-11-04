#ifndef MIC16_BEAM_INDEX_H
#define MIC16_BEAM_INDEX_H
#include "mic16_pipeline.h"
/* 显式选择索引优化；旧入口、工作区、浮点插值和判定门限均保留。 */
int mic16_candidate_beam_indexed(const float *,unsigned,const double *,float *,unsigned *);
int mic16_physical_features_indexed(const float *,const float *,const double *,unsigned,const double *,
 float *,mic16_physical_workspace *,const mic16_prepared_spectral *,mic16_candidate_beam_sink,void *);
int mic16_features130_indexed(const float *,const float *,const double *,unsigned,const double *,float *,double *,
 unsigned char *,mic16_extended_workspace *,const mic16_prepared_spectral *,unsigned,mic16_feature_reuse_report *);
int mic16_pipeline_observe_indexed_timed(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *,
 const mic16_profile_clock *,mic16_pipeline_profile *);
int mic16_pipeline_observe_indexed(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);
#endif
