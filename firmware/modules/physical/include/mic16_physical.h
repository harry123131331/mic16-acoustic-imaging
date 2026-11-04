#ifndef MIC16_PHYSICAL_H
#define MIC16_PHYSICAL_H
#include "mic16_spectral_front.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 统计阶段在物理响应完成后借用PHAT存储；不跨窗保留，不增加主工作区大小。 */
typedef struct {
    double pairs[4*256*32*2];
    double signatures[16*257*32*2];
    float summaries[16*67*18];
} mic16_statistics_reuse_storage;
typedef struct {
    mic16_spectral_workspace spectral;
    mic16_spectral_frame frame;
    mic16_spectral_reference reference;
    union {
        float phat[20*807*32*2];
        mic16_statistics_reuse_storage statistics_cache;
    };
    double levels[20*24],powers[807],scratch[2048];
    float beam[40960],quiet_beam[8192],signature[32*34*2];
    double bands[20*32],beam_level[20],quiet_level[4],errors[480];
} mic16_physical_workspace;

/* 根据冻结几何计算FP32到达时间及延迟，不裁剪方向。 */
int mic16_candidate_geometry(const double *direction,float *arrivals,float *lags);
/* 归一化FP32 PCM为[N,16]，有限且在[-1,1]，N为8192或40960，缓冲区分离。输出beam[N]为FP32，公共因果延迟10点，返回左侧保护长度。 */
int mic16_candidate_beam(const float *pcm,unsigned int samples,const double *direction,float *beam,unsigned int *guard);
/* FP32波束为[帧数,2048]，帧数4或20；使用对称Hann窗和4096点FFT。bands[帧数,32]及均方电平使用FP64 dB，不是SPL。调用方提供已初始化且互不重叠的缓冲区。 */
int mic16_mono_bands(const float *beam,unsigned int frames,double *bands,double *levels,mic16_physical_workspace *work);
/* PCM[40960,16]、独立参考[8192,16]、FP64方向[C,2]及原始投票[480,9]生成FP32输出[C,20,88]，C为1～16。不含标签、源数、类别门控、网络评分或DMA。调用方拥有互不重叠的DDR存储，不申请堆。 */
int mic16_physical_features(const float *pcm,const float *reference,const double *directions,
    unsigned int candidates,const double *votes,float *out,mic16_physical_workspace *work);
/* 只描述本次调用已经准备好的数据。调用方须保持输入不变；统计复用后失效。 */
typedef struct {
    const float *pcm,*reference;
    mic16_physical_workspace *owner;
} mic16_prepared_spectral;
typedef int (*mic16_candidate_beam_sink)(void *,unsigned int,const float *,unsigned int);
int mic16_physical_features_reused(const float *,const float *,const double *,unsigned int,
    const double *,float *,mic16_physical_workspace *,const mic16_prepared_spectral *,
    mic16_candidate_beam_sink,void *);
#ifdef __cplusplus
}
#endif
#endif
