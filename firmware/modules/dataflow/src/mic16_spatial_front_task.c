#include "mic16_spatial_front_task.h"
#include <string.h>
#define MAGIC 0x4d534654u
enum {FRONT_BEGIN=0,FRONT_COMPUTE=1,FRONT_PHAT=2,FRONT_LEVELS=3,FRONT_NEXT=4,FRONT_DONE=5};
static int valid(const m16_spatial_front_task *t){return t&&t->magic==MAGIC;}
static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int all_disjoint(const void *const *p,const uintptr_t *n,unsigned count){
 unsigned i,j;for(i=0;i<count;i++)for(j=i+1;j<count;j++)if(overlap(p[i],n[i],p[j],n[j]))return 0;return 1;
}
int m16_spatial_front_init(m16_spatial_front_task *t,mic16_spectral_frame *frame,mic16_spectral_workspace *work,
                           m16_coarse_scan_runtime *coarse,m16_projection_runtime *projection){
 int rc;const void *p[6];uintptr_t n[6];uint64_t ca,ce,pa,pe;
 if(!t||!frame||!work||!coarse||!projection||((uintptr_t)t%_Alignof(m16_spatial_front_task))||t->magic||
    !m16_coarse_scan_available(coarse)||!m16_projection_runtime_available(projection))return M16_SF_INVALID;
 p[0]=t;n[0]=sizeof(*t);p[1]=frame;n[1]=sizeof(*frame);p[2]=work;n[2]=sizeof(*work);
 p[3]=coarse;n[3]=sizeof(*coarse);p[4]=projection;n[4]=sizeof(*projection);p[5]=projection->arena;n[5]=(uintptr_t)projection->words*8u;
 if(!all_disjoint(p,n,6))return M16_SF_INVALID;
 ca=(uint64_t)coarse->io.arena_address-64u;ce=ca+sizeof(coarse->arena);
 pa=projection->io.arena_address;pe=pa+(uint64_t)projection->words*8u;
 if(ca<pe&&pa<ce)return M16_SF_INVALID;
 memset(t,0,sizeof(*t));t->coarse=coarse;
 rc=m16_public_spectrum_init(&t->spectrum,frame,work);if(rc)return rc;
 rc=m16_grid_refinement_init(&t->refinement,projection);if(rc)return rc;
 t->magic=MAGIC;return 0;
}
int m16_spatial_front_bind_evidence(m16_spatial_front_task *t,m16_pp_grid_cache *cache){
 if(!valid(t)||!cache||!cache->context||!cache->lookup||!cache->store||t->evidence_cache||
  (t->phase!=M16_SF_IDLE&&t->phase!=M16_SF_STOPPED)||t->refinement.epoch||!m16_spatial_front_releasable(t)||(M16_PROJECTION_VERSION!=5u&&M16_PROJECTION_VERSION!=6u))return M16_SF_INVALID;
 t->evidence_cache=cache;return 0;
}
int m16_spatial_front_releasable(const m16_spatial_front_task *t){
 return valid(t)&&(t->phase==M16_SF_IDLE||t->phase==M16_SF_COMPLETE||t->phase==M16_SF_STOPPED||t->phase==M16_SF_FAILED)&&
  (t->evidence_cache||m16_coarse_scan_available(t->coarse))&&
  (!t->spectrum.provider||!t->spectrum.provider->owns(t->spectrum.provider->context,&t->spectrum))&&
  m16_projection_runtime_client_released(t->refinement.projection.runtime,&t->refinement.projection);
}
static int begin_spatial(m16_spatial_front_task *t,const float *pcm,unsigned np,float *phat,unsigned nh,
 double *levels,unsigned nl,float *maps,unsigned nm,uint64_t epoch,uint64_t first,uint64_t tag,unsigned evidence){
 const void *p[10];uintptr_t n[10];unsigned i,j;
 if(!valid(t))return M16_SF_INVALID;
 if(!m16_spatial_front_releasable(t))return M16_SF_BUSY;
 if((t->evidence_cache!=0)!=(evidence!=0))return M16_SF_INVALID;
 if(!pcm||!phat||!levels||!maps||((uintptr_t)pcm%_Alignof(float))||((uintptr_t)phat%_Alignof(float))||
    ((uintptr_t)levels%_Alignof(double))||((uintptr_t)maps%_Alignof(float))||np<40960u*16u||nh<M16_SP_PHAT_FLOATS||
    nl<20u*24u||nm<M16_SP_MAP_FLOATS||!epoch||first%40960u||first>UINT64_MAX-40960u||!tag||
    tag>UINT64_MAX-(M16_RG_TAGS-1u)||tag<=t->refinement.reserved_tag_high||
    (!t->refinement.projection.runtime->arbitrated&&tag<=t->refinement.projection.runtime->tag_high))return M16_SF_INVALID;
 if(!evidence&&(epoch<t->coarse->last_epoch||(epoch>t->coarse->last_epoch&&first!=0)||
    (epoch==t->coarse->last_epoch&&first/2048u<=t->coarse->last_frame)))return M16_SF_SEQUENCE;
 p[0]=t;n[0]=sizeof(*t);p[1]=t->spectrum.work;n[1]=sizeof(*t->spectrum.work);p[2]=t->spectrum.output;n[2]=sizeof(*t->spectrum.output);
 p[3]=t->coarse;n[3]=sizeof(*t->coarse);p[4]=t->refinement.projection.runtime;n[4]=sizeof(*t->refinement.projection.runtime);
 p[5]=t->refinement.projection.runtime->arena;n[5]=(uintptr_t)t->refinement.projection.runtime->words*8u;
 p[6]=pcm;n[6]=40960u*16u*4u;p[7]=phat;n[7]=(uintptr_t)M16_SP_PHAT_FLOATS*4u;
 p[8]=levels;n[8]=20u*24u*8u;p[9]=maps;n[9]=M16_SP_MAP_FLOATS*4u;
 for(i=0;i<10;i++)for(j=i+1;j<10;j++)if(overlap(p[i],n[i],p[j],n[j]))return M16_SF_INVALID;
 t->pcm=pcm;t->phat=phat;t->levels=levels;t->maps=maps;t->epoch=epoch;t->first_sample=first;t->first_frame=first/2048u;t->first_tag=tag;
 t->front_index=t->ready=t->submitted=t->completed=t->cursor=t->turn=t->stop_requested=0;
 t->front_phase=FRONT_BEGIN;t->error=0;t->steps=t->front_steps_while_device=0;t->phase=M16_SF_ACTIVE;return 0;
}
int m16_spatial_front_begin(m16_spatial_front_task *t,const float *pcm,unsigned np,float *phat,unsigned nh,
 double *levels,unsigned nl,float *maps,unsigned nm,uint64_t epoch,uint64_t first,uint64_t tag){
 return begin_spatial(t,pcm,np,phat,nh,levels,nl,maps,nm,epoch,first,tag,0u);
}
int m16_spatial_front_begin_evidence(m16_spatial_front_task *t,const float *pcm,unsigned np,float *phat,unsigned nh,
 double *levels,unsigned nl,float *maps,unsigned nm,uint64_t epoch,uint64_t first,uint64_t tag){
 int rc=begin_spatial(t,pcm,np,phat,nh,levels,nl,maps,nm,epoch,first,tag,1u);if(rc)return rc;
 t->front_phase=FRONT_DONE;t->front_index=t->ready=t->submitted=t->completed=20u;return 0;
}
int m16_spatial_front_begin_prepared(m16_spatial_front_task *t,const float *pcm,unsigned np,
 float *phat,unsigned nh,double *levels,unsigned nl,float *maps,unsigned nm,uint64_t epoch,uint64_t first,uint64_t tag){
 int rc=m16_spatial_front_begin(t,pcm,np,phat,nh,levels,nl,maps,nm,epoch,first,tag);
 if(rc)return rc;
 t->front_phase=FRONT_DONE;t->front_index=20u;t->ready=20u;return 0;
}
static int begin_stop(m16_spatial_front_task *t,int error){
 int rc;unsigned refinement_active=t->phase==M16_SF_REFINE;
 if(error&&!t->error)t->error=error;
 t->stop_requested=1;t->phase=M16_SF_STOPPING;
 m16_public_spectrum_stop(&t->spectrum);
 if(!t->evidence_cache&&!m16_coarse_scan_available(t->coarse)){
  rc=m16_coarse_scan_cancel(t->coarse);if(rc<0){t->phase=M16_SF_FAILED;if(!t->error)t->error=rc;return t->error;}
 }
 if(refinement_active||!m16_projection_runtime_client_released(t->refinement.projection.runtime,&t->refinement.projection)){
  rc=m16_grid_refinement_stop(&t->refinement);if(rc<0){t->phase=M16_SF_FAILED;if(!t->error)t->error=rc;return t->error;}
 }
 return 0;
}
static int stopping(m16_spatial_front_task *t){
 int rc;
 if(!t->evidence_cache&&!m16_coarse_scan_available(t->coarse)){
  rc=m16_coarse_scan_poll(t->coarse);
  if(rc<0){t->phase=M16_SF_FAILED;if(!t->error)t->error=rc;return t->error;}
  if(rc==1)return 1;
  rc=m16_coarse_scan_retire(t->coarse);if(rc){t->phase=M16_SF_FAILED;if(!t->error)t->error=rc;return t->error;}
  return 1;
 }
 if(!m16_projection_runtime_client_released(t->refinement.projection.runtime,&t->refinement.projection)){
  rc=m16_grid_refinement_step(&t->refinement);
  if(rc<0){t->phase=M16_SF_FAILED;if(!t->error)t->error=rc;return t->error;}
  return 1;
 }
 if(t->spectrum.provider&&t->spectrum.provider->owns(t->spectrum.provider->context,&t->spectrum))return 1;
 t->phase=t->error?M16_SF_FAILED:M16_SF_STOPPED;return t->error?t->error:2;
}
static int front_action(m16_spatial_front_task *t){
 unsigned i,end;int rc;
 if(t->coarse->phase==M16_CS_DEVICE)t->front_steps_while_device++;
 if(t->front_phase==FRONT_BEGIN){
  rc=m16_public_spectrum_begin(&t->spectrum,t->pcm+t->front_index*2048u*16u,2048u*16u,t->epoch,t->first_frame+t->front_index);
  if(rc)return rc;
  t->front_phase=FRONT_COMPUTE;return 1;
 }
 if(t->front_phase==FRONT_COMPUTE){
  rc=m16_public_spectrum_step(&t->spectrum);if(rc<0)return rc;if(rc==2)return M16_SF_SEQUENCE;
  if(!rc){t->front_phase=FRONT_PHAT;t->cursor=0;}return 1;
 }
 if(t->front_phase==FRONT_PHAT){
  end=t->cursor+64u;if(end>807u*32u*2u)end=807u*32u*2u;
  for(i=t->cursor;i<end;i++)t->phat[t->front_index*807u*32u*2u+i]=t->spectrum.output->phat[i];
  t->cursor=end;if(end==807u*32u*2u)t->front_phase=FRONT_LEVELS;return 1;
 }
 if(t->front_phase==FRONT_LEVELS){
  for(i=0;i<24u;i++)t->levels[t->front_index*24u+i]=t->spectrum.output->zone_level_db[i];
  t->ready++;t->front_phase=FRONT_NEXT;return 1;
 }
 if(t->front_phase==FRONT_NEXT){
  if(t->front_index==19u)t->front_phase=FRONT_DONE;
  else if(t->ready-t->submitted<1u){t->front_index++;t->front_phase=FRONT_BEGIN;}
  return 1;
 }
 return M16_SF_INVALID;
}
int m16_spatial_front_step(m16_spatial_front_task *t){
 int rc;unsigned can_front,coarse_busy;
 if(!valid(t))return M16_SF_INVALID;
 t->steps++;
 if(t->phase==M16_SF_COMPLETE)return 0;
 if(t->phase==M16_SF_STOPPED)return 2;
 if(t->phase==M16_SF_FAILED)return t->error;
 if(t->phase==M16_SF_STOPPING)return stopping(t);
 if(t->phase==M16_SF_REFINE){
  rc=m16_grid_refinement_step(&t->refinement);
  if(rc<0){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}
  if(rc==2){begin_stop(t,M16_SF_SEQUENCE);return t->phase==M16_SF_FAILED?t->error:1;}
  if(!rc){t->phase=M16_SF_COMPLETE;return 0;}return 1;
 }
 if(t->phase!=M16_SF_ACTIVE)return M16_SF_INVALID;
 if(t->ready>t->submitted&&m16_coarse_scan_available(t->coarse)){
  rc=m16_coarse_scan_submit(t->coarse,t->phat+t->submitted*807u*32u*2u,807u*32u*2u,
       t->maps+t->submitted*24u*325u,24u*325u,t->epoch,t->first_frame+t->submitted);
  if(rc){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}
  t->submitted++;return 1;
 }
 if(t->front_phase==FRONT_DONE&&t->completed==20u){
  rc=t->evidence_cache?
   m16_grid_refinement_begin_cached(&t->refinement,t->phat,M16_SP_PHAT_FLOATS,t->maps,M16_SP_MAP_FLOATS,t->epoch,t->first_tag,t->evidence_cache,t->first_frame):
   m16_grid_refinement_begin(&t->refinement,t->phat,M16_SP_PHAT_FLOATS,t->maps,M16_SP_MAP_FLOATS,t->epoch,t->first_tag);
  if(rc){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}
  t->phase=M16_SF_REFINE;return 1;
 }
 can_front=t->front_phase!=FRONT_DONE&&!(t->front_phase==FRONT_NEXT&&t->front_index<19u&&t->ready-t->submitted>=1u);
 coarse_busy=!m16_coarse_scan_available(t->coarse);
 if(coarse_busy&&(t->turn||!can_front)){
  t->turn=0;rc=m16_coarse_scan_poll(t->coarse);
  if(rc<0){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}
  if(rc==2){begin_stop(t,M16_SF_SEQUENCE);return t->phase==M16_SF_FAILED?t->error:1;}
  if(!rc){rc=m16_coarse_scan_retire(t->coarse);if(rc){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}t->completed++;}
  return 1;
 }
 if(can_front){
  t->turn=1;rc=front_action(t);if(rc<0){begin_stop(t,rc);return t->phase==M16_SF_FAILED?t->error:1;}return 1;
 }
 return M16_SF_INVALID;
}
int m16_spatial_front_stop(m16_spatial_front_task *t){
 if(!valid(t))return M16_SF_INVALID;
 if(t->phase==M16_SF_FAILED)return t->error;
 if(t->phase==M16_SF_STOPPED)return 1;
 begin_stop(t,0);if(t->phase==M16_SF_FAILED)return t->error;
 if((t->evidence_cache||m16_coarse_scan_available(t->coarse))&&
  (!t->spectrum.provider||!t->spectrum.provider->owns(t->spectrum.provider->context,&t->spectrum))&&
  m16_projection_runtime_client_released(t->refinement.projection.runtime,&t->refinement.projection)){
  t->phase=M16_SF_STOPPED;return 1;
 }
 return 0;
}
