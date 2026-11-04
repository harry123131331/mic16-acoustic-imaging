#include "mic16_audio_pipeline_task.h"
#include "mic16_front_context.h"
#define MAGIC 0x4d415031u
static int detection_quiet(const m16_audio_pipeline_task *);
static int valid(const m16_audio_pipeline_task *t){return t && !((uintptr_t)t%_Alignof(m16_audio_pipeline_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);}
int m16_audio_pipeline_init(m16_audio_pipeline_task *t,m16_detection_task *d,m16_source_audio_task *s,mic16_pipeline_observation *o){
 const void *p[7],*q[8];uintptr_t n[7],m[8];unsigned i,j;
 if(!t || (uintptr_t)t%_Alignof(m16_audio_pipeline_task) || t->magic || !d || !s || !o ||
  (uintptr_t)o%_Alignof(mic16_pipeline_observation) || !m16_detection_releasable(d) || !m16_source_audio_releasable(s) ||
  s->enhancement->shared!=&d->work->extended || s->continuous->work->windows || s->next_sample || s->closed)return M16_AP_INVALID;
 p[0]=t;p[1]=s;p[2]=s->enhancement;p[3]=s->enhancement->work;p[4]=s->continuous;p[5]=s->continuous->work;p[6]=o;
 n[0]=sizeof(*t);n[1]=sizeof(*s);n[2]=sizeof(*s->enhancement);n[3]=sizeof(*s->enhancement->work);n[4]=sizeof(*s->continuous);n[5]=sizeof(*s->continuous->work);n[6]=sizeof(*o);
 q[0]=d;q[1]=d->work;q[2]=d->coarse;q[3]=d->spectrum;q[4]=d->spectrum->owner;q[5]=d->runtime;q[6]=d->runtime->arena;q[7]=d->statistics_storage;
 m[0]=sizeof(*d);m[1]=sizeof(*d->work);m[2]=sizeof(*d->coarse);m[3]=sizeof(*d->spectrum);m[4]=sizeof(*d->spectrum->owner);m[5]=sizeof(*d->runtime);m[6]=(uintptr_t)d->runtime->words*8u;m[7]=sizeof(*d->statistics_storage);
 for(i=0;i<7;i++){
  for(j=i+1;j<7;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_AP_INVALID;
  for(j=0;j<8;j++)if(!apart(p[i],n[i],q[j],m[j]))return M16_AP_INVALID;
 }
 *t=(m16_audio_pipeline_task){0};t->magic=MAGIC;t->detection=d;t->source_audio=s;t->observation=o;return 0;
}
int m16_audio_pipeline_releasable(const m16_audio_pipeline_task *t){
 return valid(t) && (t->phase==M16_AP_IDLE || t->phase==M16_AP_COMPLETE || t->phase==M16_AP_STOPPED || t->phase==M16_AP_FAILED) &&
  detection_quiet(t) && m16_source_audio_releasable(t->source_audio);
}
static int downstream_quiet(const void *);
static int begin_context(m16_audio_pipeline_task *t,const float *x,const float *q,uint64_t first,m16_front_context_task *context){
 const void *p[6];uintptr_t n[6];uint64_t tag,job;unsigned i;int rc;m16_detection_task *d;
 if(!valid(t) || !x || !q)return M16_AP_INVALID;
 if(context){
  if((t->phase!=M16_AP_IDLE&&t->phase!=M16_AP_COMPLETE)||!m16_detection_prepared_releasable(t->detection,context)||
   !m16_source_audio_releasable(t->source_audio))return M16_AP_BUSY;
 }else if(!m16_audio_pipeline_releasable(t))return M16_AP_BUSY;
 if(t->phase==M16_AP_STOPPED || t->phase==M16_AP_FAILED || t->source_audio->closed || first!=t->source_audio->next_sample)return M16_AP_SEQUENCE;
 if(t->source_audio->epoch_reference && q!=t->source_audio->epoch_reference)return M16_AP_SEQUENCE;
 p[0]=t;p[1]=t->source_audio;p[2]=t->source_audio->enhancement;p[3]=t->source_audio->enhancement->work;p[4]=t->source_audio->continuous;p[5]=t->source_audio->continuous->work;
 n[0]=sizeof(*t);n[1]=sizeof(*t->source_audio);n[2]=sizeof(*t->source_audio->enhancement);n[3]=sizeof(*t->source_audio->enhancement->work);n[4]=sizeof(*t->source_audio->continuous);n[5]=sizeof(*t->source_audio->continuous->work);
 for(i=0;i<6;i++)if(!apart(x,40960u*16u*4u,p[i],n[i]) || !apart(q,8192u*16u*4u,p[i],n[i]))return M16_AP_INVALID;
 d=t->detection;tag=d->reserved_tag_high;job=d->reserved_job_high;
 if(tag<d->runtime->tag_high)tag=d->runtime->tag_high;
 if(tag<d->front.refinement.reserved_tag_high)tag=d->front.refinement.reserved_tag_high;
 if(tag<d->features.reserved_tag_high)tag=d->features.reserved_tag_high;
 if(tag==UINT64_MAX || job==UINT64_MAX)return M16_AP_SEQUENCE;
 if(context&&context->epoch!=t->source_audio->epoch)return M16_AP_SEQUENCE;
 rc=context?m16_detection_begin_prepared(d,context,q,8192u*16u,t->observation,job+1u,tag+1u):
  m16_detection_begin(d,x,40960u*16u,q,8192u*16u,t->observation,t->source_audio->epoch,first,job+1u,tag+1u);
 if(rc)return rc;
 t->front_context=context;
 if(context){context->downstream_owner=t;context->downstream_releasable=downstream_quiet;}
 t->pcm=x;t->reference=q;t->first_sample=first;t->phase=M16_AP_DETECT;t->error=0;t->stop_sent=t->finishing=0;t->steps=0;return 0;
}
static int detection_quiet(const m16_audio_pipeline_task *t){
 return t->front_context&&t->detection->front_context==t->front_context?
  m16_detection_prepared_releasable(t->detection,t->front_context):m16_detection_releasable(t->detection);
}
static int downstream_quiet(const void *p){return m16_audio_pipeline_releasable((const m16_audio_pipeline_task *)p);}
int m16_audio_pipeline_begin(m16_audio_pipeline_task *t,const float *x,const float *q,uint64_t first){return begin_context(t,x,q,first,0);}
int m16_audio_pipeline_begin_prepared(m16_audio_pipeline_task *t,m16_front_context_task *c,const float *q){
 if(!valid(t)||!c||c->detector!=t->detection||c->phase!=M16_FC_READY||c->submitted)return M16_AP_INVALID;
 return begin_context(t,c->pcm,q,c->first_sample,c);
}
static int draining(m16_audio_pipeline_task *t,int rc){
 if(!t->error)t->error=rc;
 (void)m16_source_audio_stop(t->source_audio);t->stop_sent=0;t->phase=M16_AP_DRAIN;return 1;
}
static int drain(m16_audio_pipeline_task *t){
 int rc;
 if(!t->stop_sent){t->stop_sent=1;rc=m16_detection_stop(t->detection);if(rc<0 && !t->error)t->error=rc;}
 if(!detection_quiet(t)){
  rc=m16_detection_step(t->detection);if(rc<0 && !t->error)t->error=rc;
 }
 if(!detection_quiet(t) || !m16_source_audio_releasable(t->source_audio))return 1;
 t->phase=t->error?M16_AP_FAILED:M16_AP_STOPPED;return t->error?t->error:2;
}
int m16_audio_pipeline_step(m16_audio_pipeline_task *t){
 int rc;if(!valid(t))return M16_AP_INVALID;t->steps++;
 if(t->phase==M16_AP_IDLE)return M16_AP_INVALID;
 if(t->phase==M16_AP_COMPLETE)return 0;
 if(t->phase==M16_AP_STOPPED)return 2;
 if(t->phase==M16_AP_FAILED)return t->error;
 if(t->phase==M16_AP_DRAIN)return drain(t);
 if(t->phase==M16_AP_DETECT){
  rc=m16_detection_step(t->detection);
  if(rc<0 || rc==2)return draining(t,rc<0?rc:M16_AP_INPUT);
  if(!rc){
   if(!detection_quiet(t))return draining(t,M16_AP_INPUT);
   rc=m16_source_audio_begin(t->source_audio,t->pcm,t->reference,t->observation,t->first_sample);
   if(rc)return draining(t,rc);
   t->phase=M16_AP_SOURCE_AUDIO;
  }
  return 1;
 }
 if(t->phase==M16_AP_SOURCE_AUDIO){
  rc=m16_source_audio_step(t->source_audio);
  if(rc<0 || rc==2)return draining(t,rc<0?rc:M16_AP_INPUT);
  if(!rc){t->phase=M16_AP_COMPLETE;return 0;}return 1;
 }
 return draining(t,M16_AP_INVALID);
}
int m16_audio_pipeline_location(const m16_audio_pipeline_task *t,const mic16_cycle_output **tracks,const mic16_pipeline_observation **o){
 if(!valid(t))return M16_AP_INVALID;
 if(t->phase!=M16_AP_SOURCE_AUDIO)return 0;
 return m16_source_audio_location(t->source_audio,tracks,o);
}
int m16_audio_pipeline_location_release(m16_audio_pipeline_task *t,uint32_t epoch,uint32_t window){
 if(!valid(t) || t->phase!=M16_AP_SOURCE_AUDIO)return M16_AP_INVALID;
 return m16_source_audio_location_release(t->source_audio,epoch,window);
}
int m16_audio_pipeline_output(const m16_audio_pipeline_task *t,const m16_cont_info **info,const double **a,const double **b,unsigned *n){
 if(!valid(t))return M16_AP_INVALID;
 if(t->phase!=M16_AP_SOURCE_AUDIO)return 0;
 return m16_source_audio_output(t->source_audio,info,a,b,n);
}
int m16_audio_pipeline_output_release(m16_audio_pipeline_task *t,uint32_t epoch,uint32_t window,uint32_t id){
 if(!valid(t) || t->phase!=M16_AP_SOURCE_AUDIO)return M16_AP_INVALID;
 return m16_source_audio_output_release(t->source_audio,epoch,window,id);
}
int m16_audio_pipeline_finish(m16_audio_pipeline_task *t){
 int rc;if(!valid(t))return M16_AP_INVALID;
 if(!m16_audio_pipeline_releasable(t))return M16_AP_BUSY;
 if(t->phase==M16_AP_STOPPED || t->phase==M16_AP_FAILED)return M16_AP_SEQUENCE;
 rc=m16_source_audio_finish(t->source_audio);if(rc)return rc;
 t->phase=M16_AP_SOURCE_AUDIO;t->finishing=1;return 0;
}
int m16_audio_pipeline_stop(m16_audio_pipeline_task *t){if(!valid(t))return M16_AP_INVALID;return draining(t,0);}
int m16_audio_pipeline_reset(m16_audio_pipeline_task *t,uint32_t epoch,const char *key){
 int rc;if(!valid(t) || (uint64_t)epoch<=t->detection->last_epoch || (uint64_t)epoch<=t->detection->coarse->last_epoch)return M16_AP_INVALID;
 if(!m16_audio_pipeline_releasable(t))return M16_AP_BUSY;
 rc=m16_source_audio_reset(t->source_audio,epoch,key);if(rc)return rc;
 t->phase=M16_AP_IDLE;t->error=0;t->stop_sent=t->finishing=0;return 0;
}
