#include "mic16_resample_task.h"
#include "mic16_resample_tables.h"
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d525331u
static int valid(const m16_resample_task *t){return t && !((uintptr_t)t%_Alignof(m16_resample_task)) && t->magic==MAGIC;}
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
static int fail(m16_resample_task *t,int rc){
 if(t->provider&&t->provider->owns(t->provider->context,t))(void)t->provider->cancel(t->provider->context,t);
 t->error=rc;t->phase=M16_RS_FAILED;return rc;
}
int m16_resample_task_init(m16_resample_task *t){
 if(!t || (uintptr_t)t%_Alignof(m16_resample_task) || t->magic || !environment())return M16_RS_INVALID;
 *t=(m16_resample_task){0};t->magic=MAGIC;return 0;
}
int m16_resample_task_releasable(const m16_resample_task *t){
 return valid(t) && (t->phase==M16_RS_IDLE || t->phase==M16_RS_COMPLETE || t->phase==M16_RS_STOPPED || t->phase==M16_RS_FAILED)&&(!t->provider||!t->provider->owns(t->provider->context,t));
}
int m16_resample_task_bind(m16_resample_task *t,const m16_fir_provider *p){
 if(!valid(t)||!p||!p->context||!p->request||!p->poll||!p->cancel||!p->owns||t->provider||
  !apart(t,sizeof(*t),p,sizeof(*p))||!m16_resample_task_releasable(t))return M16_RS_INVALID;
 t->provider=p;return 0;
}
static int begin(m16_resample_task *t,const void *x,unsigned n,unsigned stride,unsigned channel,double *out,unsigned capacity,unsigned kind){
 unsigned length;
 if(!valid(t) || !x || !out || !n || n>(kind?45680u:40960u) || !stride || stride>16u || channel>=stride ||
  (kind && (stride!=1u || channel)) || (uintptr_t)x%(kind?8u:4u) || (uintptr_t)out%8u)return M16_RS_INVALID;
 length=(n*256u+624u)/625u;
 if(capacity<length || !apart(x,(uintptr_t)n*stride*(kind?8u:4u),out,(uintptr_t)length*8u) ||
  !apart(x,(uintptr_t)n*stride*(kind?8u:4u),t,sizeof(*t)) || !apart(out,(uintptr_t)length*8u,t,sizeof(*t)))return M16_RS_INVALID;
 if(!m16_resample_task_releasable(t))return M16_RS_BUSY;
 if(!environment())return M16_RS_FLOAT_ENV;
 t->kind=kind;t->input32=kind?0:(const float *)x;t->input64=kind?(const double *)x:0;t->output=out;t->samples=n;t->stride=stride;t->channel=channel;t->length=length;
 t->phase=M16_RS_VALIDATE;t->cursor=t->last_items=t->max_items=0;t->steps=0;t->error=0;return 0;
}
int m16_resample_task_begin32(m16_resample_task *t,const float *x,unsigned n,unsigned stride,unsigned channel,double *out,unsigned capacity){return begin(t,x,n,stride,channel,out,capacity,0);}
int m16_resample_task_begin64(m16_resample_task *t,const double *x,unsigned n,double *out,unsigned capacity){return begin(t,x,n,1,0,out,capacity,1);}
static double sample(const m16_resample_task *t,unsigned i){return t->kind?t->input64[i]:(double)t->input32[i*t->stride+t->channel];}
static int acceptable(const m16_resample_task *t,double x){return t->kind?finite64(x):(x>=-1. && x<=1.);}
static int step(m16_resample_task *t){
 unsigned i,end;
 if(t->phase==M16_RS_COMPLETE)return 0;
 if(t->phase==M16_RS_STOPPED)return 2;
 if(t->phase==M16_RS_FAILED)return t->error;
 if(t->phase==M16_RS_IDLE)return M16_RS_INVALID;
 if(!environment())return fail(t,M16_RS_FLOAT_ENV);
 if(t->phase==M16_RS_VALIDATE){
  end=t->cursor+64u;if(end>t->samples)end=t->samples;
  for(i=t->cursor;i<end;i++){if(!acceptable(t,sample(t,i)))return fail(t,M16_RS_INPUT);t->last_items++;}
  t->cursor=end;if(end==t->samples){t->cursor=0;t->phase=t->provider?M16_RS_DISPATCH:M16_RS_FILTER;}return 1;
 }
 if(t->phase==M16_RS_DISPATCH){
  m16_fir_request q={t->kind?(const void *)t->input64:(const void *)t->input32,t->output,
    t->kind,t->samples,t->stride,t->channel,1u,t->length};
  int rc=t->provider->request(t->provider->context,t,&q);
  if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=0;t->phase=M16_RS_FILTER;}
  else if(!rc)t->phase=M16_RS_DEVICE;
  return 1;
 }
 if(t->phase==M16_RS_DEVICE){
  int rc=t->provider->poll(t->provider->context,t);if(rc<0)return fail(t,rc);
  if(rc==2){t->cursor=0;t->phase=M16_RS_FILTER;return 1;}
  if(!rc){t->cursor=t->length;t->phase=M16_RS_COMPLETE;return 0;}
  return 1;
 }
 if(t->phase==M16_RS_FILTER){
  int time=((int)t->cursor+10)*625,low=(time-12500+255)/256,high=time/256,k;double sum=0.;
  if(time-12500<0 || low<0)low=0;
  if(high>=(int)t->samples)high=(int)t->samples-1;
  for(k=low;k<=high;k++){
   int h=time-k*256;
   if(h>=0 && h<=12500){double x=sample(t,(unsigned)k);if(!acceptable(t,x))return fail(t,M16_RS_INPUT);sum+=x*m16_resample_h[h];t->last_items++;}
  }
  if(!finite64(sum))return fail(t,M16_RS_OUTPUT);
  t->output[t->cursor]=sum;
  if(++t->cursor==t->length){t->phase=M16_RS_COMPLETE;return 0;}return 1;
 }
 return fail(t,M16_RS_INVALID);
}
int m16_resample_task_step(m16_resample_task *t){
 int rc;if(!valid(t))return M16_RS_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_resample_task_stop(m16_resample_task *t){
 int rc=1;if(!valid(t))return M16_RS_INVALID;
 if(t->provider&&t->provider->owns(t->provider->context,t))rc=t->provider->cancel(t->provider->context,t);
 if(rc<0){t->error=rc;t->phase=M16_RS_FAILED;return rc;}
 t->phase=M16_RS_STOPPED;return rc;
}
