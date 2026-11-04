#include "mic16_candidate_beam_task.h"
#include "mic16_physical.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d424541u
static int valid(const m16_candidate_beam_task *t){return t && !((uintptr_t)t%_Alignof(m16_candidate_beam_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
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
static int fail(m16_candidate_beam_task *t,int e){t->phase=M16_BT_FAILED;t->error=e;return e;}
int m16_candidate_beam_init(m16_candidate_beam_task *t){
 if(!t || (uintptr_t)t%_Alignof(m16_candidate_beam_task) || t->magic)return M16_BT_INVALID;
 *t=(m16_candidate_beam_task){0};t->magic=MAGIC;return 0;
}
int m16_candidate_beam_begin(m16_candidate_beam_task *t,const float *pcm,unsigned values,unsigned n,
 const double *direction,float *beam,unsigned beam_values){
 float arrival[16],lags[32];double minshift=0.;unsigned c,indexed=1;
 if(!valid(t) || !pcm || !beam || !direction || (uintptr_t)pcm%4u || (uintptr_t)beam%4u ||
  (uintptr_t)direction%8u || (n!=8192u && n!=40960u) || values<n*16u || beam_values<n ||
  !apart(pcm,n*16u*4u,beam,n*4u) || !apart(t,sizeof(*t),pcm,n*16u*4u) ||
  !apart(t,sizeof(*t),beam,n*4u) || !apart(t,sizeof(*t),direction,16u))return M16_BT_INVALID;
 if(!m16_candidate_beam_releasable(t))return M16_BT_BUSY;
 if(!environment())return M16_BT_FLOAT_ENV;
 if(mic16_candidate_geometry(direction,arrival,lags))return M16_BT_INPUT;
 for(c=0;c<16;c++){
  double shift=(double)arrival[c]-10.,scaled;
  if(shift>0. || shift<-(double)n)return M16_BT_INPUT;
  if(shift<minshift)minshift=shift;
  t->shifts[c]=shift;scaled=shift*0x1p37;
  if(scaled!=(double)(int64_t)scaled)indexed=0;
  t->offsets[c]=(int)floor(shift);t->fractions[c]=shift-(double)t->offsets[c];
 }
 t->pcm=pcm;t->beam=beam;t->samples=n;t->guard=(unsigned)ceil(-minshift)+2u;t->indexed=indexed;
 t->cursor=t->last_items=t->max_items=0;t->steps=0;t->error=0;t->phase=M16_BT_VALIDATE;return 0;
}
int m16_candidate_beam_step(m16_candidate_beam_task *t){
 unsigned i,end,c;
 if(!valid(t))return M16_BT_INVALID;
 t->last_items=0;t->steps++;
 if(t->phase==M16_BT_COMPLETE)return 0;
 if(t->phase==M16_BT_STOPPED)return 2;
 if(t->phase==M16_BT_FAILED)return t->error;
 if(t->phase==M16_BT_IDLE)return M16_BT_INVALID;
 if(!environment())return fail(t,M16_BT_FLOAT_ENV);
 if(t->phase==M16_BT_VALIDATE){
  end=t->cursor+64u;if(end>t->samples*16u)end=t->samples*16u;
  for(i=t->cursor;i<end;i++){float x=t->pcm[i];if(!(x>=-1.f && x<=1.f))return fail(t,M16_BT_INPUT);t->last_items++;}
  t->cursor=end;if(end==t->samples*16u){t->cursor=0;t->phase=M16_BT_PRODUCE;}
 }else if(t->phase==M16_BT_PRODUCE){
  end=t->cursor+4u;if(end>t->samples)end=t->samples;
  for(i=t->cursor;i<end;i++){
   float aligned[16],r[8];
   if(i<t->guard){t->beam[i]=0.f;t->last_items++;continue;}
   for(c=0;c<16;c++){
    unsigned lo;double f,a,b,y;
    if(t->indexed){lo=(unsigned)((int)i+t->offsets[c]);f=t->fractions[c];}
    else{double q=(double)i+t->shifts[c];lo=(unsigned)floor(q);f=q-(double)lo;}
    if(lo>=t->samples || (f!=0. && lo+1u>=t->samples))return fail(t,M16_BT_INPUT);
    a=t->pcm[lo*16u+c];if(!(a>=-1. && a<=1.))return fail(t,M16_BT_INPUT);y=a;
    if(f!=0.){b=t->pcm[(lo+1u)*16u+c];if(!(b>=-1. && b<=1.))return fail(t,M16_BT_INPUT);y+=(b-a)*f;}
    aligned[c]=(float)y;t->last_items++;
   }
   for(c=0;c<8;c++)r[c]=aligned[c]+aligned[c+8];
   t->beam[i]=(((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7])))/16.f;
  }
  t->cursor=end;if(end==t->samples)t->phase=M16_BT_COMPLETE;
 }else return fail(t,M16_BT_INVALID);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return t->phase==M16_BT_COMPLETE?0:1;
}
int m16_candidate_beam_stop(m16_candidate_beam_task *t){if(!valid(t))return M16_BT_INVALID;t->phase=M16_BT_STOPPED;return 1;}
int m16_candidate_beam_releasable(const m16_candidate_beam_task *t){
 return valid(t) && (t->phase==M16_BT_IDLE || t->phase==M16_BT_COMPLETE || t->phase==M16_BT_STOPPED || t->phase==M16_BT_FAILED);
}
