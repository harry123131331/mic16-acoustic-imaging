#ifndef MIC16_ENHANCEMENT_H
#define MIC16_ENHANCEMENT_H
#include "mic16_extended.h"
#include "mic16_frozen.h"
#ifdef __cplusplus
extern "C" {
#endif
#define M16_ENH_T 67
#define M16_ENH_BANDS 64
#define M16_ENH_INPUTS 10
#define M16_ENH_HIDDEN 24
#define M16_ENH_SAMPLES 16778
/* CPU参考使用FP32激活并保留FP16输入往返。三个因果深度可分离残差块，膨胀率1/2/4。不是新增网络、HLS核或持续流，也不是声学评分。 */
typedef struct {
    float h[2][67*64*24], depth[67*64*24];
} m16_enh_network_workspace;
int m16_enh_mask(const float *features10, float *mask,
                 m16_enh_network_workspace *workspace);
float m16_enh_half_roundtrip(float value);
const char *m16_enh_checkpoint_sha256(void);

/* 工作区放DDR/BSS，禁止放Cortex-A9任务栈。冻结检测器完成后才能借用扩展工作区。无堆、设备、标定假设、可变全局状态或自动增益。 */
typedef struct {
    m16_enh_network_workspace network;
    double gcc_spectra[16*2049*2], fft_re[4096], fft_im[4096];
    double twiddle_re[2048], twiddle_im[2048];
    double beam[40960], spectrum[67*257*2];
    double signal[17408], norm[17408];
    double wdas[16778], enhanced[16778];
    float weights[20*16], mask[67*64];
} m16_enh_workspace;

/* 缓冲区同步借用，下一个声源会复用。接收端返回0前必须复制或完全消费；非零则停止整窗，前面输出只是部分事务，调用方须整窗丢弃。回放时间戳由调用方提供，本层不虚构。 */
typedef int (*m16_enh_sink)(void *context, unsigned int source_id,
    unsigned int candidate_index, const float *features10, const float *mask,
    const double *beam_spectrum, const double *wdas, const double *enhanced,
    unsigned int samples);
/* PCM40960×16和独立安静参考8192×16均为归一化FP32[-1,1]。完整当前C16参与竞争。仅唯一、当前确认、分数≥0.65的轨迹输出；保持轨迹不伪造当前音频。返回发布源数0～8或负错误码，不负责连续重叠相加。 */
int m16_enh_window(const float *pcm, const float *quiet,
    const double *directions, const unsigned char *valid,
    const mic16_cycle_output *tracks, mic16_extended_workspace *shared,
    m16_enh_workspace *workspace, m16_enh_sink sink, void *context);
#ifdef __cplusplus
}
#endif
#endif
