#include "mic16_real_fft.h"
extern int m16_spectral_process_real(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
extern int m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int m16_stft512_visit_real(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
#ifdef M16_REAL_ARM_WRAP
#define FRONT __wrap_mic16_spectral_process_fft
#define MONO __wrap_mic16_mono_bands_fft
#define STFT __wrap_mic16_stft512_visit_fft
#define OLD_FRONT __real_mic16_spectral_process_fft
#define OLD_MONO __real_mic16_mono_bands_fft
#define OLD_STFT __real_mic16_stft512_visit_fft
#else
#define FRONT mic16_spectral_process_fft
#define MONO mic16_mono_bands_fft
#define STFT mic16_stft512_visit_fft
#define OLD_FRONT m16_old_spectral_process_fft
#define OLD_MONO m16_old_mono_bands_fft
#define OLD_STFT m16_old_stft512_visit_fft
#endif
extern int OLD_FRONT(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
extern int OLD_MONO(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int OLD_STFT(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
int FRONT(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){return m16_real_fft_mode>=2?m16_spectral_process_real(x,o,w):OLD_FRONT(x,o,w);}
int MONO(const float *x,unsigned f,double *b,double *l,mic16_physical_workspace *w){return m16_real_fft_mode>=1?m16_mono_bands_real(x,f,b,l,w):OLD_MONO(x,f,b,l,w);}
int STFT(const double *x,unsigned n,unsigned f,mic16_extended_workspace *w,mic16_stft_consumer c,void *ctx){return m16_real_fft_mode>=3?m16_stft512_visit_real(x,n,f,w,c,ctx):OLD_STFT(x,n,f,w,c,ctx);}

#ifdef M16_REAL_ARM_WRAP
#define PHYSICAL __wrap_mic16_physical_features_indexed
#define OLD_PHYSICAL __real_mic16_physical_features_indexed
#else
#define PHYSICAL mic16_physical_features_indexed
#define OLD_PHYSICAL m16_old_physical_features_indexed
#endif
extern int OLD_PHYSICAL(const float *,const float *,const double *,unsigned,const double *,float *,mic16_physical_workspace *,const mic16_prepared_spectral *,mic16_candidate_beam_sink,void *);
extern int m16_physical_features_quiet(const float *,const float *,const double *,unsigned,const double *,float *,mic16_physical_workspace *,const mic16_prepared_spectral *,mic16_candidate_beam_sink,void *);
int PHYSICAL(const float *x,const float *q,const double *d,unsigned n,const double *v,float *o,mic16_physical_workspace *w,const mic16_prepared_spectral *p,mic16_candidate_beam_sink s,void *ctx){
 return m16_real_fft_mode?m16_physical_features_quiet(x,q,d,n,v,o,w,p,s,ctx):OLD_PHYSICAL(x,q,d,n,v,o,w,p,s,ctx);
}
