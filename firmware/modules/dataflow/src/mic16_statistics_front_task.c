#include "mic16_statistics_front_task.h"
#include "mic16_resample_tables.h"
#include <math.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d535446u
static int valid(const m16_statistics_front_task *t){return t && !((uintptr_t)t%_Alignof(m16_statistics_front_task)) && t->magic==MAGIC;}
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
static int fail(m16_statistics_front_task *t,int error){
 if(t->provider&&t->provider->owns(t->provider->context,t))(void)t->provider->cancel(t->provider->context,t);
 if(t->fir_provider&&t->fir_provider->owns(t->fir_provider->context,t))(void)t->fir_provider->cancel(t->fir_provider->context,t);
 t->error=error;t->phase=M16_SFT_FAILED;return error;
}
static float clean(float f){union{float f;uint32_t u;}v;v.f=f;if(!(v.u&0x7f800000u))v.u&=0x80000000u;return v.f;}
static float muln(float a,float b){return clean(clean(a)*clean(b));}
static float addn(float a,float b){return clean(clean(a)+clean(b));}
static float subn(float a,float b){return clean(clean(a)-clean(b));}
static void one32(m16_statistics_front_task *t,unsigned a,unsigned b,float wr,float wi,unsigned neon){
 float ar=t->real32[a],ai=t->imaginary32[a],br=t->real32[b],bi=t->imaginary32[b],re,im;
 if(neon){
  re=subn(muln(wr,br),muln(wi,bi));im=addn(muln(wr,bi),muln(wi,br));
  t->real32[a]=addn(ar,re);t->imaginary32[a]=addn(ai,im);t->real32[b]=subn(ar,re);t->imaginary32[b]=subn(ai,im);
 }else{
  re=wr*br-wi*bi;im=wr*bi+wi*br;t->real32[a]=ar+re;t->imaginary32[a]=ai+im;t->real32[b]=ar-re;t->imaginary32[b]=ai-im;
 }
}
int m16_statistics_front_init(m16_statistics_front_task *t,mic16_extended_workspace *w){
 unsigned len,k;
 if(!t || !w || (uintptr_t)t%_Alignof(m16_statistics_front_task) || (uintptr_t)w%_Alignof(mic16_extended_workspace) ||
  t->magic || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_SFT_INVALID;
 *t=(m16_statistics_front_task){0};t->magic=MAGIC;t->work=w;
 t->twiddle_real=m16_twiddle_re;t->twiddle_imaginary=m16_twiddle_im;
 for(len=2;len<=256u;len<<=1){unsigned half=len/2u,step=512u/len;
  for(k=0;k<half;k++){t->twiddle_real32[half-1u+k]=(float)t->twiddle_real[k*step];t->twiddle_imaginary32[half-1u+k]=(float)t->twiddle_imaginary[k*step];}
 }return 0;
}
int m16_statistics_front_bind_fft64(m16_statistics_front_task *t,const m16_fft_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->provider||!m16_statistics_front_releasable(t))return M16_SFT_INVALID;
 t->provider=p;return 0;
}
int m16_statistics_front_bind_fir(m16_statistics_front_task *t,const m16_fir_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->fir_provider||
  !apart(t,sizeof(*t),p,sizeof(*p))||!m16_statistics_front_releasable(t))return M16_SFT_INVALID;
 t->fir_provider=p;return 0;
}
int m16_statistics_front_begin(m16_statistics_front_task *t,unsigned mode,const float *x,unsigned values,unsigned candidate){
 unsigned samples,channels;
 if(!valid(t) || !x || mode>2 || (uintptr_t)x%4u || (mode!=M16_SF_BEAM1 && candidate) || candidate>=16)return M16_SFT_INVALID;
 samples=mode==M16_SF_QUIET16?8192u:40960u;channels=mode==M16_SF_BEAM1?1u:16u;
 if(values<samples*channels || !apart(x,(uintptr_t)samples*channels*4u,t,sizeof(*t)) ||
  (!(mode==M16_SF_BEAM1 && x==t->work->physical.beam) && !apart(x,(uintptr_t)samples*channels*4u,t->work,sizeof(*t->work))))return M16_SFT_INVALID;
 if(!m16_statistics_front_releasable(t))return M16_SFT_BUSY;
 if(!environment())return M16_SFT_FLOAT_ENV;
 t->mode=mode;t->input=x;t->samples=samples;t->channels=channels;t->length=(samples*256u+624u)/625u;
 t->frames=mode==M16_SF_QUIET16?15u:67u;t->candidate=candidate;t->phase=M16_SFT_VALIDATE;t->cursor=t->resample_index=0;
 t->error=0;t->steps=0;t->resampled_values=t->FP32_frames=t->FP64_frames=t->vector_groups=t->snapshots=t->last_items=t->max_items=0;return 0;
}
static void begin_fft(m16_statistics_front_task *t){t->cursor=0;t->maximum=0.;t->phase=M16_SFT_LOAD;}
static int transform(m16_statistics_front_task *t){
 unsigned i,end=t->cursor+64u;double *r=t->work->fft_re,*im=t->work->fft_im;
 if(t->phase==M16_SFT_DISPATCH){
  int rc=t->provider->request(t->provider->context,t,r,im,256u,t->twiddle_real,t->twiddle_imaginary,512u,0u);
  if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_SFT_REVERSE;}
  else if(!rc)t->phase=M16_SFT_DEVICE;
  return 1;
 }
 if(t->phase==M16_SFT_DEVICE){
  int rc=t->provider->poll(t->provider->context,t);if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=1;t->reverse=0;t->phase=M16_SFT_REVERSE;}
  else if(!rc){t->cursor=0;t->phase=M16_SFT_RECOVER;}
  return 1;
 }
 if(t->phase==M16_SFT_COMPRESS){
  if(end>256u)end=256u;
  for(i=t->cursor;i<end;i++){
   if(t->fp64){double a=r[2u*i],b=r[2u*i+1u];r[i]=a;im[i]=b;}
   else{t->real32[i]=(float)r[2u*i];t->imaginary32[i]=(float)r[2u*i+1u];}t->last_items++;
  }
  t->cursor=end;if(end==256u){
   t->cursor=t->fp64&&t->provider?0u:1u;t->reverse=0;
   t->phase=t->fp64&&t->provider?M16_SFT_DISPATCH:M16_SFT_REVERSE;
  }return 1;
 }
 if(t->phase==M16_SFT_REVERSE){
  if(end>256u)end=256u;
  for(i=t->cursor;i<end;i++){
   unsigned bit=128u,j=t->reverse;for(;j&bit;bit>>=1)j^=bit;j^=bit;t->reverse=j;
   if(i<j){if(t->fp64){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}
    else{float a=t->real32[i],b=t->imaginary32[i];t->real32[i]=t->real32[j];t->imaginary32[i]=t->imaginary32[j];t->real32[j]=a;t->imaginary32[j]=b;}}
   t->last_items++;
  }
  t->cursor=end;if(end==256u){t->cursor=0;t->fft_length=2;t->phase=M16_SFT_FFT;}return 1;
 }
 if(t->phase==M16_SFT_FFT){
  unsigned half=t->fft_length/2u;end=t->cursor+32u;if(end>128u)end=128u;
  if(!t->fp64 && half>=4u){
   for(i=t->cursor;i<end;i+=4u){
    unsigned k=i%half,a=(i/half)*t->fft_length+k,b=a+half;const float *wr=t->twiddle_real32+half-1u+k,*wi=t->twiddle_imaginary32+half-1u+k;
#if defined(__ARM_NEON)
    float32x4_t ar=vld1q_f32(t->real32+a),ai=vld1q_f32(t->imaginary32+a),br=vld1q_f32(t->real32+b),bi=vld1q_f32(t->imaginary32+b);
    float32x4_t tr=vld1q_f32(wr),ti=vld1q_f32(wi),re=vsubq_f32(vmulq_f32(tr,br),vmulq_f32(ti,bi)),ii=vaddq_f32(vmulq_f32(tr,bi),vmulq_f32(ti,br));
    vst1q_f32(t->real32+a,vaddq_f32(ar,re));vst1q_f32(t->imaginary32+a,vaddq_f32(ai,ii));
    vst1q_f32(t->real32+b,vsubq_f32(ar,re));vst1q_f32(t->imaginary32+b,vsubq_f32(ai,ii));
#else
    unsigned j;for(j=0;j<4u;j++)one32(t,a+j,b+j,wr[j],wi[j],1);
#endif
    t->vector_groups++;t->last_items+=4;
   }
  }else for(i=t->cursor;i<end;i++){
   unsigned k=i%half,a=(i/half)*t->fft_length+k,b=a+half,q=k*(512u/t->fft_length);
   if(t->fp64){double re=t->twiddle_real[q]*r[b]-t->twiddle_imaginary[q]*im[b],ii=t->twiddle_real[q]*im[b]+t->twiddle_imaginary[q]*r[b],ar=r[a],ai=im[a];
    r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;
   }else one32(t,a,b,t->twiddle_real32[half-1u+k],t->twiddle_imaginary32[half-1u+k],0);
   t->last_items++;
  }
  t->cursor=end;if(end==128u){t->cursor=0;t->fft_length<<=1;if(t->fft_length>256u)t->phase=M16_SFT_RECOVER;}return 1;
 }
 if(t->phase==M16_SFT_RECOVER){
  end=t->cursor+32u;if(end>129u)end=129u;
  if(t->cursor==0){
   if(t->fp64){double a=r[0],b=im[0];r[0]=a+b;r[256]=a-b;}
   else{float a=t->real32[0],b=t->imaginary32[0];r[0]=a+b;r[256]=a-b;}
   im[0]=im[256]=0.;t->cursor=1;t->last_items++;
  }
  for(i=t->cursor;i<end;i++){
   unsigned p=256u-i;
   if(t->fp64){double a=r[i],b=im[i],c=r[p],d=im[p],er=(a+c)*.5,ei=(b-d)*.5,orr=(b+d)*.5,oi=(c-a)*.5;
    double x=er+t->twiddle_real[i]*orr-t->twiddle_imaginary[i]*oi,y=ei+t->twiddle_real[i]*oi+t->twiddle_imaginary[i]*orr;
    double u=er+t->twiddle_real[p]*orr+t->twiddle_imaginary[p]*oi,v=-ei-t->twiddle_real[p]*oi+t->twiddle_imaginary[p]*orr;
    r[i]=x;im[i]=y;if(p!=i){r[p]=u;im[p]=v;}
   }else{
    float a=t->real32[i],b=t->imaginary32[i],c=t->real32[p],d=t->imaginary32[p],er=(a+c)*.5f,ei=(b-d)*.5f,orr=(b+d)*.5f,oi=(c-a)*.5f;
    float wr=(float)t->twiddle_real[i],wi=(float)t->twiddle_imaginary[i],wp=(float)t->twiddle_real[p],ip=(float)t->twiddle_imaginary[p];
    r[i]=er+wr*orr-wi*oi;im[i]=ei+wr*oi+wi*orr;if(p!=i){r[p]=er+wp*orr+ip*oi;im[p]=-ei-wp*oi+ip*orr;}
   }t->last_items++;
  }
  t->cursor=end;if(end==129u){t->cursor=1;t->phase=M16_SFT_CONJUGATE;}return 1;
 }
 if(t->phase==M16_SFT_CONJUGATE){
  if(end>256u)end=256u;
  for(i=t->cursor;i<end;i++){r[512u-i]=r[i];im[512u-i]=-im[i];t->last_items++;}
  t->cursor=end;if(end==256u){t->cursor=0;t->phase=M16_SFT_SPECTRUM;}return 1;
 }
 return fail(t,M16_SFT_INVALID);
}
static int step(m16_statistics_front_task *t){
 unsigned i,c,end;mic16_extended_workspace *w=t->work;
 if(t->phase==M16_SFT_COMPLETE)return 0;
 if(t->phase==M16_SFT_STOPPED)return 2;
 if(t->phase==M16_SFT_FAILED)return t->error;
 if(t->phase==M16_SFT_IDLE)return M16_SFT_INVALID;
 if(!environment())return fail(t,M16_SFT_FLOAT_ENV);
 if(t->phase==M16_SFT_FIR_DISPATCH){
  double *output=t->mode==M16_SF_BEAM1?w->resampled:w->observed;
  m16_fir_request q={t->input,output,0u,t->samples,t->channels,0u,t->channels,t->length*t->channels};
  int rc=t->fir_provider->request(t->fir_provider->context,t,&q);if(rc<0)return fail(t,rc);
  if(rc==2){t->resample_index=0;t->phase=M16_SFT_FIR_SETUP;}
  else if(!rc)t->phase=M16_SFT_FIR_DEVICE;
  return 1;
 }
 if(t->phase==M16_SFT_FIR_DEVICE){
  int rc=t->fir_provider->poll(t->fir_provider->context,t);if(rc<0)return fail(t,rc);
  if(rc==2){t->resample_index=0;t->phase=M16_SFT_FIR_SETUP;return 1;}
  if(!rc){
   t->resample_index=t->length;t->resampled_values=t->length*t->channels;
   t->frame=t->mode==M16_SF_OBSERVED16?t->frames-1u:0u;t->channel=t->cursor=0;
   if(t->mode==M16_SF_BEAM1)begin_fft(t);else t->phase=M16_SFT_SNAPSHOT;
  }return 1;
 }
 if((t->phase>=M16_SFT_COMPRESS && t->phase<=M16_SFT_CONJUGATE)||t->phase==M16_SFT_DISPATCH||t->phase==M16_SFT_DEVICE)return transform(t);
 if(t->phase==M16_SFT_VALIDATE){
  end=t->cursor+64u;if(end>t->samples*t->channels)end=t->samples*t->channels;
  for(i=t->cursor;i<end;i++){float x=t->input[i];if(!(x>=-1.f && x<=1.f))return fail(t,M16_SFT_INPUT);t->last_items++;}
  t->cursor=end;if(end==t->samples*t->channels){t->cursor=0;t->phase=t->mode==M16_SF_QUIET16?M16_SFT_CLEAR:(t->fir_provider?M16_SFT_FIR_DISPATCH:M16_SFT_FIR_SETUP);}return 1;
 }
 if(t->phase==M16_SFT_CLEAR){
  end=t->cursor+64u;if(end>15u*64u)end=15u*64u;
  for(i=t->cursor;i<end;i++){w->quiet_rows[i]=0.;t->last_items++;}
  t->cursor=end;if(end==15u*64u){t->cursor=0;t->phase=t->fir_provider?M16_SFT_FIR_DISPATCH:M16_SFT_FIR_SETUP;}return 1;
 }
 if(t->phase==M16_SFT_FIR_SETUP){
  int low;t->time=(t->resample_index+10u)*625u;low=((int)t->time-12500+255)/256;
  if((int)t->time-12500<0 || low<0)low=0;
  t->tap=low;t->high=(int)(t->time/256u);if(t->high>=(int)t->samples)t->high=(int)t->samples-1;
  for(c=0;c<t->channels;c++)t->sum[c]=0.;
  t->phase=M16_SFT_FIR;return 1;
 }
 if(t->phase==M16_SFT_FIR){
  unsigned taps=t->channels==16u?4u:32u;
  for(i=0;i<taps && t->tap<=t->high;i++,t->tap++){
   int h=(int)t->time-t->tap*256;
   if(h>=0 && h<=12500){
    double coefficient=m16_resample_h[h];
    for(c=0;c<t->channels;c++){
     double x=t->input[(unsigned)t->tap*t->channels+c];if(!(x>=-1. && x<=1.))return fail(t,M16_SFT_INPUT);
     t->sum[c]+=x*coefficient;t->last_items++;
    }
   }
  }
  if(t->tap>t->high)t->phase=M16_SFT_FIR_STORE;
  return 1;
 }
 if(t->phase==M16_SFT_FIR_STORE){
  for(c=0;c<t->channels;c++){
   if(!finite64(t->sum[c]))return fail(t,M16_SFT_OUTPUT);
   if(t->mode==M16_SF_BEAM1)w->resampled[t->resample_index]=t->sum[c];
   else w->observed[t->resample_index*16u+c]=t->sum[c];
   t->resampled_values++;t->last_items++;
  }
  if(++t->resample_index==t->length){
   t->frame=t->mode==M16_SF_OBSERVED16?t->frames-1u:0u;t->channel=t->cursor=0;
   if(t->mode==M16_SF_BEAM1)begin_fft(t);else t->phase=M16_SFT_SNAPSHOT;
  }else t->phase=M16_SFT_FIR_SETUP;return 1;
 }
 if(t->phase==M16_SFT_SNAPSHOT){
  end=t->cursor+64u;if(end>512u*16u)end=512u*16u;
  for(i=t->cursor;i<end;i++){
   int pos=(int)(t->frame*256u+i/16u)-256;
   w->resampled[i]=(pos>=0 && pos<(int)t->length)?w->observed[(unsigned)pos*16u+i%16u]:0.;t->last_items++;
  }
  t->cursor=end;if(end==512u*16u){t->snapshots++;begin_fft(t);}return 1;
 }
 if(t->phase==M16_SFT_LOAD){
  end=t->cursor+64u;if(end>512u)end=512u;
  for(i=t->cursor;i<end;i++){
   int pos=(int)(t->frame*256u+i)-256;double x=0.,a;
   if(pos>=0 && pos<(int)t->length)x=(t->mode==M16_SF_BEAM1?w->resampled[(unsigned)pos]:w->resampled[i*16u+t->channel])*m16_periodic_hann512[i];
   if(!finite64(x))return fail(t,M16_SFT_INPUT);
   w->fft_re[i]=x;w->fft_im[i]=0.;a=x<0.?-x:x;if(a>t->maximum)t->maximum=a;t->last_items++;
  }
  t->cursor=end;if(end==512u){
   t->fp64=t->maximum!=0. && (t->maximum<1e-15 || t->maximum>2.);
   if(t->fp64)t->FP64_frames++;else t->FP32_frames++;
   t->cursor=0;t->phase=M16_SFT_COMPRESS;
  }return 1;
 }
 if(t->phase==M16_SFT_SPECTRUM){
  end=t->cursor+64u;if(end>257u)end=257u;
  for(i=t->cursor;i<end;i++){
   double a=w->fft_re[i],b=w->fft_im[i];if(!finite64(a) || !finite64(b))return fail(t,M16_SFT_OUTPUT);
   w->spectrum[i*2u]=a;w->spectrum[i*2u+1u]=b;t->last_items++;
  }
  t->cursor=end;if(end==257u){t->cursor=0;t->phase=M16_SFT_CONSUME;}return 1;
 }
 if(t->phase==M16_SFT_CONSUME){
  if(t->mode==M16_SF_OBSERVED16){
   end=t->cursor+64u;if(end>257u)end=257u;
   for(i=t->cursor;i<end;i++){
    unsigned q=((t->frame*257u+i)*16u+t->channel)*2u;
    w->observed[q]=w->spectrum[i*2u];w->observed[q+1u]=w->spectrum[i*2u+1u];t->last_items++;
   }
   t->cursor=end;if(end==257u)t->phase=M16_SFT_NEXT;return 1;
  }
  end=t->cursor+4u;if(end>64u)end=64u;
  for(i=t->cursor;i<end;i++){
   unsigned k;double power=0.;
   for(k=0;k<4;k++){
    unsigned b=1u+i*4u+k;double re=w->spectrum[b*2u],im=w->spectrum[b*2u+1u],p=re*re+im*im;
    if(t->mode==M16_SF_QUIET16)w->quiet_rows[t->frame*64u+i]+=p;else power+=p;t->last_items++;
   }
   if(t->mode==M16_SF_BEAM1)w->beam_power[(t->candidate*67u+t->frame)*64u+i]=power/4.;
  }
  t->cursor=end;if(end==64u)t->phase=M16_SFT_NEXT;return 1;
 }
 if(t->phase==M16_SFT_NEXT){
  t->cursor=0;
  if(++t->channel<t->channels){begin_fft(t);return 1;}
  t->channel=0;
  if(t->mode==M16_SF_OBSERVED16){
   if(t->frame==0){t->phase=M16_SFT_CHANNEL_POWER;return 1;}
   t->frame--;t->phase=M16_SFT_SNAPSHOT;return 1;
  }
  if(++t->frame<t->frames){if(t->mode==M16_SF_BEAM1)begin_fft(t);else t->phase=M16_SFT_SNAPSHOT;return 1;}
  if(t->mode==M16_SF_QUIET16){t->phase=M16_SFT_MEDIAN;return 1;}
  t->phase=M16_SFT_COMPLETE;return 0;
 }
 if(t->phase==M16_SFT_MEDIAN){
  double a[15];unsigned j;
  for(i=0;i<15u;i++)a[i]=w->quiet_rows[i*64u+t->cursor]/64.;
  for(i=1;i<15u;i++){double v=a[i];j=i;while(j>0 && a[j-1u]>v){a[j]=a[j-1u];j--;}a[j]=v;}
  if(!finite64(a[7]))return fail(t,M16_SFT_OUTPUT);
  w->quiet_power[t->cursor]=a[7];t->last_items=15;if(++t->cursor==64u){t->phase=M16_SFT_COMPLETE;return 0;}return 1;
 }
 if(t->phase==M16_SFT_CHANNEL_POWER){
  unsigned frame=t->cursor/64u,z=t->cursor%64u,k;double power=0.;
  for(k=0;k<4u;k++)for(c=0;c<16u;c++){
   unsigned q=((frame*257u+1u+z*4u+k)*16u+c)*2u;double re=w->observed[q],im=w->observed[q+1u];
   power+=re*re+im*im;t->last_items++;
  }
  if(!finite64(power))return fail(t,M16_SFT_OUTPUT);
  w->channel_power[t->cursor]=power/64.;if(++t->cursor==67u*64u){t->phase=M16_SFT_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_SFT_INVALID);
}
int m16_statistics_front_step(m16_statistics_front_task *t){
 int rc;if(!valid(t))return M16_SFT_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_statistics_front_stop(m16_statistics_front_task *t){
 int rc=1;if(!valid(t))return M16_SFT_INVALID;
 if(t->provider&&t->provider->owns(t->provider->context,t))rc=t->provider->cancel(t->provider->context,t);
 if(t->fir_provider&&t->fir_provider->owns(t->fir_provider->context,t)){
  int other=t->fir_provider->cancel(t->fir_provider->context,t);if(other<0||rc>0)rc=other;
 }
 if(rc<0){t->error=rc;t->phase=M16_SFT_FAILED;return rc;}
 t->phase=M16_SFT_STOPPED;return rc;
}
int m16_statistics_front_releasable(const m16_statistics_front_task *t){
 return valid(t) && (t->phase==M16_SFT_IDLE || t->phase==M16_SFT_COMPLETE || t->phase==M16_SFT_STOPPED || t->phase==M16_SFT_FAILED) &&
  (!t->provider||!t->provider->owns(t->provider->context,t)) &&
  (!t->fir_provider||!t->fir_provider->owns(t->fir_provider->context,t));
}
