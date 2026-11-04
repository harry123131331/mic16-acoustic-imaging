#ifndef MIC16_SPECTRAL_FRONT_H
#define MIC16_SPECTRAL_FRONT_H
#ifdef __cplusplus
extern "C" {
#endif
#define M16_SAMPLES 2048
#define M16_CHANNELS 16
#define M16_FFT 4096
#define M16_FIRST_BIN 32
#define M16_BINS 807
#define M16_PAIRS 32
#define M16_ZONES 24

typedef struct {
    double hann[2048], twiddle_re[2048], twiddle_im[2048];
    double fft_re[4096], fft_im[4096];
    unsigned int initialized;
} mic16_spectral_workspace;

typedef struct {
    /* 实部与虚部交错：spectrum为[频点,逻辑通道,2]，PHAT为[频点,通道对,2]。 */
    double spectrum[807*16*2];
    double mean_channel_power[807];
    double zone_level_db[24];
    float phat[807*32*2];
} mic16_spectral_frame;

typedef struct {
    double level_db[4][24], floor_db[24];
    unsigned int count, finalized;
} mic16_spectral_reference;

/* 调用方拥有DDR存储，无堆或可变全局缓冲区；本模块不是HLS RTL。PCM为有限FP32[-1,1]，逻辑MIC0～15，样本优先[2048,16]。输入、输出和工作区必须互不重叠；FP64均值、Hann与FFT遵循冻结Python前端。 */
void mic16_spectral_init(mic16_spectral_workspace *workspace);
int mic16_spectral_process(const float *pcm, mic16_spectral_frame *out,
                           mic16_spectral_workspace *workspace);
/* 归一化已有FP64频谱，并计算功率与分区电平。每个实/虚分量必须有限且位于[-4096,4096]这一保守界内；拒绝时不改原帧。 */
int mic16_spectral_observe(mic16_spectral_frame *frame);
void mic16_reference_init(mic16_spectral_reference *reference);
int mic16_reference_add(mic16_spectral_reference *reference, const double *levels);
int mic16_reference_finalize(mic16_spectral_reference *reference);
int mic16_spectral_quality(const double *levels, const mic16_spectral_reference *reference, float *quality48);
/* 兼容旧复数PHAT字：实部为有符号26位[25:0]，虚部为[51:26]，高12位必须为零。仅此解码不能证明PL数值等价。 */
int mic16_decode_q24_complex(unsigned long long packed, float *real_imag);
#ifdef __cplusplus
}
#endif
#endif
