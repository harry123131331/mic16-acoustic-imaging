#include "mic16_frozen_score_task.h"
#include "mic16_frozen_weights.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d4e5431u
static int valid(const m16_frozen_score_task *t){return t && !((uintptr_t)t%_Alignof(m16_frozen_score_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static int finite32(float x){return x==x && x<=3.402823466e38f && x>=-3.402823466e38f;}
static float relu(float x){return x>0.f?x:0.f;}
static float absolute(float x){return x<0.f?-x:x;}
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
static int fail(m16_frozen_score_task *t,int rc){t->phase=M16_NT_FAILED;t->error=rc;return rc;}
static float mean20(const float *x,int stride){int i;float s=0.f;for(i=0;i<20;i++)s+=x[i*stride];return s/20.f;}
static float std20(const float *x,int stride){int i;float m=mean20(x,stride),s=0.f;for(i=0;i<20;i++){float d=x[i*stride]-m;s+=d*d;}return sqrtf(s/20.f);}
int m16_frozen_score_task_init(m16_frozen_score_task *t,mic16_score_workspace *w){
 if(!t || !w || (uintptr_t)t%_Alignof(m16_frozen_score_task) || (uintptr_t)w%_Alignof(mic16_score_workspace) ||
  t->magic || !apart(t,sizeof(*t),w,sizeof(*w)) || !environment())return M16_NT_INVALID;
 *t=(m16_frozen_score_task){0};t->magic=MAGIC;t->work=w;t->neighbor_limit=cos((15.0+1e-9)*0.017453292519943295);return 0;
}
int m16_frozen_score_task_releasable(const m16_frozen_score_task *t){
 return valid(t) && (t->phase==M16_NT_IDLE || t->phase==M16_NT_COMPLETE || t->phase==M16_NT_STOPPED || t->phase==M16_NT_FAILED);
}
int m16_frozen_score_task_begin(m16_frozen_score_task *t,const float *x,unsigned xn,const double *d,const unsigned char *v,float *logits,double *p){
 const void *a[7];uintptr_t n[7];unsigned i,j;
 if(!valid(t) || !x || !d || !v || !logits || !p || xn<16u*20u*130u || (uintptr_t)x%4u ||
  (uintptr_t)d%8u || (uintptr_t)logits%4u || (uintptr_t)p%8u)return M16_NT_INVALID;
 if(!m16_frozen_score_task_releasable(t))return M16_NT_BUSY;
 if(!environment())return M16_NT_FLOAT_ENV;
 a[0]=t;a[1]=t->work;a[2]=x;a[3]=d;a[4]=v;a[5]=logits;a[6]=p;
 n[0]=sizeof(*t);n[1]=sizeof(*t->work);n[2]=16u*20u*130u*4u;n[3]=32u*8u;n[4]=16u;n[5]=16u*4u;n[6]=16u*8u;
 for(i=0;i<7;i++)for(j=i+1;j<7;j++)if(!apart(a[i],n[i],a[j],n[j]))return M16_NT_INVALID;
 t->features=x;t->directions=d;t->valid_candidates=v;t->logits=logits;t->probabilities=p;
 t->phase=M16_NT_VALIDATE_X;t->candidate=t->cursor=t->layer=t->last_items=t->max_items=0;t->steps=0;t->error=0;return 0;
}
static int step(m16_frozen_score_task *t){
 unsigned i,end;mic16_score_workspace *w=t->work;const float *x=t->features+t->candidate*20u*130u;float *own=w->own+t->candidate*71u;
 if(t->phase==M16_NT_COMPLETE)return 0;
 if(t->phase==M16_NT_STOPPED)return 2;
 if(t->phase==M16_NT_FAILED)return t->error;
 if(t->phase==M16_NT_IDLE)return M16_NT_INVALID;
 if(!environment())return fail(t,M16_NT_FLOAT_ENV);
 if(t->phase==M16_NT_VALIDATE_X){
  end=t->cursor+64u;if(end>16u*20u*130u)end=16u*20u*130u;
  for(i=t->cursor;i<end;i++){if(!finite32(t->features[i]))return fail(t,M16_NT_INPUT);t->last_items++;}
  t->cursor=end;if(end==16u*20u*130u){t->cursor=0;t->phase=M16_NT_VALIDATE_D;}return 1;
 }
 if(t->phase==M16_NT_VALIDATE_D){
  for(i=0;i<32u;i++){if(!finite64(t->directions[i]))return fail(t,M16_NT_INPUT);t->last_items++;}
  t->phase=M16_NT_VALIDATE_V;return 1;
 }
 if(t->phase==M16_NT_VALIDATE_V){
  for(i=0;i<16u;i++){if(t->valid_candidates[i]>1u)return fail(t,M16_NT_INPUT);t->last_items++;}
  t->cursor=t->candidate=0;t->phase=M16_NT_CANDIDATE;return 1;
 }
 if(t->phase==M16_NT_CANDIDATE){
  double az=t->directions[t->candidate*2u]*0.017453292519943295,el=t->directions[t->candidate*2u+1u]*0.017453292519943295;
  w->unit[t->candidate*3u]=cos(el)*sin(az);w->unit[t->candidate*3u+1u]=sin(el);w->unit[t->candidate*3u+2u]=cos(el)*cos(az);
  t->logits[t->candidate]=-20.f;for(i=0;i<71u;i++)own[i]=0.f;
  t->last_items=71;t->cursor=0;t->phase=t->valid_candidates[t->candidate]?M16_NT_PROJECT:M16_NT_NEXT;return 1;
 }
 if(t->phase==M16_NT_PROJECT){
  unsigned o=t->cursor/20u,frame=t->cursor%20u;float original=0.f,competition=0.f;const float *row=x+frame*130u;
  for(i=0;i<106u;i++)original+=mic16_frozen_weights[W_PROJECT_WEIGHT+o*106u+i]*row[i];
  original+=mic16_frozen_weights[W_PROJECT_BIAS+o];
  for(i=0;i<24u;i++)competition+=mic16_frozen_weights[W_COMPETITION_PROJECT_WEIGHT+o*24u+i]*row[106u+i];
  w->h0[o*20u+frame]=relu(original+competition);t->last_items=130;
  if(++t->cursor==640u){t->cursor=t->layer=0;t->phase=M16_NT_TEMPORAL;}return 1;
 }
 if(t->phase==M16_NT_TEMPORAL){
  const unsigned wo[3]={W_TEMPORAL_0_WEIGHT,W_TEMPORAL_1_WEIGHT,W_TEMPORAL_2_WEIGHT},bo[3]={W_TEMPORAL_0_BIAS,W_TEMPORAL_1_BIAS,W_TEMPORAL_2_BIAS};
  unsigned o=t->cursor/20u,frame=t->cursor%20u,k,dilation=1u<<t->layer;float sum=0.f;
  for(i=0;i<32u;i++)for(k=0;k<3u;k++){
   int ti=(int)frame+((int)k-2)*(int)dilation;
   if(ti>=0){sum+=mic16_frozen_weights[wo[t->layer]+(o*32u+i)*3u+k]*w->h0[i*20u+(unsigned)ti];t->last_items++;}
  }
  sum+=mic16_frozen_weights[bo[t->layer]+o];w->h1[o*20u+frame]=relu(w->h0[o*20u+frame]+sum);
  if(++t->cursor==640u){t->cursor=0;t->phase=M16_NT_COPY;}return 1;
 }
 if(t->phase==M16_NT_COPY){
  end=t->cursor+64u;if(end>640u)end=640u;
  for(i=t->cursor;i<end;i++){w->h0[i]=w->h1[i];t->last_items++;}
  t->cursor=end;if(end==640u){t->cursor=0;t->phase=++t->layer==3u?M16_NT_POOL:M16_NT_TEMPORAL;}return 1;
 }
 if(t->phase==M16_NT_POOL){
  float maximum=0.f;own[t->cursor]=mean20(w->h0+t->cursor*20u,1);
  for(i=0;i<20u;i++)if(w->h0[t->cursor*20u+i]>maximum)maximum=w->h0[t->cursor*20u+i];
  own[32u+t->cursor]=maximum;t->last_items=40;
  if(++t->cursor==32u){t->cursor=48;t->band_std=t->absdiff=0.f;t->phase=M16_NT_BAND_STD;}return 1;
 }
 if(t->phase==M16_NT_BAND_STD){
  t->band_std+=std20(x+t->cursor,130);t->last_items=40;
  if(++t->cursor==80u){t->cursor=1;t->phase=M16_NT_DIFF;}return 1;
 }
 if(t->phase==M16_NT_DIFF){
  for(i=48;i<80u;i++)t->absdiff+=absolute(x[t->cursor*130u+i]-x[(t->cursor-1u)*130u+i]);
  t->last_items=32;if(++t->cursor==20u){t->cursor=0;t->phase=M16_NT_ENTROPY;}return 1;
 }
 if(t->phase==M16_NT_ENTROPY){
  float lp[32],maximum=-3.402823466e38f,sum=0.f,mean=0.f,logsum,e=0.f;
  for(i=0;i<32u;i++){lp[i]=x[t->cursor*130u+48u+i]*9.210340371976184f;if(lp[i]>maximum)maximum=lp[i];mean+=lp[i];}
  for(i=0;i<32u;i++)sum+=expf(lp[i]-maximum);
  logsum=maximum+logf(sum);
  for(i=0;i<32u;i++){float logp=lp[i]-logsum;e-=expf(logp)*logp;}
  t->entropy[t->cursor]=e/3.4657359027997265f;t->flatness[t->cursor]=expf(mean/32.f-logsum+3.4657359027997265f);
  t->last_items=96;if(++t->cursor==20u){t->cursor=0;t->phase=M16_NT_STATISTICS;}return 1;
 }
 if(t->phase==M16_NT_STATISTICS){
  own[64]=std20(x+86,130);own[65]=t->band_std/32.f;own[66]=t->absdiff/(19.f*32.f);
  own[67]=mean20(t->entropy,1);own[68]=std20(t->entropy,1);own[69]=mean20(t->flatness,1);own[70]=std20(t->flatness,1);
  t->last_items=160;t->phase=M16_NT_NEXT;return 1;
 }
 if(t->phase==M16_NT_NEXT){
  t->cursor=0;if(++t->candidate==16u){t->candidate=0;t->phase=M16_NT_NEIGHBORS;}else t->phase=M16_NT_CANDIDATE;return 1;
 }
 if(t->phase==M16_NT_NEIGHBORS){
  end=t->cursor+8u;if(end>256u)end=256u;
  for(i=t->cursor;i<end;i++){
   unsigned c=i/16u,j=i%16u;double dot=w->unit[c*3u]*w->unit[j*3u]+w->unit[c*3u+1u]*w->unit[j*3u+1u]+w->unit[c*3u+2u]*w->unit[j*3u+2u];
   w->neighbors[i]=(unsigned char)(c!=j && t->valid_candidates[c] && t->valid_candidates[j] && dot>=t->neighbor_limit);t->last_items++;
  }
  t->cursor=end;if(end==256u){t->candidate=t->cursor=0;t->phase=M16_NT_CONTEXT_INIT;}return 1;
 }
 if(t->phase==M16_NT_CONTEXT_INIT){
  if(!t->valid_candidates[t->candidate]){t->phase=M16_NT_PROBABILITY;return 1;}
  for(i=0;i<71u;i++){w->context[i]=own[i];w->context[71u+i]=0.f;w->context[142u+i]=-3.402823466e38f;}
  t->last_items=213;t->neighbor=t->neighbor_count=0;t->phase=M16_NT_CONTEXT_ADD;return 1;
 }
 if(t->phase==M16_NT_CONTEXT_ADD){
  if(w->neighbors[t->candidate*16u+t->neighbor]){
   t->neighbor_count++;
   for(i=0;i<71u;i++){float value=w->own[t->neighbor*71u+i];w->context[71u+i]+=value;if(value>w->context[142u+i])w->context[142u+i]=value;t->last_items++;}
  }
  if(++t->neighbor==16u)t->phase=M16_NT_CONTEXT_FINISH;
  return 1;
 }
 if(t->phase==M16_NT_CONTEXT_FINISH){
  for(i=0;i<71u;i++){
   w->context[71u+i]=t->neighbor_count?own[i]-w->context[71u+i]/t->neighbor_count:0.f;
   w->context[142u+i]=t->neighbor_count?own[i]-w->context[142u+i]:0.f;
  }
  w->context[213]=t->neighbor_count/15.f;w->context[214]=t->neighbor_count?1.f:0.f;t->last_items=144;t->cursor=0;t->phase=M16_NT_HEAD64;return 1;
 }
 if(t->phase==M16_NT_HEAD64 || t->phase==M16_NT_HEAD32 || t->phase==M16_NT_HEAD1){
  unsigned n,wo,bo,outputs;const float *src;float *dst,sum=0.f;
  if(t->phase==M16_NT_HEAD64){n=215;wo=W_HEAD_0_WEIGHT;bo=W_HEAD_0_BIAS;src=w->context;dst=w->fc64;outputs=64;}
  else if(t->phase==M16_NT_HEAD32){n=64;wo=W_HEAD_2_WEIGHT;bo=W_HEAD_2_BIAS;src=w->fc64;dst=w->fc32;outputs=32;}
  else{n=32;wo=W_HEAD_4_WEIGHT;bo=W_HEAD_4_BIAS;src=w->fc32;dst=t->logits+t->candidate;outputs=1;}
  for(i=0;i<n;i++)sum+=mic16_frozen_weights[wo+t->cursor*n+i]*src[i];
  sum+=mic16_frozen_weights[bo+t->cursor];dst[t->cursor]=t->phase==M16_NT_HEAD1?sum:relu(sum);t->last_items=n;
  if(++t->cursor==outputs){
   t->cursor=0;
   if(t->phase==M16_NT_HEAD64)t->phase=M16_NT_HEAD32;
   else if(t->phase==M16_NT_HEAD32)t->phase=M16_NT_HEAD1;
   else{if(!finite32(t->logits[t->candidate]))return fail(t,M16_NT_OUTPUT);t->phase=M16_NT_PROBABILITY;}
  }return 1;
 }
 if(t->phase==M16_NT_PROBABILITY){
  t->probabilities[t->candidate]=mic16_frozen_probability(t->logits[t->candidate]);t->last_items=1;
  if(++t->candidate==16u){t->phase=M16_NT_COMPLETE;return 0;}
  t->phase=M16_NT_CONTEXT_INIT;return 1;
 }
 return fail(t,M16_NT_INVALID);
}
int m16_frozen_score_task_step(m16_frozen_score_task *t){
 int rc;if(!valid(t))return M16_NT_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_frozen_score_task_stop(m16_frozen_score_task *t){if(!valid(t))return M16_NT_INVALID;t->phase=M16_NT_STOPPED;return 1;}
