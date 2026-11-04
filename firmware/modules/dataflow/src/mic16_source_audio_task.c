#include "mic16_source_audio_task.h"
#include <limits.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d534131u
static int valid(const m16_source_audio_task *t){return t && !((uintptr_t)t%_Alignof(m16_source_audio_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);}
static unsigned key_length(const char *key){unsigned n;if(!key)return 0;for(n=0;n<512u;n++)if(!key[n])return n;return 0;}
static void key_copy(char *out,const char *key,unsigned n){unsigned i;for(i=0;i<=n;i++)out[i]=key[i];}
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
static int fail(m16_source_audio_task *t,int rc){
 (void)m16_enh_prediction_stop(t->enhancement);(void)m16_continuous_task_stop(t->continuous);t->location_ready=0;t->phase=M16_SA_FAILED;t->error=rc;return rc;
}
int m16_source_audio_init(m16_source_audio_task *t,m16_enh_prediction_task *e,m16_continuous_task *c,const char *key){
 unsigned n=key_length(key);const void *p[6];uintptr_t size[6];unsigned i,j;
 if(!t || (uintptr_t)t%_Alignof(m16_source_audio_task) || t->magic || !e || !c || !n || !m16_enh_prediction_releasable(e) || !m16_continuous_task_releasable(c) ||
  !c->work->epoch || c->work->windows || c->work->closed || c->work->failed)return M16_SA_INVALID;
 p[0]=t;p[1]=e;p[2]=c;p[3]=e->shared;p[4]=e->work;p[5]=c->work;
 size[0]=sizeof(*t);size[1]=sizeof(*e);size[2]=sizeof(*c);size[3]=sizeof(*e->shared);size[4]=sizeof(*e->work);size[5]=sizeof(*c->work);
 for(i=0;i<6;i++)for(j=i+1;j<6;j++)if(!apart(p[i],size[i],p[j],size[j]))return M16_SA_INVALID;
 if(!apart(t,sizeof(*t),key,n+1u))return M16_SA_INVALID;
 *t=(m16_source_audio_task){0};t->magic=MAGIC;t->enhancement=e;t->continuous=c;t->epoch=c->work->epoch;
 key_copy(t->pose_reference_key,key,n);mic16_memory_init(&t->memory);return 0;
}
int m16_source_audio_releasable(const m16_source_audio_task *t){
 return valid(t) && (t->phase==M16_SA_IDLE || t->phase==M16_SA_COMPLETE || t->phase==M16_SA_STOPPED || t->phase==M16_SA_FAILED) && !t->location_ready &&
  m16_enh_prediction_releasable(t->enhancement) && m16_continuous_task_releasable(t->continuous);
}
int m16_source_audio_set_localization_only(m16_source_audio_task *t,unsigned enabled){
 if(!valid(t)||enabled>1u)return M16_SA_INVALID;
 if(t->phase!=M16_SA_IDLE||t->next_sample||t->closed||!m16_source_audio_releasable(t))return M16_SA_BUSY;
 t->localization_only=enabled;return 0;
}
int m16_source_audio_begin(m16_source_audio_task *t,const float *pcm,const float *reference,const mic16_pipeline_observation *o,uint64_t first){
 const void *p[9];uintptr_t size[9];unsigned i,j;
 if(!valid(t) || !pcm || !reference || !o || (uintptr_t)pcm%4u || (uintptr_t)reference%4u || (uintptr_t)o%_Alignof(mic16_pipeline_observation))return M16_SA_INVALID;
 if(!m16_source_audio_releasable(t))return M16_SA_BUSY;
 if(t->closed || t->phase==M16_SA_FAILED || t->phase==M16_SA_STOPPED)return M16_SA_SEQUENCE;
 if(first!=t->next_sample || first/40960u>=(unsigned)INT_MAX || t->continuous->work->epoch!=t->epoch || (!t->localization_only && t->continuous->work->windows!=first/40960u) ||
  (t->epoch_reference && reference!=t->epoch_reference))return M16_SA_SEQUENCE;
 p[0]=t;p[1]=t->enhancement;p[2]=t->continuous;p[3]=t->enhancement->shared;p[4]=t->enhancement->work;p[5]=t->continuous->work;p[6]=pcm;p[7]=reference;p[8]=o;
 size[0]=sizeof(*t);size[1]=sizeof(*t->enhancement);size[2]=sizeof(*t->continuous);size[3]=sizeof(*t->enhancement->shared);size[4]=sizeof(*t->enhancement->work);size[5]=sizeof(*t->continuous->work);size[6]=40960u*16u*4u;size[7]=8192u*16u*4u;size[8]=sizeof(*o);
 for(i=0;i<9;i++)for(j=i+1;j<9;j++)if(!apart(p[i],size[i],p[j],size[j]))return M16_SA_INVALID;
 if(o->candidate_count<1u || o->candidate_count>16u)return M16_SA_INPUT;
 t->pcm=pcm;t->reference=reference;t->observation=o;t->epoch_reference=reference;t->first_sample=first;t->steps=0;t->error=0;t->last_was_finish=0;t->phase=M16_SA_TRACK;return 0;
}
static int completed(m16_source_audio_task *t){
 if(t->last_was_finish)t->closed=1;else t->next_sample=t->first_sample+40960u;
 if(t->location_ready){t->phase=M16_SA_WAIT_LOCATION;return 1;}
 t->phase=M16_SA_COMPLETE;return 0;
}
int m16_source_audio_step(m16_source_audio_task *t){
 int rc;
 if(!valid(t))return M16_SA_INVALID;
 t->steps++;
 if(t->phase==M16_SA_COMPLETE)return 0;
 if(t->phase==M16_SA_STOPPED)return 2;
 if(t->phase==M16_SA_FAILED)return t->error;
 if(t->phase==M16_SA_IDLE)return M16_SA_INVALID;
 if(!environment())return fail(t,M16_CT_FLOAT_ENV);
 if(t->phase==M16_SA_WAIT_LOCATION)return 1;
 if(t->phase==M16_SA_TRACK){
  const mic16_pipeline_observation *o=t->observation;
  rc=mic16_consume_scored_cycle(&t->memory,o->directions,o->valid,o->scores,(double)(t->first_sample+40960u)/39062.5,t->pose_reference_key,1,&t->tracks);
  if(rc)return fail(t,rc);
  t->location_ready=1;
  /* 仅定位保留同一冻结评分和记忆更新，不进入任何增强、FIR增强或重建节点。 */
  if(t->localization_only)return completed(t);
  rc=m16_enh_prediction_begin(t->enhancement,t->pcm,t->reference,o->directions,o->valid,&t->tracks,&t->prediction);
  if(rc)return fail(t,rc);
  t->phase=M16_SA_PREDICT;return 1;
 }
 if(t->phase==M16_SA_PREDICT){
  rc=m16_enh_prediction_step(t->enhancement);
  if(rc<0 || rc==2)return fail(t,rc<0?rc:M16_SA_INVALID);
  if(!rc){
   if(!t->enhancement->prediction_valid)return fail(t,M16_SA_INPUT);
   rc=m16_continuous_task_push(t->continuous,t->pcm,&t->prediction,t->first_sample);if(rc)return fail(t,rc);
   t->phase=M16_SA_REBUILD;
  }
  return 1;
 }
 if(t->phase==M16_SA_REBUILD || t->phase==M16_SA_FINISH){
  rc=m16_continuous_task_step(t->continuous);
  if(rc<0 || rc==2)return fail(t,rc<0?rc:M16_SA_INVALID);
  if(!rc)return completed(t);
  return 1;
 }
 return fail(t,M16_SA_INVALID);
}
int m16_source_audio_location(const m16_source_audio_task *t,const mic16_cycle_output **tracks,const mic16_pipeline_observation **o){
 if(!valid(t) || !tracks || !o)return M16_SA_INVALID;
 if(!t->location_ready)return 0;
 *tracks=&t->tracks;*o=t->observation;return 1;
}
int m16_source_audio_location_release(m16_source_audio_task *t,uint32_t epoch,uint32_t window){
 if(!valid(t) || !t->location_ready || epoch!=t->epoch || (uint64_t)window!=t->first_sample/40960u)return M16_SA_INVALID;
 t->location_ready=0;if(t->phase==M16_SA_WAIT_LOCATION)t->phase=M16_SA_COMPLETE;return 0;
}
int m16_source_audio_output(const m16_source_audio_task *t,const m16_cont_info **info,const double **a,const double **b,unsigned *n){
 if(!valid(t))return M16_SA_INVALID;
 if(t->phase!=M16_SA_REBUILD && t->phase!=M16_SA_FINISH)return 0;
 return m16_continuous_task_output(t->continuous,info,a,b,n);
}
int m16_source_audio_output_release(m16_source_audio_task *t,uint32_t epoch,uint32_t window,uint32_t id){
 if(!valid(t) || (t->phase!=M16_SA_REBUILD && t->phase!=M16_SA_FINISH))return M16_SA_INVALID;
 return m16_continuous_task_release(t->continuous,epoch,window,id);
}
int m16_source_audio_finish(m16_source_audio_task *t){
 int rc;if(!valid(t))return M16_SA_INVALID;if(!m16_source_audio_releasable(t))return M16_SA_BUSY;
 if(t->closed || t->phase==M16_SA_FAILED || t->phase==M16_SA_STOPPED)return M16_SA_SEQUENCE;
 if(t->localization_only){t->closed=t->last_was_finish=1;t->phase=M16_SA_COMPLETE;return 0;}
 rc=m16_continuous_task_finish(t->continuous);if(rc)return fail(t,rc);t->last_was_finish=1;t->phase=M16_SA_FINISH;return 0;
}
int m16_source_audio_stop(m16_source_audio_task *t){
 if(!valid(t))return M16_SA_INVALID;
 (void)m16_enh_prediction_stop(t->enhancement);(void)m16_continuous_task_stop(t->continuous);t->location_ready=0;t->phase=M16_SA_STOPPED;return 1;
}
int m16_source_audio_reset(m16_source_audio_task *t,uint32_t epoch,const char *key){
 unsigned n=key_length(key);int rc;
 if(!valid(t) || !n || epoch<=t->epoch || !apart(t,sizeof(*t),key,n+1u))return M16_SA_INVALID;
 if(!m16_source_audio_releasable(t))return M16_SA_BUSY;
 rc=m16_continuous_task_reset(t->continuous,epoch);if(rc)return rc;
 (void)m16_enh_prediction_stop(t->enhancement);mic16_memory_init(&t->memory);key_copy(t->pose_reference_key,key,n);
 t->epoch=epoch;t->closed=t->location_ready=t->last_was_finish=0;t->next_sample=0;t->epoch_reference=0;t->phase=M16_SA_IDLE;t->error=0;return 0;
}
