#include "mic16_weighted_beam_task.h"
#include "mic16_physical.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d574231u
static int valid(const m16_weighted_beam_task *t){return t && !((uintptr_t)t%_Alignof(m16_weighted_beam_task)) && t->magic==MAGIC;}
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
static int fail(m16_weighted_beam_task *t,int rc){t->error=rc;t->phase=M16_WB_FAILED;return rc;}
int m16_weighted_beam_init(m16_weighted_beam_task *t){
 if(!t || (uintptr_t)t%_Alignof(m16_weighted_beam_task) || t->magic || !environment())return M16_WB_INVALID;
 *t=(m16_weighted_beam_task){0};t->magic=MAGIC;return 0;
}
int m16_weighted_beam_releasable(const m16_weighted_beam_task *t){
 return valid(t) && (t->phase==M16_WB_IDLE || t->phase==M16_WB_COMPLETE || t->phase==M16_WB_STOPPED || t->phase==M16_WB_FAILED);
}
int m16_weighted_beam_begin(m16_weighted_beam_task *t,const float *pcm,const float *weights,const double *d,double *out){
 float arrivals[16],lags[32],coefficient[16][6];int delay[16];unsigned c,k,m,i,j;
 const void *p[5]={t,pcm,weights,d,out};uintptr_t size[5]={sizeof(*t),40960u*16u*4u,320u*4u,16u,40960u*8u};
 if(!valid(t) || !pcm || !weights || !d || !out || (uintptr_t)pcm%4u || (uintptr_t)weights%4u || (uintptr_t)d%8u || (uintptr_t)out%8u)return M16_WB_INVALID;
 for(i=0;i<5;i++)for(j=i+1;j<5;j++)if(!apart(p[i],size[i],p[j],size[j]))return M16_WB_INVALID;
 if(!m16_weighted_beam_releasable(t))return M16_WB_BUSY;
 if(!environment())return M16_WB_FLOAT_ENV;
 if(!finite64(d[0]) || !finite64(d[1]) || mic16_candidate_geometry(d,arrivals,lags))return M16_WB_INPUT;
 for(c=0;c<16;c++){
  double compensation=10.-(double)arrivals[c],fraction;
  delay[c]=(int)floor(compensation);fraction=compensation-(double)delay[c];
  if(delay[c]<0 || delay[c]>20)return M16_WB_INPUT;
  for(k=0;k<6;k++){
   double v=1.;for(m=0;m<6;m++)if(m!=k)v*=(fraction-(double)m)/(double)((int)k-(int)m);
   coefficient[c][k]=(float)v;
  }
 }
 for(c=0;c<16;c++){t->delay[c]=delay[c];for(k=0;k<6;k++)t->coefficient[c][k]=coefficient[c][k];}
 t->pcm=pcm;t->weights=weights;t->output=out;t->phase=M16_WB_VALIDATE_PCM;
 t->cursor=t->channel=t->last_items=t->max_items=0;t->total=0.;t->steps=0;t->error=0;return 0;
}
static int step(m16_weighted_beam_task *t){
 unsigned end,i,c,k;
 if(t->phase==M16_WB_COMPLETE)return 0;
 if(t->phase==M16_WB_STOPPED)return 2;
 if(t->phase==M16_WB_FAILED)return t->error;
 if(t->phase==M16_WB_IDLE)return M16_WB_INVALID;
 if(!environment())return fail(t,M16_WB_FLOAT_ENV);
 if(t->phase==M16_WB_VALIDATE_PCM || t->phase==M16_WB_VALIDATE_WEIGHTS){
  unsigned n=t->phase==M16_WB_VALIDATE_PCM?40960u*16u:320u;const float *x=t->phase==M16_WB_VALIDATE_PCM?t->pcm:t->weights;
  end=t->cursor+64u;if(end>n)end=n;
  for(i=t->cursor;i<end;i++){
   if(t->phase==M16_WB_VALIDATE_PCM?!(x[i]>=-1.f && x[i]<=1.f):!(x[i]>=0.f && x[i]<=1.f))return fail(t,M16_WB_INPUT);
   t->last_items++;
  }
  t->cursor=end;if(end==n){t->cursor=0;t->phase=t->phase==M16_WB_VALIDATE_PCM?M16_WB_VALIDATE_WEIGHTS:M16_WB_FILTER;}return 1;
 }
 if(t->phase==M16_WB_FILTER){
  for(c=t->channel;c<t->channel+8u;c++){
   double y=0.;float weight=t->weights[(t->cursor/2048u)*16u+c];
   if(!(weight>=0.f && weight<=1.f))return fail(t,M16_WB_INPUT);
   for(k=0;k<6;k++){
    int pos=(int)t->cursor-t->delay[c]-(int)k;
    if(pos>=0){float x=t->pcm[(unsigned)pos*16u+c];if(!(x>=-1.f && x<=1.f))return fail(t,M16_WB_INPUT);y+=(double)t->coefficient[c][k]*(double)x;}
    t->last_items++;
   }
   t->total+=y*(double)weight;t->last_items++;
  }
  t->channel+=8u;
  if(t->channel==16u){
   if(!finite64(t->total))return fail(t,M16_WB_OUTPUT);
   t->output[t->cursor]=t->total;t->total=0.;t->channel=0;
   if(++t->cursor==40960u){t->phase=M16_WB_COMPLETE;return 0;}
  }
  return 1;
 }
 return fail(t,M16_WB_INVALID);
}
int m16_weighted_beam_step(m16_weighted_beam_task *t){
 int rc;if(!valid(t))return M16_WB_INVALID;t->last_items=0;t->steps++;rc=step(t);if(t->last_items>t->max_items)t->max_items=t->last_items;return rc;
}
int m16_weighted_beam_stop(m16_weighted_beam_task *t){if(!valid(t))return M16_WB_INVALID;t->phase=M16_WB_STOPPED;return 1;}
