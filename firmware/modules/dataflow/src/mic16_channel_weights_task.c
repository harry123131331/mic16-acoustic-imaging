#include "mic16_channel_weights_task.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d435731u
#define PI 3.14159265358979323846264338327950288
static int valid(const m16_channel_weights_task *t){return t && !((uintptr_t)t%_Alignof(m16_channel_weights_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
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
static int fail(m16_channel_weights_task *t,int rc){t->error=rc;t->phase=M16_CW_FAILED;return rc;}
static unsigned ca(unsigned p){return p<16u?p:(p<24u?p-16u:p-24u);}
static unsigned cb(unsigned p){return p<16u?(p+1u)%16u:(p<24u?p-8u:p-20u);}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static float sumf(const float *a,int n){
 float r[8],s=-0.f;int i,j;
 if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
 for(j=0;j<8;j++)r[j]=a[j];
 for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
 s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
 for(;i<n;i++)s+=a[i];
 return s;
}
int m16_channel_weights_init(m16_channel_weights_task *t,m16_enh_workspace *w){
 unsigned i;int rc;
 if(!t || !w || (uintptr_t)t%_Alignof(m16_channel_weights_task) || (uintptr_t)w%_Alignof(m16_enh_workspace) ||
  t->magic || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_CW_INVALID;
 *t=(m16_channel_weights_task){0};
 for(i=0;i<2048u;i++){w->twiddle_re[i]=cos(2.*PI*i/4096.);w->twiddle_im[i]=-sin(2.*PI*i/4096.);}
 rc=m16_complex_fft_init(&t->fft,w->fft_re,w->fft_im,4096,w->twiddle_re,w->twiddle_im,4096);if(rc)return rc;
 t->magic=MAGIC;t->work=w;return 0;
}
int m16_channel_weights_releasable(const m16_channel_weights_task *t){
 return valid(t) && (t->phase==M16_CW_IDLE || t->phase==M16_CW_COMPLETE || t->phase==M16_CW_STOPPED || t->phase==M16_CW_FAILED) && m16_complex_fft_releasable(&t->fft);
}
int m16_channel_weights_begin(m16_channel_weights_task *t,const float *x,unsigned n){
 if(!valid(t) || !x || (uintptr_t)x%4u || n<40960u*16u || !apart(x,40960u*16u*4u,t,sizeof(*t)) ||
  !apart(x,40960u*16u*4u,t->work,sizeof(*t->work)))return M16_CW_INVALID;
 if(!m16_channel_weights_releasable(t))return M16_CW_BUSY;
 if(!environment())return M16_CW_FLOAT_ENV;
 t->pcm=x;t->phase=M16_CW_VALIDATE;t->frame=t->channel=t->pair=t->cursor=t->last_items=t->max_items=0;
 t->error=0;t->steps=t->forward_transforms=t->inverse_transforms=0;t->mean=t->mean2=0.f;return 0;
}
static int step(m16_channel_weights_task *t){
 unsigned i,end;int rc;m16_enh_workspace *w=t->work;
 if(t->phase==M16_CW_COMPLETE)return 0;
 if(t->phase==M16_CW_STOPPED)return 2;
 if(t->phase==M16_CW_FAILED)return t->error;
 if(t->phase==M16_CW_IDLE)return M16_CW_INVALID;
 if(!environment()){(void)m16_complex_fft_stop(&t->fft);return fail(t,M16_CW_FLOAT_ENV);}
 if(t->phase==M16_CW_VALIDATE){
  end=t->cursor+64u;if(end>40960u*16u)end=40960u*16u;
  for(i=t->cursor;i<end;i++){if(!(t->pcm[i]>=-1.f && t->pcm[i]<=1.f))return fail(t,M16_CW_INPUT);t->last_items++;}
  t->cursor=end;if(end==40960u*16u){t->cursor=0;t->phase=M16_CW_MEAN;}return 1;
 }
 if(t->phase==M16_CW_MEAN || t->phase==M16_CW_CENTER1){
  end=t->cursor+64u;if(end>2048u)end=2048u;
  for(i=t->cursor;i<end;i++){
   float x=t->pcm[(t->frame*2048u+i)*16u+t->channel];
   if(!(x>=-1.f && x<=1.f))return fail(t,M16_CW_INPUT);
   if(t->phase==M16_CW_MEAN)t->mean+=x;
   else{float v=x-t->mean;w->fft_re[i]=(double)v;t->mean2+=v;}
   t->last_items++;
  }
  t->cursor=end;if(end==2048u){
   t->cursor=0;if(t->phase==M16_CW_MEAN){t->mean/=2048.f;t->mean2=0.f;t->phase=M16_CW_CENTER1;}
   else{t->mean2/=2048.f;t->phase=M16_CW_CENTER2;}
  }return 1;
 }
 if(t->phase==M16_CW_CENTER2){
  end=t->cursor+64u;if(end>4096u)end=4096u;
  for(i=t->cursor;i<end;i++){w->fft_re[i]=i<2048u?(double)((float)w->fft_re[i]-t->mean2):0.;w->fft_im[i]=0.;t->last_items++;}
  t->cursor=end;if(end==4096u){rc=m16_complex_fft_begin(&t->fft,4096,0);if(rc)return fail(t,rc);t->forward_transforms++;t->phase=M16_CW_FFT;}return 1;
 }
 if(t->phase==M16_CW_FFT || t->phase==M16_CW_IFFT){
  rc=m16_complex_fft_step(&t->fft);t->last_items=t->fft.last_items;
  if(rc<0)return fail(t,rc);
  if(rc==1)return 1;
  if(rc==2)return fail(t,M16_CW_INPUT);
  t->cursor=0;t->phase=t->phase==M16_CW_FFT?M16_CW_SPECTRUM:M16_CW_RELIABILITY;return 1;
 }
 if(t->phase==M16_CW_SPECTRUM){
  end=t->cursor+64u;if(end>2049u)end=2049u;
  for(i=t->cursor;i<end;i++){w->gcc_spectra[(t->channel*2049u+i)*2u]=w->fft_re[i];w->gcc_spectra[(t->channel*2049u+i)*2u+1u]=w->fft_im[i];t->last_items++;}
  t->cursor=end;if(end==2049u){
   t->cursor=0;t->mean=0.f;
   if(++t->channel==16u){t->channel=t->pair=0;t->phase=M16_CW_PAIRS;}
   else t->phase=M16_CW_MEAN;
  }return 1;
 }
 if(t->phase==M16_CW_PAIRS){
  end=t->cursor+32u;if(end>2049u)end=2049u;
  for(i=t->cursor;i<end;i++){
   unsigned a=(ca(t->pair)*2049u+i)*2u,b=(cb(t->pair)*2049u+i)*2u;
   double ar=w->gcc_spectra[a],ai=w->gcc_spectra[a+1u],br=w->gcc_spectra[b],bi=w->gcc_spectra[b+1u];
   double re=ar*br+ai*bi,im=ai*br-ar*bi,den=hypot(re,im)+1e-8;
   w->fft_re[i]=re/den;w->fft_im[i]=im/den;t->last_items++;
  }
  t->cursor=end;if(end==2049u){t->phase=M16_CW_CONJUGATE;}return 1;
 }
 if(t->phase==M16_CW_CONJUGATE){
  end=t->cursor+64u;if(end>4096u)end=4096u;
  for(i=t->cursor;i<end;i++){w->fft_re[i]=w->fft_re[4096u-i];w->fft_im[i]=-w->fft_im[4096u-i];t->last_items++;}
  t->cursor=end;if(end==4096u){rc=m16_complex_fft_begin(&t->fft,4096,1);if(rc)return fail(t,rc);t->inverse_transforms++;t->phase=M16_CW_IFFT;}return 1;
 }
 if(t->phase==M16_CW_RELIABILITY){
  float curve[41],side[41],variance[41],energy[41],mean,var,psr,sharp,second,rv;int k,best=0,n=0;
  for(k=0;k<41;k++){unsigned q=(4096u+20u-(unsigned)k)%4096u;curve[k]=(float)w->fft_re[q];if(curve[k]>curve[best])best=k;}
  for(k=0;k<41;k++){energy[k]=curve[k]*curve[k];if(k<best-1 || k>best+1)side[n++]=curve[k];}
  mean=sumf(side,n)/(float)n;second=side[0];
  for(k=0;k<n;k++){float d=side[k]-mean;variance[k]=d*d;if(side[k]>second)second=side[k];}
  var=sumf(variance,n)/(float)n;
  psr=(float)(((double)curve[best]-(double)mean)/(sqrt((double)var+1e-8)+1e-6));
  sharp=curve[best]-second;rv=(.02f*psr+.3f*sharp)+.7f;
  t->reliability[t->pair]=sumf(energy,41)>1e-6f?(float)clip(rv,.05,1.):0.f;t->last_items=164;t->cursor=0;
  if(++t->pair==32u)t->phase=M16_CW_ACCUMULATE;else t->phase=M16_CW_PAIRS;return 1;
 }
 if(t->phase==M16_CW_ACCUMULATE){
  for(i=0;i<16u;i++)t->acc[i]=t->counts[i]=0.f;
  for(i=0;i<32u;i++){unsigned a=ca(i),b=cb(i);t->acc[a]+=t->reliability[i];t->acc[b]+=t->reliability[i];t->counts[a]+=1.f;t->counts[b]+=1.f;}
  t->last_items=128;t->phase=M16_CW_NORMALIZE;return 1;
 }
 if(t->phase==M16_CW_NORMALIZE){
  float den;
  for(i=0;i<16u;i++)t->acc[i]/=t->counts[i];
  den=sumf(t->acc,16);
  if(den<=1e-8f){for(i=0;i<16u;i++)t->acc[i]=1.f;den=16.f;}
  for(i=0;i<16u;i++){float v=t->acc[i]/den;if(!(v>=0.f && v<=1.f))return fail(t,M16_CW_OUTPUT);w->weights[t->frame*16u+i]=v;}
  t->last_items=48;t->cursor=t->channel=0;t->mean=0.f;
  if(++t->frame==20u){t->phase=M16_CW_COMPLETE;return 0;}
  t->phase=M16_CW_MEAN;return 1;
 }
 return fail(t,M16_CW_INVALID);
}
int m16_channel_weights_step(m16_channel_weights_task *t){
 int rc;if(!valid(t))return M16_CW_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_channel_weights_stop(m16_channel_weights_task *t){
 if(!valid(t))return M16_CW_INVALID;
 (void)m16_complex_fft_stop(&t->fft);t->phase=M16_CW_STOPPED;return 1;
}
