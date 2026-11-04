/* 仅显式对照构建启用，按两位开关分别比较两项，不影响默认链接。 */
#include "mic16_feature_fast.h"
unsigned m16_feature_fast_mode,m16_feature_fast_calls[3];
int m16_feature_fast_select(unsigned mode){unsigned i;if(mode>3)return -1;m16_feature_fast_mode=mode;for(i=0;i<3;i++)m16_feature_fast_calls[i]=0;return 0;}
#ifdef M16_FAST_ARM_WRAP
#define ENTRY_BEAM __wrap_mic16_candidate_beam_indexed
#define ENTRY_MONO __wrap_mic16_mono_bands
#define ORIGINAL_BEAM __real_mic16_candidate_beam_indexed
#define ORIGINAL_MONO __real_mic16_mono_bands
#else
#define ENTRY_BEAM mic16_candidate_beam_indexed
#define ENTRY_MONO mic16_mono_bands
#define ORIGINAL_BEAM m16_original_candidate_beam_indexed
#define ORIGINAL_MONO m16_original_mono_bands
#endif
extern int ORIGINAL_BEAM(const float *,unsigned,const double *,float *,unsigned *);
extern int ORIGINAL_MONO(const float *,unsigned,double *,double *,mic16_physical_workspace *);
int ENTRY_BEAM(const float *x,unsigned n,const double *d,float *b,unsigned *g){
 return (m16_feature_fast_mode&1)?mic16_candidate_beam_phase_fast(x,n,d,b,g):ORIGINAL_BEAM(x,n,d,b,g);
}
int ENTRY_MONO(const float *x,unsigned n,double *b,double *l,mic16_physical_workspace *w){
 return (m16_feature_fast_mode&2)?mic16_mono_bands_fast(x,n,b,l,w):ORIGINAL_MONO(x,n,b,l,w);
}
