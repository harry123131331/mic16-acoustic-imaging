#include "mic16_statistics_front_task.h"
#include "mic16_real_fft.h"
#include "mic16_simd_fft.h"
#include "mic16_stft_batch.h"
#include <math.h>
#include <string.h>
#include <fenv.h>
#include <xmmintrin.h>
/* 仅宿主测试夹具持有双份工作区；生产节点不分配这些对照存储。 */
static mic16_extended_workspace current,reference;
static m16_statistics_front_task task;
static float input[40960*16];
static double fft_probe[512*2+514];
unsigned m16_fft_fast_calls[3];
typedef struct {mic16_extended_workspace *w;unsigned c;} stft_context;
extern int sf_original_observed(unsigned,const double *,void *);
extern int sf_original_quiet(unsigned,const double *,void *);
extern int sf_original_beam_power(unsigned,const double *,void *);
extern int m16_stft512_visit_real(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
static int capture_observed(unsigned t,const double *s,void *p){
 stft_context *v=(stft_context *)p;
 if(t==0 && v->c==15){
  memcpy(fft_probe,v->w->fft_re,512*sizeof(double));
  memcpy(fft_probe+512,v->w->fft_im,512*sizeof(double));
  memcpy(fft_probe+1024,s,514*sizeof(double));
 }
 return sf_original_observed(t,s,p);
}
void sf_reset(void){
 memset(&current,0,sizeof(current));memset(&reference,0,sizeof(reference));memset(&task,0,sizeof(task));
 fesetround(FE_TONEAREST);_mm_setcsr(_mm_getcsr()&~0xe040u);
}
int sf_init(void){return m16_statistics_front_init(&task,&current);}
float *sf_input(void){return input;}
float *sf_embedded(void){return current.physical.beam;}
int sf_begin(unsigned mode,unsigned values,unsigned candidate,unsigned embedded){
 return m16_statistics_front_begin(&task,mode,embedded?current.physical.beam:input,values,candidate);
}
int sf_alias(unsigned which){
 const float *x=which==0?(const float *)&current:(const float *)&task;
 return m16_statistics_front_begin(&task,M16_SF_QUIET16,x,8192*16,0);
}
int sf_step(void){return m16_statistics_front_step(&task);}
int sf_run(unsigned limit){
 unsigned i;int rc=1;for(i=0;i<limit && rc==1;i++)rc=m16_statistics_front_step(&task);return rc;
}
int sf_run_to(unsigned phase,unsigned limit){
 unsigned i;int rc=1;for(i=0;i<limit && rc==1 && task.phase!=phase;i++)rc=m16_statistics_front_step(&task);
 return task.phase==phase?1:rc;
}
int sf_stop(void){return m16_statistics_front_stop(&task);}
int sf_releasable(void){return m16_statistics_front_releasable(&task);}
unsigned long long sf_metric(unsigned n){
 switch(n){
 case 0:return task.phase;case 1:return task.steps;case 2:return task.max_items;
 case 3:return task.resampled_values;case 4:return task.FP32_frames;case 5:return task.FP64_frames;
 case 6:return task.vector_groups;case 7:return task.snapshots;case 8:return sizeof(task);
 case 9:return task.frame;case 10:return task.channel;case 11:return task.cursor;
 case 12:return task.resample_index;case 13:return (unsigned)task.tap;
 default:return 0;
 }
}
unsigned sf_float_mode(unsigned mode){
 unsigned saved=_mm_getcsr();
 if(mode==1)fesetround(FE_DOWNWARD);else if(mode==2)_mm_setcsr(saved|0x8000u);else if(mode==3)_mm_setcsr(saved|0x40u);
 else{fesetround(FE_TONEAREST);_mm_setcsr(saved&~0xe040u);}
 return saved;
}
int sf_reference(unsigned mode,unsigned candidate,unsigned embedded){
 unsigned c,i,z,k,t,channels=mode==M16_SF_BEAM1?1u:16u,n=mode==M16_SF_QUIET16?8192u:40960u;
 unsigned frames=mode==M16_SF_QUIET16?15u:67u;int length=mode==M16_SF_QUIET16?3356:16778;
 const float *x=embedded?current.physical.beam:input;
 m16_simd_fft_select(2);m16_simd_fft_clear_tables();
 if(mode==M16_SF_QUIET16)memset(reference.quiet_rows,0,sizeof(reference.quiet_rows));
 for(c=0;c<channels;c++){
  stft_context ctx={&reference,mode==M16_SF_BEAM1?candidate:c};
  if(mic16_resample_channel(x,n,channels,c,reference.resampled)!=length)return -1;
  if(m16_stft512_visit_real(reference.resampled,(unsigned)length,frames,&reference,
   mode==M16_SF_OBSERVED16?capture_observed:(mode==M16_SF_QUIET16?sf_original_quiet:sf_original_beam_power),&ctx))return -2;
 }
 if(mode==M16_SF_QUIET16){
  for(z=0;z<64;z++){
   double a[15];int j;
   for(t=0;t<15;t++)a[t]=reference.quiet_rows[t*64+z]/64.;
   for(t=1;t<15;t++){double v=a[t];j=(int)t;while(j>0 && a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;}
   reference.quiet_power[z]=a[7];
  }
 }
 if(mode==M16_SF_OBSERVED16){
  for(t=0;t<67;t++)for(z=0;z<64;z++){
   double power=0.;
   for(k=0;k<4;k++)for(c=0;c<16;c++){
    unsigned q=((t*257+1+z*4+k)*16+c)*2;
    power+=reference.observed[q]*reference.observed[q]+reference.observed[q+1]*reference.observed[q+1];
   }
   reference.channel_power[t*64+z]=power/64.;
  }
 }
 if(mode!=M16_SF_OBSERVED16){
  memcpy(fft_probe,reference.fft_re,512*sizeof(double));memcpy(fft_probe+512,reference.fft_im,512*sizeof(double));
  memcpy(fft_probe+1024,reference.spectrum,514*sizeof(double));
 }
 for(i=0;i<3;i++)m16_fft_fast_calls[i]=0;
 return 0;
}
double *sf_array(unsigned which,unsigned original){
 mic16_extended_workspace *w=original?&reference:&current;
 switch(which){
 case 0:return w->observed;case 1:return w->quiet_rows;case 2:return w->quiet_power;
 case 3:return w->channel_power;case 4:return w->beam_power;
 case 5:return original?fft_probe:w->fft_re;case 6:return original?fft_probe+512:w->fft_im;
 case 7:return original?fft_probe+1024:w->spectrum;default:return 0;
 }
}
unsigned sf_reference_count(unsigned n){return n<4?m16_simd_fft_calls[n]:0;}
