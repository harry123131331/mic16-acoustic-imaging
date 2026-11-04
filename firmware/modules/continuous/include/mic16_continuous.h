#ifndef MIC16_CONTINUOUS_H
#define MIC16_CONTINUOUS_H
#include <stdint.h>
#define M16_CONT_N 40960u
#define M16_CONT_MAX_SOURCES 8u
#define M16_CONT_MAX_AUDIO_IDS 24u
#define M16_CONT_RAW 45680u
#define M16_CONT_RESAMPLED 18712u
#define M16_CONT_OLA 19456u
#define M16_CONT_OUTPUT 16778u
typedef struct {
    unsigned count;
    uint32_t ids[8];
    double directions[8][2];
    float weights[20*16],masks[8][67*64];
} m16_cont_prediction;
typedef struct {
    uint32_t epoch,window,source_id,ordinal,stream_count,current_confirmed,final;
    uint64_t output_start,output_end,input_start,input_end,available_input;
} m16_cont_info;
/* source_id=0表示MIC0参考，不是检出源。数组同步借用；push/finish返回非负前，先前回调均为临时输出，失败则整段/整事务作废。stream_count统计三窗支持区的音频ID数，不是当前源数。 */
typedef int (*m16_cont_sink)(void *,const m16_cont_info *,const double *wdas,
                            const double *enhanced,unsigned samples);
typedef struct {
    float pcm[3][40960*16];
    m16_cont_prediction prediction[3];
    uint32_t epoch,windows,emitted,closed,failed;
    double raw[45680],resampled[18712],signal[19456],norm[19456];
    double fft_re[512],fft_im[512],twiddle_re[256],twiddle_im[256];
    double wdas[16778],enhanced[16778];
} m16_cont_workspace;
/* 工作区放DDR/BSS而不是任务栈。无堆、设备或跟踪器。输入为连续归一化FP32，first_sample=windows*40960；无效、缺口或接收端失败会锁存失败，必须新时段复位。前瞻一窗，最多保留三窗；finish输出最后一窗。 */
int m16_cont_init(m16_cont_workspace *,uint32_t epoch);
int m16_cont_push(m16_cont_workspace *,const float *pcm,const m16_cont_prediction *,
                  uint64_t first_sample,m16_cont_sink,void *context);
int m16_cont_finish(m16_cont_workspace *,m16_cont_sink,void *context);
#endif
