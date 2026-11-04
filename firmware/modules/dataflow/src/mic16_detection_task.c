#include "mic16_detection_task.h"
#include "mic16_front_context.h"
#define MAGIC 0x4d445431u
static int valid(const m16_detection_task *t){return t && !((uintptr_t)t%_Alignof(m16_detection_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int draining(m16_detection_task *t,int error){
 if(!t->error)t->error=error;
 if(t->phase!=M16_DT_DRAIN){
  (void)m16_public_spectrum_stop(&t->front.spectrum);
  if(!m16_candidate_pool_releasable(&t->pool))(void)m16_candidate_pool_stop(&t->pool);
  if(!m16_frozen_score_task_releasable(&t->score))(void)m16_frozen_score_task_stop(&t->score);
  t->front_terminal=!t->front_started || t->front_done;
  t->features_terminal=!t->features_started || t->features_done;
  t->front_stop_sent=t->features_stop_sent=0;t->phase=M16_DT_DRAIN;
 }
 return 1;
}
int m16_detection_init(m16_detection_task *t,mic16_pipeline_workspace *w,m16_coarse_scan_runtime *c,
 m16_shared_spectrum_task *s,m16_projection_runtime *r,m16_statistics_projection_storage *storage){
 const void *p[8];uintptr_t n[8];unsigned i,j;uint64_t a[3],end[3];int rc;
 if(!t || !w || !c || !s || !r || !storage || (uintptr_t)t%_Alignof(m16_detection_task) ||
  (uintptr_t)w%_Alignof(mic16_pipeline_workspace) || (uintptr_t)c%_Alignof(m16_coarse_scan_runtime) ||
  (uintptr_t)s%_Alignof(m16_shared_spectrum_task) || (uintptr_t)r%_Alignof(m16_projection_runtime) ||
  (uintptr_t)storage%_Alignof(m16_statistics_projection_storage) || t->magic ||
  !m16_coarse_scan_available(c) || !m16_shared_spectrum_task_available(s) || !m16_projection_runtime_available(r))return M16_DT_INVALID;
 p[0]=t;p[1]=w;p[2]=c;p[3]=s;p[4]=s->owner;p[5]=r;p[6]=r->arena;p[7]=storage;
 n[0]=sizeof(*t);n[1]=sizeof(*w);n[2]=sizeof(*c);n[3]=sizeof(*s);n[4]=sizeof(*s->owner);n[5]=sizeof(*r);n[6]=(uintptr_t)r->words*8u;n[7]=sizeof(*storage);
 for(i=0;i<8;i++)for(j=i+1;j<8;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_DT_INVALID;
 a[0]=(uint64_t)c->io.arena_address-64u;end[0]=a[0]+sizeof(c->arena);
 a[1]=(uint64_t)s->owner->io.arena_address-64u;end[1]=a[1]+M16_SHARED_ARENA_WORDS*8u+128u;
 a[2]=r->io.arena_address;end[2]=a[2]+(uint64_t)r->words*8u;
 for(i=0;i<3;i++)for(j=i+1;j<3;j++)if(a[i]<end[j] && a[j]<end[i])return M16_DT_INVALID;
 *t=(m16_detection_task){0};
 rc=m16_spatial_front_init(&t->front,&w->extended.physical.frame,&w->extended.physical.spectral,c,r);if(rc)return rc;
 rc=m16_candidate_pool_init(&t->pool,&w->spatial);if(rc)return rc;
 rc=m16_feature_pipeline_init(&t->features,s,r,&w->extended,storage);if(rc)return rc;
 rc=m16_frozen_score_task_init(&t->score,&w->score);if(rc)return rc;
 t->magic=MAGIC;t->work=w;t->coarse=c;t->spectrum=s;t->runtime=r;t->statistics_storage=storage;
 t->last_epoch=c->last_epoch;t->reserved_tag_high=r->tag_high;t->reserved_job_high=s->last_job;return 0;
}
static int core_releasable(const m16_detection_task *t){
 return valid(t) && (t->phase==M16_DT_IDLE || t->phase==M16_DT_COMPLETE || t->phase==M16_DT_STOPPED || t->phase==M16_DT_FAILED) &&
  m16_spatial_front_releasable(&t->front) && m16_candidate_pool_releasable(&t->pool) &&
  m16_feature_pipeline_releasable(&t->features) && m16_frozen_score_task_releasable(&t->score);
}
int m16_detection_releasable(const m16_detection_task *t){return core_releasable(t)&&!t->front_context;}
int m16_detection_prepared_releasable(const m16_detection_task *t,const m16_front_context_task *c){
 return c&&core_releasable(t)&&t->front_context==c;
}
static int begin_context(m16_detection_task *t,const float *x,unsigned xn,const float *q,unsigned qn,mic16_pipeline_observation *o,
 uint64_t epoch,uint64_t first,uint64_t job,uint64_t tag,m16_front_context_task *owner){
 const void *p[11];uintptr_t n[11];unsigned i,j;
 if(!valid(t) || !x || !q || !o || (uintptr_t)x%4u || (uintptr_t)q%4u || (uintptr_t)o%_Alignof(mic16_pipeline_observation) ||
  xn<40960u*16u || qn<8192u*16u || !epoch || first%40960u || first>UINT64_MAX-40960u || !job || job>UINT64_MAX-31u ||
  !tag || tag>UINT64_MAX-(M16_DT_TAGS-1u) || tag<=t->reserved_tag_high || (!t->runtime->arbitrated&&tag<=t->runtime->tag_high) ||
  tag<=t->front.refinement.reserved_tag_high || tag<=t->features.reserved_tag_high)return M16_DT_INVALID;
 if(t->front.evidence_cache&&(!owner||!owner->ring->evidence_enabled||t->front.evidence_cache!=&owner->ring->evidence.view))return M16_DT_INVALID;
 if(t->front_context!=owner||!core_releasable(t))return M16_DT_BUSY;
 if(epoch<t->last_epoch || (epoch==t->last_epoch && (job<=t->reserved_job_high || (t->has_run && first<=t->last_sample))) ||
  (!t->front.evidence_cache&&(epoch<t->coarse->last_epoch || (epoch>t->coarse->last_epoch && first!=0) ||
  (epoch==t->coarse->last_epoch && first/2048u<=t->coarse->last_frame))))return M16_DT_SEQUENCE;
 p[0]=t;p[1]=t->work;p[2]=t->coarse;p[3]=t->spectrum;p[4]=t->spectrum->owner;p[5]=t->runtime;
 p[6]=t->runtime->arena;p[7]=t->statistics_storage;p[8]=x;p[9]=q;p[10]=o;
 n[0]=sizeof(*t);n[1]=sizeof(*t->work);n[2]=sizeof(*t->coarse);n[3]=sizeof(*t->spectrum);n[4]=sizeof(*t->spectrum->owner);n[5]=sizeof(*t->runtime);
 n[6]=(uintptr_t)t->runtime->words*8u;n[7]=sizeof(*t->statistics_storage);n[8]=40960u*16u*4u;n[9]=8192u*16u*4u;n[10]=sizeof(*o);
 for(i=0;i<11;i++)for(j=i+1;j<11;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_DT_INVALID;
 t->epoch=t->last_epoch=epoch;t->first_sample=t->last_sample=first;t->first_job=job;t->first_tag=tag;t->has_run=1;
 t->reserved_job_high=job+31u;t->reserved_tag_high=tag+M16_DT_TAGS-1u;
 t->pcm=x;t->reference=q;t->output=o;t->reference_frame=t->front_started=t->front_done=t->features_started=t->features_done=0;
 t->phase=M16_DT_REFERENCE_BEGIN;t->prepared_input=owner!=0;t->error=0;t->steps=0;
 mic16_reference_init(&t->work->extended.physical.reference);return 0;
}
int m16_detection_begin(m16_detection_task *t,const float *x,unsigned xn,const float *q,unsigned qn,mic16_pipeline_observation *o,
 uint64_t epoch,uint64_t first,uint64_t job,uint64_t tag){
 return begin_context(t,x,xn,q,qn,o,epoch,first,job,tag,0);
}
int m16_detection_begin_prepared(m16_detection_task *t,m16_front_context_task *c,const float *q,unsigned qn,
 mic16_pipeline_observation *o,uint64_t job,uint64_t tag){
 int rc;
 if(!valid(t)||!c||t->front_context!=c||c->detector!=t||c->phase!=M16_FC_READY||c->reader||c->submitted||c->error)return M16_DT_INVALID;
 rc=begin_context(t,c->pcm,40960u*16u,q,qn,o,c->epoch,c->first_sample,job,tag,c);
 if(!rc){c->phase=M16_FC_IN_USE;c->submitted=1;}return rc;
}
static int drain(m16_detection_task *t){
 int rc;
 if(!t->front_terminal){
  if(!t->front_stop_sent){t->front_stop_sent=1;rc=m16_spatial_front_stop(&t->front);}
  else rc=m16_spatial_front_step(&t->front);
  if(rc<0){if(!t->error)t->error=rc;t->front_terminal=1;}
  else if(rc==2 || m16_spatial_front_releasable(&t->front))t->front_terminal=1;
 }
 if(!t->features_terminal){
  if(!t->features_stop_sent){t->features_stop_sent=1;rc=m16_feature_pipeline_stop(&t->features);}
  else rc=m16_feature_pipeline_step(&t->features);
  if(rc<0){if(!t->error)t->error=rc;t->features_terminal=1;}
  else if(rc==2 || m16_feature_pipeline_releasable(&t->features))t->features_terminal=1;
 }
 if(!t->front_terminal || !t->features_terminal)return 1;
 t->phase=t->error?M16_DT_FAILED:M16_DT_STOPPED;return t->error?t->error:2;
}
int m16_detection_step(m16_detection_task *t){
 unsigned i;int rc;mic16_pipeline_workspace *w;mic16_physical_workspace *p;
 if(!valid(t))return M16_DT_INVALID;
 t->steps++;w=t->work;p=&w->extended.physical;
 if(t->phase==M16_DT_COMPLETE)return 0;
 if(t->phase==M16_DT_STOPPED)return 2;
 if(t->phase==M16_DT_FAILED)return t->error;
 if(t->phase==M16_DT_DRAIN)return drain(t);
 if(t->phase==M16_DT_REFERENCE_BEGIN){
  rc=m16_public_spectrum_begin(&t->front.spectrum,t->reference+t->reference_frame*2048u*16u,2048u*16u,t->epoch,t->reference_frame);
  if(rc)return draining(t,rc);
  t->phase=M16_DT_REFERENCE;return 1;
 }
 if(t->phase==M16_DT_REFERENCE){
  rc=m16_public_spectrum_step(&t->front.spectrum);if(rc<0)return draining(t,rc);if(rc==2)return draining(t,M16_DT_INPUT);
  if(rc==0)t->phase=M16_DT_REFERENCE_ADD;
  return 1;
 }
 if(t->phase==M16_DT_REFERENCE_ADD){
  rc=mic16_reference_add(&p->reference,p->frame.zone_level_db);if(rc)return draining(t,rc);
  if(++t->reference_frame<4u){t->phase=M16_DT_REFERENCE_BEGIN;return 1;}
  rc=mic16_reference_finalize(&p->reference);if(rc)return draining(t,rc);
  rc=(t->front.evidence_cache?m16_spatial_front_begin_evidence:(t->prepared_input?m16_spatial_front_begin_prepared:m16_spatial_front_begin))
   (&t->front,t->pcm,40960u*16u,p->phat,M16_SP_PHAT_FLOATS,p->levels,480,w->maps,M16_SP_MAP_FLOATS,
   t->epoch,t->first_sample,t->first_tag);if(rc)return draining(t,rc);
  t->front_started=1;t->phase=M16_DT_SCAN;return 1;
 }
 if(t->phase==M16_DT_SCAN){
  rc=m16_spatial_front_step(&t->front);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_DT_INPUT);
  t->front_done=1;
  rc=m16_candidate_pool_begin(&t->pool,w->maps,M16_SP_MAP_FLOATS,p->levels,480,p->reference.floor_db,w->votes,480u*9u,w->best_indices,&w->pool);
  if(rc)return draining(t,rc);
  t->phase=M16_DT_POOL;return 1;
 }
 if(t->phase==M16_DT_POOL){
  rc=m16_candidate_pool_step(&t->pool);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_DT_INPUT);
  if(w->pool.count<1u || w->pool.count>16u)return draining(t,M16_DT_INPUT);
  t->phase=M16_DT_DIRECTIONS;return 1;
 }
 if(t->phase==M16_DT_DIRECTIONS){
  mic16_prepared_spectral prepared={t->pcm,t->reference,p};
  for(i=0;i<32u;i++)w->physical_directions[i]=0.;
  for(i=0;i<w->pool.count;i++){w->physical_directions[i*2u]=w->pool.items[i].direction[0];w->physical_directions[i*2u+1u]=w->pool.items[i].direction[1];}
  rc=m16_feature_pipeline_begin(&t->features,t->pcm,40960u*16u,t->reference,8192u*16u,w->physical_directions,w->pool.count,w->votes,480u*9u,
   t->output->features,16u*20u*130u,t->output->directions,t->output->valid,&prepared,t->epoch,t->first_job,t->first_tag+M16_RG_TAGS);
  if(rc)return draining(t,rc);
  t->features_started=1;t->phase=M16_DT_FEATURES;return 1;
 }
 if(t->phase==M16_DT_FEATURES){
  rc=m16_feature_pipeline_step(&t->features);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_DT_INPUT);
  t->features_done=1;
  rc=m16_frozen_score_task_begin(&t->score,t->output->features,16u*20u*130u,t->output->directions,t->output->valid,t->output->logits,t->output->scores);
  if(rc)return draining(t,rc);
  t->phase=M16_DT_SCORE;return 1;
 }
 if(t->phase==M16_DT_SCORE){
  rc=m16_frozen_score_task_step(&t->score);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_DT_INPUT);
  t->output->candidate_count=w->pool.count;t->phase=M16_DT_COMPLETE;return 0;
 }
 return draining(t,M16_DT_INVALID);
}
int m16_detection_stop(m16_detection_task *t){
 if(!valid(t))return M16_DT_INVALID;
 if(t->front_context&&!t->front_context->submitted){
  int rc=m16_front_context_stop(t->front_context);if(rc<0)return rc;t->phase=M16_DT_STOPPED;return rc;
 }
 if(core_releasable(t)){t->phase=M16_DT_STOPPED;return 1;}
 if(t->phase==M16_DT_FAILED)return t->error;
 (void)draining(t,0);return 0;
}
