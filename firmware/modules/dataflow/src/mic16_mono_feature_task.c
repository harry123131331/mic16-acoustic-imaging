#include "mic16_mono_feature_task.h"
#include <math.h>
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d4d4654u
static const unsigned starts[32]={0,25,50,76,101,126,151,177,202,227,252,278,303,328,353,378,404,429,454,479,505,530,555,580,606,631,656,681,706,732,757,782};
static const unsigned counts[32]={25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25};
static int valid(const m16_mono_feature_task *t){return t && !((uintptr_t)t%_Alignof(m16_mono_feature_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t na,const void *b,uintptr_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-na && y<=UINTPTR_MAX-nb && (x+na<=y || y+nb<=x);
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
static double balanced8(const double *r){return ((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));}
/* 原NumPy式小段求和，实际频带长度只有25或26，整个归约仍是有限工作片段。 */
static double small_sum(const double *a,unsigned n){
 double r[8],s=-0.;unsigned i,j;
 if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
 for(j=0;j<8;j++)r[j]=a[j];
 for(i=8;i<n-n%8u;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
 s=balanced8(r);for(;i<n;i++)s+=a[i];return s;
}
static int abort_task(m16_mono_feature_task *t,int rc){
 if(t->provider&&t->provider->owns(t->provider->context,t))(void)t->provider->cancel(t->provider->context,t);
 if(!t->error)t->error=rc;
 t->stop_requested=1;t->phase=t->owned?M16_MB_STOPPING:M16_MB_FAILED;
 return t->owned?1:t->error;
}
int m16_mono_feature_init(m16_mono_feature_task *t,m16_shared_spectrum_task *device,
 mic16_spectral_workspace *work,double *scratch,double *powers){
 const void *p[6];uintptr_t bytes[6];unsigned i,j;
 if(!t || !device || !work || !scratch || !powers ||
  (uintptr_t)t%_Alignof(m16_mono_feature_task) || (uintptr_t)device%_Alignof(m16_shared_spectrum_task) ||
  (uintptr_t)work%_Alignof(mic16_spectral_workspace) ||
  (uintptr_t)scratch%8u || (uintptr_t)powers%8u || t->magic || !m16_shared_spectrum_task_available(device) ||
  work->initialized!=0x4d313653u || !environment())return M16_MB_INVALID;
 p[0]=t;p[1]=device;p[2]=device->owner;p[3]=work;p[4]=scratch;p[5]=powers;
 bytes[0]=sizeof(*t);bytes[1]=sizeof(*device);bytes[2]=sizeof(*device->owner);bytes[3]=sizeof(*work);bytes[4]=2048u*8u;bytes[5]=807u*8u;
 for(i=0;i<6;i++)for(j=i+1;j<6;j++)if(!apart(p[i],bytes[i],p[j],bytes[j]))return M16_MB_INVALID;
 *t=(m16_mono_feature_task){0};t->magic=MAGIC;t->device=device;t->work=work;t->scratch=scratch;t->powers=powers;return 0;
}
int m16_mono_feature_bind_fft64(m16_mono_feature_task *t,const m16_fft_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->provider||!m16_mono_feature_releasable(t))return M16_MB_INVALID;
 t->provider=p;return 0;
}
int m16_mono_feature_begin(m16_mono_feature_task *t,const float *beam,unsigned values,unsigned frames,unsigned only,
 double *bands,unsigned bn,double *levels,unsigned ln,uint64_t epoch,uint64_t first_job){
 const void *fixed[6],*io[3];uintptr_t fs[6],is[3];unsigned i,j,n;
 if(!valid(t) || !beam || !levels || (frames!=4 && frames!=20) || only>1 || values<frames*2048u ||
  ln<frames || (uintptr_t)beam%4u || (uintptr_t)levels%8u || !epoch || !first_job ||
  first_job>UINT64_MAX-(frames==20?1u:0u) || (only?(bands!=0 || bn!=0):(!bands || (uintptr_t)bands%8u || bn<frames*32u)))return M16_MB_INVALID;
 if(!m16_mono_feature_releasable(t))return M16_MB_BUSY;
 if(t->device->owner->quarantined)return M16_MB_DEVICE_ERROR;
 if(!environment())return M16_MB_FLOAT_ENV;
 fixed[0]=t;fixed[1]=t->device;fixed[2]=t->device->owner;fixed[3]=t->work;fixed[4]=t->scratch;fixed[5]=t->powers;
 fs[0]=sizeof(*t);fs[1]=sizeof(*t->device);fs[2]=sizeof(*t->device->owner);fs[3]=sizeof(*t->work);fs[4]=2048u*8u;fs[5]=807u*8u;
 io[0]=beam;io[1]=levels;io[2]=bands;is[0]=frames*2048u*4u;is[1]=frames*8u;is[2]=frames*32u*8u;n=only?2u:3u;
 for(i=0;i<n;i++){
  for(j=0;j<6;j++)if(!apart(io[i],is[i],fixed[j],fs[j]))return M16_MB_INVALID;
  for(j=i+1;j<n;j++)if(!apart(io[i],is[i],io[j],is[j]))return M16_MB_INVALID;
 }
 t->beam=beam;t->bands=bands;t->levels=levels;t->frames=frames;t->levels_only=only;t->epoch=epoch;t->first_job=first_job;
 t->phase=M16_MB_VALIDATE;t->base=t->frame=t->cursor=t->block=t->owned=t->cpu_fallback=t->range_fallback=t->stop_requested=0;
 t->error=0;t->steps=t->completed_batches=0;t->last_items=t->max_items=0;return 0;
}
static int drain(m16_mono_feature_task *t,unsigned fallback){
 int rc;
 if(t->owned){
  rc=m16_shared_spectrum_task_stop(t->device);
  if(!m16_shared_spectrum_task_releasable(t->device)){
   if(rc<0){t->error=rc;t->phase=M16_MB_FAILED;return rc;}
   rc=m16_shared_spectrum_task_poll(t->device);
   if(rc<0 && !m16_shared_spectrum_task_releasable(t->device)){t->error=rc;t->phase=M16_MB_FAILED;return rc;}
   if(!m16_shared_spectrum_task_releasable(t->device))return 1;
  }
  rc=m16_shared_spectrum_task_retire(t->device,t->epoch,t->job);
  if(rc){t->error=rc;t->phase=M16_MB_FAILED;return rc;}
  t->owned=0;
 }
 if(fallback && !t->stop_requested){
  t->cpu_fallback=t->range_fallback=1;t->frame=t->base=t->cursor=0;t->phase=M16_MB_LOAD;return 1;
 }
 t->phase=t->error?M16_MB_FAILED:M16_MB_STOPPED;return t->error?t->error:2;
}
static int cpu_fft(m16_mono_feature_task *t){
 unsigned i,end=t->cursor+64u;double *r=t->work->fft_re,*im=t->work->fft_im;
 const double *wr=t->work->twiddle_re,*wi=t->work->twiddle_im;
 switch(t->phase){
 case M16_MB_FP64_DISPATCH:{
  int rc=t->provider->request(t->provider->context,t,r,im,2048u,wr,wi,4096u,0u);
  if(rc<0)return abort_task(t,rc);
  if(rc==2){t->cursor=1;t->fft_reverse=0;t->phase=M16_MB_REVERSE;}
  else if(!rc)t->phase=M16_MB_FP64_DEVICE;
  return 1;
 }
 case M16_MB_FP64_DEVICE:{
  int rc=t->provider->poll(t->provider->context,t);if(rc<0)return abort_task(t,rc);
  if(rc==2){t->cursor=1;t->fft_reverse=0;t->phase=M16_MB_REVERSE;}
  else if(!rc){t->cursor=0;t->phase=M16_MB_RECOVER;}
  return 1;
 }
 case M16_MB_ZERO:
  if(end>4096u)end=4096u;
  for(i=t->cursor;i<end;i++){r[i]=0.;im[i]=0.;t->last_items++;}
  t->cursor=end;if(end==4096u){t->cursor=0;t->phase=M16_MB_COMPRESS;}return 1;
 case M16_MB_COMPRESS:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){double a=r[2u*i],b=r[2u*i+1u];r[i]=a;im[i]=b;t->last_items++;}
  t->cursor=end;if(end==2048u){t->cursor=t->provider?0u:1u;t->fft_reverse=0;t->phase=t->provider?M16_MB_FP64_DISPATCH:M16_MB_REVERSE;}return 1;
 case M16_MB_REVERSE:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){
   unsigned bit=1024u,j=t->fft_reverse;for(;j&bit;bit>>=1)j^=bit;j^=bit;t->fft_reverse=j;
   if(i<j){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}t->last_items++;
  }
  t->cursor=end;if(end==2048u){t->cursor=0;t->fft_len=2;t->phase=M16_MB_BUTTERFLIES;}return 1;
 case M16_MB_BUTTERFLIES:
  if(end>1024u)end=1024u;
  for(i=t->cursor;i<end;i++){
   unsigned half=t->fft_len/2u,k=i%half,a=(i/half)*t->fft_len+k,b=a+half,q=k*(4096u/t->fft_len);
   double re=wr[q]*r[b]-wi[q]*im[b],ii=wr[q]*im[b]+wi[q]*r[b],ar=r[a],ai=im[a];
   r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;t->last_items++;
  }
  t->cursor=end;if(end==1024u){t->cursor=0;t->fft_len<<=1;if(t->fft_len>2048u)t->phase=M16_MB_RECOVER;}return 1;
 case M16_MB_RECOVER:
  if(t->cursor==0){double a=r[0],b=im[0];r[0]=a+b;im[0]=0.;r[2048]=a-b;im[2048]=0.;t->cursor=1;end=64u;t->last_items=1;}
  if(end>1025u)end=1025u;
  for(i=t->cursor;i<end;i++){
   unsigned p=2048u-i;double a=r[i],b=im[i],c=r[p],d=im[p];
   double er=(a+c)*.5,ei=(b-d)*.5,orr=(b+d)*.5,oi=(c-a)*.5;
   double x=er+wr[i]*orr-wi[i]*oi,y=ei+wr[i]*oi+wi[i]*orr;
   double u=er+wr[p]*orr+wi[p]*oi,v=-ei-wr[p]*oi+wi[p]*orr;
   r[i]=x;im[i]=y;if(p!=i){r[p]=u;im[p]=v;}t->last_items++;
  }
  t->cursor=end;if(end==1025u){t->cursor=1;t->phase=M16_MB_CONJUGATE;}return 1;
 case M16_MB_CONJUGATE:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){r[4096u-i]=r[i];im[4096u-i]=-im[i];t->last_items++;}
  t->cursor=end;if(end==2048u){t->cursor=0;t->phase=M16_MB_CHECK_FFT;}return 1;
 case M16_MB_CHECK_FFT:
  if(end>4096u)end=4096u;
  for(i=t->cursor;i<end;i++){if(!finite64(r[i]) || !finite64(im[i]))return abort_task(t,M16_MB_OUTPUT);t->last_items++;}
  t->cursor=end;if(end==4096u){t->cursor=0;t->phase=M16_MB_POWERS;}return 1;
 default:return abort_task(t,M16_MB_INVALID);
 }
}
static int step(m16_mono_feature_task *t){
 unsigned i,j,end;int rc;double *r=t->work->fft_re,*im=t->work->fft_im;
 if(t->phase==M16_MB_COMPLETE)return 0;
 if(t->phase==M16_MB_STOPPED)return 2;
 if(t->phase==M16_MB_FAILED)return t->error;
 if(t->phase==M16_MB_IDLE)return M16_MB_INVALID;
 if(t->phase==M16_MB_STOPPING)return drain(t,0);
 if(t->stop_requested){t->phase=M16_MB_STOPPING;return drain(t,0);}
 if(!environment())return abort_task(t,M16_MB_FLOAT_ENV);
 if((t->phase>=M16_MB_ZERO && t->phase<=M16_MB_CHECK_FFT)||t->phase==M16_MB_FP64_DISPATCH||t->phase==M16_MB_FP64_DEVICE)return cpu_fft(t);
 end=t->cursor+64u;
 switch(t->phase){
 case M16_MB_VALIDATE:
  if(end>t->frames*2048u)end=t->frames*2048u;
  for(i=t->cursor;i<end;i++){float v=t->beam[i];if(!(v>=-1.f && v<=1.f))return abort_task(t,M16_MB_INPUT);t->last_items++;}
  t->cursor=end;if(end==t->frames*2048u){t->cursor=0;t->phase=t->levels_only?M16_MB_LOAD:M16_MB_ACQUIRE;}return 1;
 case M16_MB_ACQUIRE:
  t->active=t->frames-t->base;if(t->active>16u)t->active=16u;t->job=t->first_job+t->base/16u;
  rc=m16_shared_spectrum_task_acquire(t->device,t->epoch,t->job,t->active);
  if(rc==M16_SS_BUSY){if(t->device->owner->quarantined)return abort_task(t,M16_MB_DEVICE_ERROR);return 1;}
  if(rc)return abort_task(t,rc);
  t->owned=1;t->frame=t->base;t->cursor=0;t->phase=M16_MB_LOAD;return 1;
 case M16_MB_LOAD:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){
   double x=t->beam[t->frame*2048u+i];if(!(x>=-1. && x<=1.))return abort_task(t,M16_MB_INPUT);
   r[i]=x;t->scratch[i]=x*x;t->last_items++;
  }
  t->cursor=end;if(end==2048u){t->cursor=t->block=0;t->phase=M16_MB_SUM;}return 1;
 case M16_MB_SUM:
  if(t->cursor==0){for(j=0;j<8;j++){t->mean_lane[j]=r[t->block*128u+j];t->energy_lane[j]=t->scratch[t->block*128u+j];}t->cursor=8;t->last_items=8;}
  while(t->cursor<128u && t->last_items+8u<=64u){
   for(j=0;j<8;j++){t->mean_lane[j]+=r[t->block*128u+t->cursor+j];t->energy_lane[j]+=t->scratch[t->block*128u+t->cursor+j];}
   t->cursor+=8;t->last_items+=8;
  }
  if(t->cursor==128u){
   t->mean_leaf[t->block]=balanced8(t->mean_lane);t->energy_leaf[t->block]=balanced8(t->energy_lane);
   t->cursor=0;if(++t->block==16u){t->reduce_n=16;t->phase=M16_MB_REDUCE;}
  }return 1;
 case M16_MB_REDUCE:
  for(i=0;i<t->reduce_n/2u;i++){t->mean_leaf[i]=t->mean_leaf[2u*i]+t->mean_leaf[2u*i+1u];t->energy_leaf[i]=t->energy_leaf[2u*i]+t->energy_leaf[2u*i+1u];t->last_items++;}
  t->reduce_n/=2u;
  if(t->reduce_n==1u){
   double energy=t->energy_leaf[0]/2048.;t->mean=t->mean_leaf[0]/2048.;
   if(!finite64(energy) || !finite64(t->mean))return abort_task(t,M16_MB_OUTPUT);
   t->levels[t->frame]=10.*log10(energy>1e-20?energy:1e-20);
   t->cursor=0;t->maximum=0.;
   if(t->levels_only){if(++t->frame==t->frames){t->phase=M16_MB_COMPLETE;return 0;}t->phase=M16_MB_LOAD;}
   else t->phase=M16_MB_WINDOW;
  }return 1;
 case M16_MB_WINDOW:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){
   double v=(r[i]-t->mean)*t->work->hann[i],m=v<0.?-v:v;
   if(!finite64(v))return abort_task(t,M16_MB_INPUT);
   r[i]=v;im[i]=0.;if(m>t->maximum)t->maximum=m;t->last_items++;
  }
  t->cursor=end;
  if(end==2048u){
   t->cursor=0;
   if(t->cpu_fallback){t->cursor=2048;t->phase=M16_MB_ZERO;}
   else if(t->maximum>2. || (t->maximum!=0. && t->maximum<1e-15))t->phase=M16_MB_FALLBACK;
   else t->phase=M16_MB_PACK;
  }return 1;
 case M16_MB_PACK:{
  uint64_t *a=m16_shared_spectrum_task_arena(t->device);if(!a)return abort_task(t,M16_MB_DEVICE_ERROR);
  end=t->cursor+32u;if(end>1024u)end=1024u;
  for(i=t->cursor;i<end;i++){
   union{float f;uint32_t u;}x,y;x.f=(float)r[2u*i];y.f=(float)r[2u*i+1u];
   a[(t->frame-t->base)*1024u+i]=(uint64_t)x.u|((uint64_t)y.u<<32);t->last_items+=2;
  }
  t->cursor=end;if(end==1024u){t->cursor=0;if(++t->frame==t->base+t->active){t->frame=t->base;t->phase=M16_MB_SUBMIT;}else t->phase=M16_MB_LOAD;}return 1;
 }
 case M16_MB_SUBMIT:
  rc=m16_shared_spectrum_task_submit(t->device);if(rc)return abort_task(t,rc);t->phase=M16_MB_DEVICE;return 1;
 case M16_MB_DEVICE:
  rc=m16_shared_spectrum_task_poll(t->device);if(rc<0)return abort_task(t,rc);
  if(rc==0){t->completed_batches++;t->cursor=0;t->phase=M16_MB_READ;}
  else if(rc==2)return abort_task(t,M16_MB_DEVICE_ERROR);
  return 1;
 case M16_MB_READ:{
  const uint64_t *a=m16_shared_spectrum_task_output(t->device,t->epoch,t->job);
  if(!a)return abort_task(t,M16_MB_DEVICE_ERROR);
  if(end>807u)end=807u;
  for(i=t->cursor;i<end;i++){
   uint64_t v=a[(t->frame-t->base)*807u+i];union{uint32_t u;float f;}x,y;x.u=(uint32_t)v;y.u=(uint32_t)(v>>32);
   r[i+32u]=(double)x.f;im[i+32u]=(double)y.f;t->last_items++;
  }
  t->cursor=end;if(end==807u){t->cursor=0;t->phase=M16_MB_POWERS;}return 1;
 }
 case M16_MB_POWERS:
  if(end>807u)end=807u;
  for(i=t->cursor;i<end;i++){
   double re=r[i+32u],ii=im[i+32u],p=re*re+ii*ii;
   if(!finite64(p))return abort_task(t,M16_MB_OUTPUT);
   t->powers[i]=p;t->last_items++;
  }
  t->cursor=end;if(end==807u){t->cursor=0;t->band=0;t->phase=M16_MB_BANDS;}return 1;
 case M16_MB_BANDS:{
  double power=small_sum(t->powers+starts[t->band],counts[t->band])/counts[t->band];
  t->bands[t->frame*32u+t->band]=10.*log10(power>1e-20?power:1e-20);t->last_items=counts[t->band];
  if(++t->band==32u){
   t->cursor=0;t->frame++;
   if(t->cpu_fallback){if(t->frame==t->frames){t->phase=M16_MB_COMPLETE;return 0;}t->phase=M16_MB_LOAD;}
   else t->phase=t->frame==t->base+t->active?M16_MB_RETIRE:M16_MB_READ;
  }return 1;
 }
 case M16_MB_RETIRE:
  rc=m16_shared_spectrum_task_retire(t->device,t->epoch,t->job);if(rc)return abort_task(t,rc);
  t->owned=0;t->base+=t->active;t->cursor=0;
  if(t->base==t->frames){t->phase=M16_MB_COMPLETE;return 0;}t->phase=M16_MB_ACQUIRE;return 1;
 case M16_MB_FALLBACK:return drain(t,1);
 default:return abort_task(t,M16_MB_INVALID);
 }
}
int m16_mono_feature_step(m16_mono_feature_task *t){
 int rc;if(!valid(t))return M16_MB_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_mono_feature_stop(m16_mono_feature_task *t){
 if(!valid(t))return M16_MB_INVALID;
 if(t->provider&&t->provider->owns(t->provider->context,t)){
  int rc=t->provider->cancel(t->provider->context,t);if(rc<0)return abort_task(t,rc);
 }
 t->stop_requested=1;t->phase=M16_MB_STOPPING;
 if(!t->owned){t->phase=M16_MB_STOPPED;return 1;}
 (void)m16_shared_spectrum_task_stop(t->device);return 0;
}
int m16_mono_feature_releasable(const m16_mono_feature_task *t){
 return valid(t) && !t->owned && (t->phase==M16_MB_IDLE || t->phase==M16_MB_COMPLETE || t->phase==M16_MB_STOPPED || t->phase==M16_MB_FAILED) &&
  (!t->provider||!t->provider->owns(t->provider->context,t));
}
