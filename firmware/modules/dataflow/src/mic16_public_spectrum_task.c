#include "mic16_public_spectrum_task.h"
#include <string.h>
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d505354u
static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int valid(const m16_public_spectrum_task *t){return t&&t->magic==MAGIC;}
static int fail(m16_public_spectrum_task *t,int rc){t->error=rc;t->phase=M16_PS_FAILED;return rc;}
static int float_environment(void){
#if defined(__arm__)
 uint32_t fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr));return !(fpscr&((3u<<22)|(1u<<24)));
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&((3u<<13)|(1u<<15)|(1u<<6)))return 0;
#endif
 return 1;
#endif
}
int m16_public_spectrum_init(m16_public_spectrum_task *t,mic16_spectral_frame *o,mic16_spectral_workspace *w){
 if(!t||!o||!w||((uintptr_t)t%_Alignof(m16_public_spectrum_task))||t->magic||
    ((uintptr_t)o%_Alignof(mic16_spectral_frame))||((uintptr_t)w%_Alignof(mic16_spectral_workspace))||
    overlap(t,sizeof(*t),o,sizeof(*o))||overlap(t,sizeof(*t),w,sizeof(*w))||
    overlap(o,sizeof(*o),w,sizeof(*w))||w->initialized!=0x4d313653u)return M16_PS_INVALID;
 if(!float_environment())return M16_PS_FLOAT_ENV;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->output=o;t->work=w;return 0;
}
int m16_public_spectrum_bind(m16_public_spectrum_task *t,const m16_fft_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->provider||
    (t->phase!=M16_PS_IDLE&&t->phase!=M16_PS_COMPLETE&&t->phase!=M16_PS_STOPPED))return M16_PS_INVALID;
 t->provider=p;return 0;
}
int m16_public_spectrum_retarget(m16_public_spectrum_task *t,mic16_spectral_frame *o){
 if(!valid(t)||!o||(uintptr_t)o%_Alignof(mic16_spectral_frame)||overlap(o,sizeof(*o),t,sizeof(*t))||
    overlap(o,sizeof(*o),t->work,sizeof(*t->work)))return M16_PS_INVALID;
 if(t->phase!=M16_PS_IDLE&&t->phase!=M16_PS_COMPLETE&&t->phase!=M16_PS_STOPPED)return M16_PS_BUSY;
 if(t->provider&&t->provider->owns(t->provider->context,t))return M16_PS_BUSY;
 t->output=o;return 0;
}
int m16_public_spectrum_begin(m16_public_spectrum_task *t,const float *pcm,unsigned n,uint64_t epoch,uint64_t frame){
 if(!valid(t))return M16_PS_INVALID;
 if(t->phase!=M16_PS_IDLE&&t->phase!=M16_PS_COMPLETE&&t->phase!=M16_PS_STOPPED&&t->phase!=M16_PS_FAILED)return M16_PS_BUSY;
 if(!pcm||((uintptr_t)pcm%_Alignof(float))||n<2048u*16u||!epoch||
    overlap(pcm,2048u*16u*4u,t,sizeof(*t))||overlap(pcm,2048u*16u*4u,t->output,sizeof(*t->output))||
    overlap(pcm,2048u*16u*4u,t->work,sizeof(*t->work))||t->work->initialized!=0x4d313653u)return M16_PS_INVALID;
 if(!float_environment())return M16_PS_FLOAT_ENV;
 if(t->provider&&t->provider->owns(t->provider->context,t))return M16_PS_BUSY;
 t->pcm=pcm;t->epoch=epoch;t->frame=frame;t->phase=M16_PS_VALIDATE;t->channel=t->cursor=t->reverse=0;
 t->error=0;t->steps=0;t->last_items=t->max_items=0;return 0;
}
static int run_step(m16_public_spectrum_task *t){
 unsigned i,end;double *r=t->work->fft_re,*im=t->work->fft_im;
 const double *wr=t->work->twiddle_re,*wi=t->work->twiddle_im;
 if(t->phase==M16_PS_COMPLETE)return 0;
 if(t->phase==M16_PS_STOPPED)return 2;
 if(t->phase==M16_PS_FAILED)return t->error;
 if(t->phase==M16_PS_IDLE)return M16_PS_INVALID;
 if(!float_environment())return fail(t,M16_PS_FLOAT_ENV);
 end=t->cursor+64u;
 switch(t->phase){
 case M16_PS_DISPATCH:{
  int rc=t->provider->request(t->provider->context,t,r,im,2048u,wr,wi,4096u,0u);
  if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_PS_REVERSE;return 1;}
  if(!rc){t->phase=M16_PS_DEVICE;}
  return 1;
 }
 case M16_PS_DEVICE:{
  int rc=t->provider->poll(t->provider->context,t);if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_PS_REVERSE;return 1;}
  if(!rc){t->cursor=0;t->phase=M16_PS_RECOVER;}return 1;
 }
 case M16_PS_VALIDATE:
  if(end>2048u*16u)end=2048u*16u;
  for(i=t->cursor;i<end;i++){float v=t->pcm[i];if(!(v>=-1.f&&v<=1.f))return fail(t,M16_PS_INPUT);t->last_items++;}
  t->cursor=end;if(end==2048u*16u){t->phase=M16_PS_MEAN;t->cursor=0;t->mean=0.;}return 1;
 case M16_PS_MEAN:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){t->mean+=(double)t->pcm[i*16u+t->channel];t->last_items++;}
  t->cursor=end;if(end==2048u){t->mean/=2048.;t->phase=M16_PS_WINDOW;t->cursor=0;}return 1;
 case M16_PS_WINDOW:
  if(end>4096u)end=4096u;
  for(i=t->cursor;i<end;i++){
   if(i<2048u)r[i]=((double)t->pcm[i*16u+t->channel]-t->mean)*t->work->hann[i];else r[i]=0.;
   im[i]=0.;t->last_items++;
  }
  t->cursor=end;if(end==4096u){t->phase=M16_PS_COMPRESS;t->cursor=0;}return 1;
 case M16_PS_COMPRESS:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){double a=r[2u*i],b=r[2u*i+1u];r[i]=a;im[i]=b;t->last_items++;}
  t->cursor=end;if(end==2048u){t->cursor=t->provider?0u:1u;t->reverse=0;t->phase=t->provider?M16_PS_DISPATCH:M16_PS_REVERSE;}return 1;
 case M16_PS_REVERSE:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){
   unsigned bit=1024u,j=t->reverse;for(;j&bit;bit>>=1)j^=bit;j^=bit;t->reverse=j;
   if(i<j){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}t->last_items++;
  }
  t->cursor=end;if(end==2048u){t->phase=M16_PS_BUTTERFLIES;t->cursor=0;t->len=2u;}return 1;
 case M16_PS_BUTTERFLIES:
  if(end>1024u)end=1024u;
  unsigned half=t->len/2u,mask=half-1u,stride=4096u/t->len;
  for(i=t->cursor;i<end;i++){
   unsigned k=i&mask,a=(i<<1)-k,b=a+half,q=k*stride;
   double re=wr[q]*r[b]-wi[q]*im[b],ii=wr[q]*im[b]+wi[q]*r[b],ar=r[a],ai=im[a];
   r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;t->last_items++;
  }
  t->cursor=end;if(end==1024u){t->cursor=0;t->len<<=1;if(t->len>2048u)t->phase=M16_PS_RECOVER;}return 1;
 case M16_PS_RECOVER:
  if(t->cursor==0){double a=r[0],b=im[0];r[0]=a+b;im[0]=0.;r[2048]=a-b;im[2048]=0.;t->cursor=1;end=64u;t->last_items=1;}
  if(end>1025u)end=1025u;
  for(i=t->cursor;i<end;i++){
   unsigned p=2048u-i;double a=r[i],b=im[i],c=r[p],d=im[p];
   double er=(a+c)*.5,ei=(b-d)*.5,orr=(b+d)*.5,oi=(c-a)*.5;
   double x=er+wr[i]*orr-wi[i]*oi,y=ei+wr[i]*oi+wi[i]*orr;
   double u=er+wr[p]*orr+wi[p]*oi,v=-ei-wr[p]*oi+wi[p]*orr;
   r[i]=x;im[i]=y;if(p!=i){r[p]=u;im[p]=v;}t->last_items++;
  }
  t->cursor=end;if(end==1025u){t->phase=M16_PS_CONJUGATE;t->cursor=1;}return 1;
 case M16_PS_CONJUGATE:
  if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){r[4096u-i]=r[i];im[4096u-i]=-im[i];t->last_items++;}
  t->cursor=end;if(end==2048u){t->phase=M16_PS_STORE;t->cursor=0;}return 1;
 case M16_PS_STORE:
  if(end>807u)end=807u;
  for(i=t->cursor;i<end;i++){
   t->output->spectrum[(i*16u+t->channel)*2u]=r[i+32u];
   t->output->spectrum[(i*16u+t->channel)*2u+1u]=im[i+32u];t->last_items++;
  }
  t->cursor=end;if(end==807u){t->cursor=0;if(++t->channel==16u)t->phase=M16_PS_CHECK_SPECTRUM;else{t->phase=M16_PS_MEAN;t->mean=0.;}}return 1;
 case M16_PS_CHECK_SPECTRUM:
  if(end>807u*16u*2u)end=807u*16u*2u;
  for(i=t->cursor;i<end;i++){
   double value=t->output->spectrum[i];
   if(!(value>=-4096.&&value<=4096.))return fail(t,M16_PS_INPUT);
   t->last_items++;
  }
  t->cursor=end;if(end==807u*16u*2u){t->phase=M16_PS_OBSERVE;t->cursor=0;}return 1;
 case M16_PS_OBSERVE:{
  unsigned c,p,b=t->cursor;double power=0.;mic16_spectral_frame *o=t->output;
  for(c=0;c<16u;c++){
   double re=o->spectrum[(b*16u+c)*2u],ii=o->spectrum[(b*16u+c)*2u+1u];power+=re*re+ii*ii;
  }
  o->mean_channel_power[b]=power/16.;
  for(p=0;p<32u;p++){
   unsigned ca=p<16u?p:(p<24u?p-16u:p-24u),cb=p<16u?(p+1u)%16u:(p<24u?p-8u:p-20u);
   unsigned a=(b*16u+ca)*2u,bb=(b*16u+cb)*2u;
   double ar=o->spectrum[a],ai=o->spectrum[a+1u],br=o->spectrum[bb],bi=o->spectrum[bb+1u];
   double re=ar*br+ai*bi,ii=ai*br-ar*bi,mag=hypot(re,ii);
   o->phat[(b*32u+p)*2u]=mag>1e-8?(float)(re/mag):0.f;
   o->phat[(b*32u+p)*2u+1u]=mag>1e-8?(float)(ii/mag):0.f;
  }
  t->last_items=1;if(++t->cursor==807u){t->phase=M16_PS_ZONES;t->cursor=0;}return 1;
 }
 case M16_PS_ZONES:{
  unsigned z=t->cursor,start=z*34u,limit=start+34u;double power=0.;if(limit>807u)limit=807u;
  for(i=start;i<limit;i++)power+=t->output->mean_channel_power[i];
  power/=(limit-start);
  t->output->zone_level_db[z]=10.*log10(power>1e-20?power:1e-20);t->last_items=1;
  if(++t->cursor==24u){t->phase=M16_PS_COMPLETE;return 0;}return 1;
 }
 default:return fail(t,M16_PS_INVALID);
 }
}
int m16_public_spectrum_step(m16_public_spectrum_task *t){
 int rc;if(!valid(t))return M16_PS_INVALID;t->last_items=0;t->steps++;rc=run_step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_public_spectrum_stop(m16_public_spectrum_task *t){
 int rc=1;if(!valid(t))return M16_PS_INVALID;
 if(t->provider&&t->provider->owns(t->provider->context,t)){
  rc=t->provider->cancel(t->provider->context,t);if(rc<0)return fail(t,rc);
 }
 t->phase=M16_PS_STOPPED;return rc;
}
