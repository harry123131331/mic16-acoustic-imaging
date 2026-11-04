/* 只有mode3启用FFT展开；其余三路保留本轮已验证的原函数。 */
#include "mic16_cooperate.h"
#include "mic16_fft_fast.h"
unsigned m16_fft_fast_mode,m16_fft_fast_calls[3];
int m16_fft_fast_select(unsigned mode){unsigned i;static const unsigned old[4]={0,1,3,3};if(mode>3)return -1;
 m16_fft_fast_mode=mode;for(i=0;i<3;i++)m16_fft_fast_calls[i]=0;return m16_feature_fast_select(old[mode]);}
#ifdef M16_FAST_ARM_WRAP
#define FRONT __wrap_mic16_spectral_process
#define STFT __wrap_mic16_stft512_visit
#define MONO __wrap_mic16_mono_bands_fast
#define OLD_FRONT __real_mic16_spectral_process
#define OLD_STFT __real_mic16_stft512_visit
#define OLD_MONO __real_mic16_mono_bands_fast
#else
#define FRONT mic16_spectral_process
#define STFT mic16_stft512_visit
#define MONO mic16_mono_bands_fast_host
#define OLD_FRONT m16_original_spectral_process
#define OLD_STFT m16_original_stft512_visit
#define OLD_MONO mic16_mono_bands_fast
#endif
extern int OLD_FRONT(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
extern int OLD_STFT(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
extern int OLD_MONO(const float *,unsigned,double *,double *,mic16_physical_workspace *);
int FRONT(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){return m16_fft_fast_mode==3?mic16_spectral_process_fft(x,o,w):OLD_FRONT(x,o,w);}
int STFT(const double *x,unsigned n,unsigned f,mic16_extended_workspace *w,mic16_stft_consumer c,void *ctx){return m16_fft_fast_mode==3?mic16_stft512_visit_fft(x,n,f,w,c,ctx):OLD_STFT(x,n,f,w,c,ctx);}
int MONO(const float *x,unsigned n,double *b,double *l,mic16_physical_workspace *w){return m16_fft_fast_mode==3?mic16_mono_bands_fft(x,n,b,l,w):OLD_MONO(x,n,b,l,w);}
