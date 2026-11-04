#include "mic16_enh_spectrum_task.h"
#include "mic16_resample_tables.h"
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d455331u
static int valid(const m16_enh_spectrum_task *t){return t && !((uintptr_t)t%_Alignof(m16_enh_spectrum_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static int environment(void){
#if defined(__arm__)
 unsigned x;__asm__ volatile("vmrs %0,fpscr":"=r"(x));return !(x&0x01c00000u);
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&0xe040u)return 0;
#endif
 return 1;
#endif
}
static int fail(m16_enh_spectrum_task *t,int rc){
 (void)m16_resample_task_stop(&t->resample);(void)m16_complex_fft_stop(&t->fft);
 t->error=rc;t->phase=M16_ES_FAILED;return rc;
}
int m16_enh_spectrum_init(m16_enh_spectrum_task *t,mic16_extended_workspace *w){
 if(!t || (uintptr_t)t%_Alignof(m16_enh_spectrum_task) || t->magic || !w || (uintptr_t)w%_Alignof(mic16_extended_workspace) ||
  !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_ES_INVALID;
 *t=(m16_enh_spectrum_task){0};
 if(m16_resample_task_init(&t->resample) || m16_complex_fft_init(&t->fft,w->fft_re,w->fft_im,512,m16_twiddle_re,m16_twiddle_im,512))return M16_ES_INVALID;
 t->magic=MAGIC;t->work=w;return 0;
}
int m16_enh_spectrum_releasable(const m16_enh_spectrum_task *t){
 return valid(t) && (t->phase==M16_ES_IDLE || t->phase==M16_ES_COMPLETE || t->phase==M16_ES_STOPPED || t->phase==M16_ES_FAILED) &&
  m16_resample_task_releasable(&t->resample) && m16_complex_fft_releasable(&t->fft);
}
static int resample_begin(m16_enh_spectrum_task *t){
 if(t->mode==M16_ES_BEAM64)return m16_resample_task_begin64(&t->resample,t->input64,40960,t->work->resampled,16778);
 return m16_resample_task_begin32(&t->resample,t->input32,t->mode==M16_ES_QUIET16?8192u:40960u,16,t->channel,t->work->resampled,16778);
}
static int begin(m16_enh_spectrum_task *t,unsigned mode,const void *x,unsigned n,unsigned candidate){
 int rc;uintptr_t bytes=(uintptr_t)n*(mode==M16_ES_BEAM64?8u:4u);
 if(!valid(t) || !x || mode>M16_ES_BEAM64 || (mode==M16_ES_BEAM64?candidate>=16u:0) ||
  n!=(mode==M16_ES_BEAM64?40960u:(mode==M16_ES_QUIET16?8192u*16u:40960u*16u)) ||
  (uintptr_t)x%(mode==M16_ES_BEAM64?8u:4u) || !apart(x,bytes,t,sizeof(*t)) || !apart(x,bytes,t->work,sizeof(*t->work)))return M16_ES_INVALID;
 if(!m16_enh_spectrum_releasable(t))return M16_ES_BUSY;
 if(!environment())return M16_ES_FLOAT_ENV;
 t->mode=mode;t->candidate=candidate;t->input32=mode==M16_ES_BEAM64?0:(const float *)x;t->input64=mode==M16_ES_BEAM64?(const double *)x:0;
 t->channel=t->frame=t->cursor=t->band=t->last_items=t->max_items=t->transforms=0;t->steps=0;t->error=0;
 t->length=mode==M16_ES_QUIET16?3356u:16778u;t->frames=mode==M16_ES_QUIET16?15u:67u;
 rc=resample_begin(t);if(rc)return fail(t,rc);
 t->phase=mode==M16_ES_QUIET16?M16_ES_CLEAR:M16_ES_RESAMPLE;return 0;
}
int m16_enh_spectrum_begin32(m16_enh_spectrum_task *t,unsigned mode,const float *x,unsigned n){
 if(mode>M16_ES_QUIET16)return M16_ES_INVALID;
 return begin(t,mode,x,n,0);
}
int m16_enh_spectrum_begin64(m16_enh_spectrum_task *t,const double *x,unsigned candidate){return begin(t,M16_ES_BEAM64,x,40960,candidate);}
static int step(m16_enh_spectrum_task *t){
 unsigned i,end;int rc;mic16_extended_workspace *w=t->work;
 if(t->phase==M16_ES_COMPLETE)return 0;
 if(t->phase==M16_ES_STOPPED)return 2;
 if(t->phase==M16_ES_FAILED)return t->error;
 if(t->phase==M16_ES_IDLE)return M16_ES_INVALID;
 if(!environment())return fail(t,M16_ES_FLOAT_ENV);
 if(t->phase==M16_ES_CLEAR){
  end=t->cursor+64u;if(end>15u*64u)end=15u*64u;
  for(i=t->cursor;i<end;i++){w->quiet_rows[i]=0.;t->last_items++;}
  t->cursor=end;if(end==15u*64u){t->cursor=0;t->phase=M16_ES_RESAMPLE;}return 1;
 }
 if(t->phase==M16_ES_RESAMPLE){
  rc=m16_resample_task_step(&t->resample);t->last_items=t->resample.last_items;
  if(rc<0)return fail(t,rc);
  if(rc==2)return fail(t,M16_ES_INVALID);
  if(!rc){t->cursor=0;t->phase=M16_ES_LOAD;}return 1;
 }
 if(t->phase==M16_ES_LOAD){
  end=t->cursor+64u;if(end>512u)end=512u;
  for(i=t->cursor;i<end;i++){
   int pos=(int)(t->frame*256u+i)-256;
   double x=pos>=0 && pos<(int)t->length?w->resampled[pos]:0.;
   if(!finite64(x))return fail(t,M16_ES_INPUT);
   w->fft_re[i]=pos>=0 && pos<(int)t->length?x*m16_periodic_hann512[i]:0.;w->fft_im[i]=0.;t->last_items++;
  }
  t->cursor=end;
  if(end==512u){rc=m16_complex_fft_begin(&t->fft,512,0);if(rc)return fail(t,rc);t->phase=M16_ES_FFT;}return 1;
 }
 if(t->phase==M16_ES_FFT){
  rc=m16_complex_fft_step(&t->fft);t->last_items=t->fft.last_items;
  if(rc<0)return fail(t,rc);
  if(rc==2)return fail(t,M16_ES_INVALID);
  if(!rc){t->transforms++;t->cursor=0;t->phase=M16_ES_SPECTRUM;}return 1;
 }
 if(t->phase==M16_ES_SPECTRUM){
  end=t->cursor+32u;if(end>257u)end=257u;
  for(i=t->cursor;i<end;i++){
   w->spectrum[i*2u]=w->fft_re[i];w->spectrum[i*2u+1u]=w->fft_im[i];
   if(t->mode==M16_ES_OBSERVED16){unsigned q=((t->frame*257u+i)*16u+t->channel)*2u;w->observed[q]=w->fft_re[i];w->observed[q+1u]=w->fft_im[i];}
   t->last_items+=2u;
  }
  t->cursor=end;
  if(end==257u){t->band=0;t->phase=t->mode==M16_ES_OBSERVED16?M16_ES_NEXT:M16_ES_CONSUME;}return 1;
 }
 if(t->phase==M16_ES_CONSUME){
  unsigned k;double power=0.;
  for(k=0;k<4u;k++){
   unsigned b=1u+t->band*4u+k;
   double term=w->spectrum[b*2u]*w->spectrum[b*2u]+w->spectrum[b*2u+1u]*w->spectrum[b*2u+1u];
   if(t->mode==M16_ES_QUIET16)w->quiet_rows[t->frame*64u+t->band]+=term;else power+=term;
   t->last_items+=2u;
  }
  if(t->mode==M16_ES_BEAM64){if(!finite64(power))return fail(t,M16_ES_OUTPUT);w->beam_power[(t->candidate*67u+t->frame)*64u+t->band]=power/4.;}
  if(++t->band==64u)t->phase=M16_ES_NEXT;
  return 1;
 }
 if(t->phase==M16_ES_NEXT){
  if(++t->frame<t->frames){t->cursor=0;t->phase=M16_ES_LOAD;return 1;}
  t->frame=t->cursor=0;
  if(t->mode!=M16_ES_BEAM64 && ++t->channel<16u){rc=resample_begin(t);if(rc)return fail(t,rc);t->phase=M16_ES_RESAMPLE;return 1;}
  if(t->mode==M16_ES_BEAM64){t->phase=M16_ES_COMPLETE;return 0;}
  t->phase=t->mode==M16_ES_QUIET16?M16_ES_MEDIAN:M16_ES_POWER;return 1;
 }
 if(t->phase==M16_ES_MEDIAN){
  double a[15];unsigned j,k;
  for(i=0;i<15u;i++){a[i]=w->quiet_rows[i*64u+t->cursor]/64.;if(!finite64(a[i]))return fail(t,M16_ES_OUTPUT);}
  for(i=1;i<15u;i++){double v=a[i];j=i;while(j>0 && a[j-1u]>v){a[j]=a[j-1u];j--;}a[j]=v;}
  w->quiet_power[t->cursor]=a[7];t->last_items=15u;
  k=++t->cursor;if(k==64u){t->phase=M16_ES_COMPLETE;return 0;}return 1;
 }
 if(t->phase==M16_ES_POWER){
  unsigned frame=t->cursor/64u,z=t->cursor%64u,k,c;double power=0.;
  for(k=0;k<4u;k++)for(c=0;c<16u;c++){
   i=((frame*257u+1u+z*4u+k)*16u+c)*2u;
   power+=w->observed[i]*w->observed[i]+w->observed[i+1u]*w->observed[i+1u];t->last_items++;
  }
  if(!finite64(power))return fail(t,M16_ES_OUTPUT);
  w->channel_power[t->cursor]=power/64.;
  if(++t->cursor==67u*64u){t->phase=M16_ES_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_ES_INVALID);
}
int m16_enh_spectrum_step(m16_enh_spectrum_task *t){
 int rc;if(!valid(t))return M16_ES_INVALID;t->last_items=0;t->steps++;rc=step(t);if(t->last_items>t->max_items)t->max_items=t->last_items;return rc;
}
int m16_enh_spectrum_stop(m16_enh_spectrum_task *t){
 if(!valid(t))return M16_ES_INVALID;
 (void)m16_resample_task_stop(&t->resample);(void)m16_complex_fft_stop(&t->fft);t->phase=M16_ES_STOPPED;return 1;
}
