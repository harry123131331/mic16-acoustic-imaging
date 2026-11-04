#ifndef MIC16_EXTENDED_H
#define MIC16_EXTENDED_H
#include "mic16_physical.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    mic16_physical_workspace physical;
    double observed[67*257*16*2];
    double channel_power[67*64],quiet_rows[15*64],quiet_power[64];
    double beam_power[16*67*64],signature[257*32*2];
    double resampled[16778],fft_re[512],fft_im[512],spectrum[257*2];
    float spectral10[67*64*10],summary18[67*18],statistics18[16*20*18];
    float physical88[16*20*88],features106[16*20*106],contrasts24[16*20*24];
    unsigned char peers[256];
} mic16_extended_workspace;
/* 固定FP32归一化输入经FP64的256/625多相滤波重采样，边界补零。n为1～40960，步长1～16，通道小于步长；输出长度ceil(n*256/625)。输入输出分离，返回长度或-1。 */
int mic16_resample_channel(const float *x,unsigned int n,unsigned int stride,unsigned int channel,double *out);
/* 单个512点周期Hann STFT帧，写入ws->spectrum[257,2]；中心在index*256，左侧256点及右侧按合同补零。 */
int mic16_stft512_frame(const double *x,unsigned int n,unsigned int index,mic16_extended_workspace *ws);
int mic16_statistics18(const float *pcm,const float *reference,const double *directions,
    unsigned int candidates,float *out18,mic16_extended_workspace *ws);
/* 完整C16有符号对比度严格使用大于15度的邻居边界；无效或缺失邻居填0。方向使用冻结FP32往返后的FP64值。 */
int mic16_contrast24(const float *x106,const double *model_directions,const unsigned char *valid,
                     float *out24,unsigned char *peers);
/* 固定物理候选及投票生成16×20×130特征，不进行候选搜索；原样复用几何与PCM模块。返回经过FP32往返的模型方向和有效位，剩余槽位填零。 */
int mic16_features130(const float *pcm,const float *reference,const double *physical_directions,
    unsigned int candidates,const double *votes,float *out130,double *model_directions,
    unsigned char *valid,mic16_extended_workspace *ws);
enum { M16_REUSE_COMMON=1u,M16_REUSE_PAIRS=2u,M16_REUSE_BEAMS=4u,M16_REUSE_ALL=7u };
typedef struct {
    unsigned common_frames_reused,pair_normalizations,beam_transforms_reused;
} mic16_feature_reuse_report;
/* 内部批处理入口：beams_ready仅由本次物理波束回调完成后设置。 */
int mic16_statistics_beam_power(const float *,unsigned int,mic16_extended_workspace *);
int mic16_statistics18_reused(const float *,const float *,const double *,unsigned int,
    float *,mic16_extended_workspace *,unsigned int,unsigned int,mic16_feature_reuse_report *);
int mic16_statistics18_pair_cache(const double *,unsigned int,float *,mic16_extended_workspace *,
    mic16_feature_reuse_report *);
/* prepared只在相同输入、相同owner的本次调用有效，不能作为跨窗缓存令牌。 */
int mic16_features130_reused(const float *,const float *,const double *,unsigned int,const double *,
    float *,double *,unsigned char *,mic16_extended_workspace *,const mic16_prepared_spectral *,
    unsigned int,mic16_feature_reuse_report *);
#ifdef __cplusplus
}
#endif
#endif
