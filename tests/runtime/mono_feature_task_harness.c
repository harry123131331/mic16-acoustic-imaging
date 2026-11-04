/* 新单通道节点与保留实现的独立对照。设备侧仅注入已知复数，不冒充FFT数值模型。 */
#include "mic16_mono_feature_task.h"
#include "mic16_shared_spectrum_batch.h"
#include "mic16_physical.h"
#include <fenv.h>
#include <string.h>
#include <xmmintrin.h>
extern m16_shared_spectrum_task *ss_task_bridge(void);
extern m16_shared_runtime *ss_runtime_bridge(void);
extern int ss_reset(unsigned),ss_device_input_copy(uint64_t *,unsigned);
extern void ss_complete(unsigned,unsigned);
extern uint64_t ss_metric(unsigned);
extern int m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int m16_quiet_levels_reference(const float *,double *,mic16_physical_workspace *);
unsigned m16_feature_fast_calls[3],m16_fft_fast_calls[3];
static mic16_physical_workspace current,reference;
static m16_mono_feature_task task;
static double actual_bands[640],actual_levels[20],reference_bands[640],reference_levels[20];
static uint64_t reference_arena[M16_SHARED_ARENA_WORDS] __attribute__((aligned(64)));
static uint64_t reference_inputs[2][16384],actual_input[16384];
static unsigned reference_active[2],reference_calls,reference_ready,served,call_base,input_checks,input_errors;
static unsigned test_frames,test_only;
static const float *test_beam;
static int reference_execute(void *unused,uint64_t *arena,unsigned active){
 unsigned i;(void)unused;
 if(reference_calls>=2)return -1;
 reference_active[reference_calls]=active;
 memcpy(reference_inputs[reference_calls],arena,active*8192u);reference_calls++;
 for(i=0;i<active*807u;i++)arena[16384u+i]=(uint64_t)(0x3d800000u+(i&255u))|((uint64_t)(0xbf000000u+(i&127u))<<32);
 return 0;
}
int mf_setup(const float *beam,unsigned frames,unsigned only,unsigned embedded){
 unsigned i;int rc;
 if((frames!=4 && frames!=20) || !beam)return -999;
 rc=ss_reset(0);if(rc)return rc;
 memset(&task,0,sizeof(task));memset(&current,0,sizeof(current));memset(&reference,0,sizeof(reference));
 mic16_spectral_init(&current.spectral);mic16_spectral_init(&reference.spectral);
 reference_calls=reference_ready=served=call_base=input_checks=input_errors=0;test_frames=frames;test_only=only;
 for(i=0;i<640;i++)actual_bands[i]=reference_bands[i]=-777.;
 for(i=0;i<20;i++)actual_levels[i]=reference_levels[i]=-777.;
 if(embedded){memcpy(current.beam,beam,frames*2048u*4u);test_beam=current.beam;}else test_beam=beam;
 rc=m16_mono_feature_init(&task,ss_task_bridge(),&current.spectral,current.scratch,current.powers);if(rc)return rc;
 return m16_mono_feature_begin(&task,test_beam,frames*2048u,frames,only,only?0:actual_bands,only?0:frames*32u,
  actual_levels,frames,12,1);
}
int mf_reference(void){
 int rc;
 if(test_only)rc=m16_quiet_levels_reference(test_beam,reference_levels,&reference);
 else {
  rc=m16_shared_mono_bands(test_beam,test_frames,reference_bands,reference_levels,&reference,
   reference_arena,M16_SHARED_ARENA_WORDS,reference_execute,0);
  if(rc==1)rc=m16_mono_bands_real(test_beam,test_frames,reference_bands,reference_levels,&reference);
 }
 reference_ready=rc==0;return rc;
}
int mf_step(void){return m16_mono_feature_step(&task);}
int mf_stop(void){return m16_mono_feature_stop(&task);}
int mf_releasable(void){return m16_mono_feature_releasable(&task);}
int mf_service(unsigned kind){
 unsigned i,n,relative=served-call_base;
 if(ss_metric(0)!=M16_SS_DEVICE || ss_metric(3)==served)return 0;
 if(ss_metric(3)!=served+1u)return -998;
 n=ss_task_bridge()->active;
 if(ss_device_input_copy(actual_input,n*1024u))return -997;
 if(reference_ready){
  if(relative>=reference_calls || reference_active[relative]!=n)return -996;
  for(i=0;i<n*1024u;i++){if(actual_input[i]!=reference_inputs[relative][i])input_errors++;input_checks++;}
 }
 served++;ss_complete(kind,1);return input_errors?-995:1;
}
int mf_copy(unsigned old,double *bands,double *levels,double *fft){
 mic16_physical_workspace *w=old?&reference:&current;
 if(bands)memcpy(bands,old?reference_bands:actual_bands,test_frames*32u*8u);
 if(levels)memcpy(levels,old?reference_levels:actual_levels,test_frames*8u);
 if(fft){memcpy(fft,w->spectral.fft_re,4096u*8u);memcpy(fft+4096,w->spectral.fft_im,4096u*8u);}
 return 0;
}
uint64_t mf_metric(unsigned i){
 switch(i){case 0:return task.phase;case 1:return task.owned;case 2:return task.cpu_fallback;
 case 3:return task.range_fallback;case 4:return task.max_items;case 5:return task.steps;
 case 6:return task.completed_batches;case 7:return input_checks;case 8:return input_errors;
 case 9:return reference_calls;case 10:return served;case 11:return task.frame;case 12:return task.cursor;
 default:return UINT64_MAX;}
}
int mf_error(void){return task.error;}
int mf_restart(uint64_t epoch,uint64_t job){
 int rc=m16_mono_feature_begin(&task,test_beam,test_frames*2048u,test_frames,test_only,
  test_only?0:actual_bands,test_only?0:test_frames*32u,actual_levels,test_frames,epoch,job);
 if(!rc)call_base=served;
 return rc;
}
void mf_mutate_input(unsigned i,uint32_t bits){
 if(i<test_frames*2048u)memcpy((float *)test_beam+i,&bits,4);
}
void mf_corrupt_table(void){current.spectral.hann[2047]=0./0.;}
void mf_environment(unsigned mode){
 unsigned csr;
 fesetround(mode==1?FE_DOWNWARD:FE_TONEAREST);csr=_mm_getcsr();
 csr&=~0x8040u;if(mode==2)csr|=0x8040u;_mm_setcsr(csr);
}
int mf_bad_begin(unsigned which){
 const float *x=test_beam;double *bands=actual_bands,*levels=actual_levels;
 unsigned frames=test_frames,only=test_only,bn=only?0:frames*32u,ln=frames,values=frames*2048u;
 uint64_t epoch=12,job=3;
 switch(which){
 case 0:x=(const float *)((const unsigned char *)x+1);break;
 case 1:x=(const float *)current.scratch;break;
 case 2:x=(const float *)current.spectral.fft_re;break;
 case 3:x=(const float *)ss_runtime_bridge()->words;break;
 case 4:levels=(double *)test_beam;break;
 case 5:bands=levels;break;
 case 6:values--;break;
 case 7:ln--;break;
 case 8:bn--;break;
 case 9:frames=5;break;
 case 10:epoch=0;break;
 case 11:job=UINT64_MAX;break;
 case 12:only=1;break;
 case 13:levels=current.powers;break;
 default:return -999;
 }
 return m16_mono_feature_begin(&task,x,values,frames,only,bands,bn,levels,ln,epoch,job);
}
