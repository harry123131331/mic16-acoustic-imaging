#ifndef MIC16_STFT_BATCH_H
#define MIC16_STFT_BATCH_H
#include "mic16_beam_index.h"
/* 仅内部同步借用：x必须为本owner的resampled。回调不得改写输入、重入或让DMA写此区；
 * 回调返回前消费spectrum，不保留指针。检查结果不跨函数调用/重采样/窗保存。
 * 原公开逐帧API仍完整检查；这里只用于已经独占工作区的统计特征调用链。 */
typedef int (*mic16_stft_consumer)(unsigned,const double *,void *);
int mic16_stft512_visit(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
int mic16_statistics18_batched(const float *,const float *,const double *,unsigned,float *,
 mic16_extended_workspace *,unsigned,unsigned,mic16_feature_reuse_report *);
int mic16_statistics_beam_power_batched(const float *,unsigned,mic16_extended_workspace *);
int mic16_features130_batched(const float *,const float *,const double *,unsigned,const double *,float *,double *,
 unsigned char *,mic16_extended_workspace *,const mic16_prepared_spectral *,unsigned,mic16_feature_reuse_report *);
int mic16_pipeline_observe_batched_timed(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *,
 const mic16_profile_clock *,mic16_pipeline_profile *);
int mic16_pipeline_observe_batched(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);
#endif
