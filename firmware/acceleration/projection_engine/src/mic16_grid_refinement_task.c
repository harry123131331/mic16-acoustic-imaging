#include "mic16_grid_refinement_task.h"
#include "mic16_spatial_tables.h"
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d475254u
#define GUARD 0.0002f
static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int valid(const m16_grid_refinement_task *t){return t&&t->magic==MAGIC;}
static int fail(m16_grid_refinement_task *t,int rc){t->phase=M16_RG_FAILED;t->error=rc;return rc;}
static int allowed(unsigned a,unsigned b){return (m16_second15[a][b/8u]>>(b%8u))&1u;}
static int float_environment(void){
#if defined(__arm__)
 uint32_t fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr));return !(fpscr&((3u<<22)|(1u<<24)));
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&((3u<<13)|(1u<<15)|(1u<<6)))return 0;
#endif
 return 1;
#endif
}
static void first_pass(m16_grid_refinement_task *t,unsigned phase){t->phase=phase;t->first=0;t->second=325;t->cursor=1;}
static void next_zone(m16_grid_refinement_task *t){
 if(++t->zone==24u)t->phase=M16_RG_COMPLETE;else{t->phase=M16_RG_CLEAR;t->cursor=0;}
}
static int outside_buffers(const m16_grid_refinement_task *t,const void *p,uintptr_t n){
 const m16_projection_runtime *r=t->projection.runtime;
 return overlap(p,n,t,sizeof(*t))||overlap(p,n,r,sizeof(*r))||overlap(p,n,r->arena,(uintptr_t)r->words*8u);
}
int m16_grid_refinement_init(m16_grid_refinement_task *t,m16_projection_runtime *r){
 int rc;
 if(!t||!r||((uintptr_t)t%_Alignof(m16_grid_refinement_task))||t->magic||!m16_projection_runtime_available(r)||
    overlap(t,sizeof(*t),r,sizeof(*r))||overlap(t,sizeof(*t),r->arena,(uintptr_t)r->words*8u))return M16_RG_INVALID;
 memset(t,0,sizeof(*t));rc=m16_physical_projection_init(&t->projection,r);if(rc)return rc;
 t->magic=MAGIC;t->reserved_tag_high=t->projection.reserved_tag_high;return 0;
}
int m16_grid_refinement_begin(m16_grid_refinement_task *t,const float *x,unsigned nx,float *maps,
                              unsigned nm,uint64_t epoch,uint64_t tag){
 int rc;
 if(!valid(t))return M16_RG_INVALID;
 if((t->phase!=M16_RG_IDLE&&t->phase!=M16_RG_COMPLETE&&t->phase!=M16_RG_STOPPED&&t->phase!=M16_RG_FAILED)||
    !m16_projection_runtime_schedulable(t->projection.runtime))return M16_RG_BUSY;
 if(!x||!maps||((uintptr_t)x%_Alignof(float))||((uintptr_t)maps%_Alignof(float))||nx<M16_SP_PHAT_FLOATS||nm<M16_SP_MAP_FLOATS||
    !epoch||!tag||tag>UINT64_MAX-(M16_RG_TAGS-1u)||tag<=t->reserved_tag_high||
    tag<=t->projection.reserved_tag_high||(!t->projection.runtime->arbitrated&&tag<=t->projection.runtime->tag_high)||
    outside_buffers(t,x,(uintptr_t)M16_SP_PHAT_FLOATS*4u)||outside_buffers(t,maps,(uintptr_t)M16_SP_MAP_FLOATS*4u)||
    overlap(x,(uintptr_t)M16_SP_PHAT_FLOATS*4u,maps,(uintptr_t)M16_SP_MAP_FLOATS*4u))return M16_RG_INVALID;
 if(!float_environment())return M16_PP_FLOAT_ENV;
 rc=m16_physical_projection_reuse_begin(&t->projection,x,nx,epoch,tag);if(rc)return rc;
 t->phat=x;t->maps=maps;t->epoch=epoch;t->first_tag=tag;t->reserved_tag_high=tag+M16_RG_TAGS-1u;
 t->zone=t->frame=t->cursor=t->first=t->second=t->next_grid=t->batch_count=t->full=t->stop_requested=0;
 t->zero=1;t->batches=t->last_items=t->max_items=0;t->steps=0;t->error=0;
 t->report=(m16_scanner_refinement){0};t->phase=M16_RG_VALIDATE_PHAT;return 0;
}
int m16_grid_refinement_begin_cached(m16_grid_refinement_task *t,const float *x,unsigned nx,float *maps,unsigned nm,
 uint64_t epoch,uint64_t tag,m16_pp_grid_cache *c,uint64_t first_frame){
 int rc=m16_grid_refinement_begin(t,x,nx,maps,nm,epoch,tag);if(rc)return rc;
 rc=m16_physical_projection_use_grid_cache(&t->projection,c,first_frame);
 if(rc){m16_physical_projection_reuse_end(&t->projection);return fail(t,rc);}return 0;
}
static int run_step(m16_grid_refinement_task *t){
 unsigned i,end;int rc;const float *s;
 if(t->phase==M16_RG_COMPLETE)return 0;
 if(t->phase==M16_RG_STOPPED)return 2;
 if(t->phase==M16_RG_FAILED)return t->error;
 if(t->phase==M16_RG_IDLE)return M16_RG_INVALID;
 if(!float_environment()&&t->phase!=M16_RG_DEVICE)return fail(t,M16_PP_FLOAT_ENV);
 if(!t->projection.runtime->arbitrated&&t->phase!=M16_RG_DEVICE&&!m16_projection_runtime_available(t->projection.runtime))return fail(t,M16_RG_OWNERSHIP);
 end=t->cursor+64u;
 if(t->phase==M16_RG_VALIDATE_PHAT){
  if(end>M16_SP_PHAT_FLOATS)end=M16_SP_PHAT_FLOATS;
  for(i=t->cursor;i<end;i++){
   float v=t->phat[i];if(!(v>=-1.000001f&&v<=1.000001f))return fail(t,M16_RG_INPUT);
   if(v!=0.f)t->zero=0;
   t->last_items++;
  }
  t->cursor=end;if(end==M16_SP_PHAT_FLOATS){t->phase=M16_RG_VALIDATE_MAPS;t->cursor=0;}return 1;
 }
 if(t->phase==M16_RG_VALIDATE_MAPS){
  if(end>M16_SP_MAP_FLOATS)end=M16_SP_MAP_FLOATS;
  for(i=t->cursor;i<end;i++){float v=t->maps[i];if(!(v>=-2.f&&v<=2.f))return fail(t,M16_RG_INPUT);t->last_items++;}
  t->cursor=end;if(end==M16_SP_MAP_FLOATS){
   t->cursor=0;t->report.zero_input=t->zero;t->phase=t->zero?M16_RG_ZERO:M16_RG_CLEAR;
  }
  return 1;
 }
 if(t->phase==M16_RG_ZERO){
  if(end>M16_SP_MAP_FLOATS)end=M16_SP_MAP_FLOATS;
  for(i=t->cursor;i<end;i++){t->maps[i]=0.f;t->last_items++;}
  t->cursor=end;if(end==M16_SP_MAP_FLOATS){t->phase=M16_RG_COMPLETE;return 0;}return 1;
 }
 if(t->phase==M16_RG_CLEAR){
  if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){t->selected[i]=t->computed[i]=0;t->last_items++;}
  t->cursor=end;if(end==325u){t->frame=0;t->full=0;first_pass(t,M16_RG_FIRST);}return 1;
 }
 if(t->phase==M16_RG_FIRST||t->phase==M16_RG_CHECK_FIRST){
  s=t->maps+(t->frame*24u+t->zone)*325u;if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){if(s[i]>s[t->first])t->first=i;t->last_items++;}
  t->cursor=end;if(end==325u){t->cursor=0;t->second=325;t->phase=t->phase==M16_RG_FIRST?M16_RG_SECOND:M16_RG_CHECK_SECOND;}return 1;
 }
 if(t->phase==M16_RG_SECOND||t->phase==M16_RG_CHECK_SECOND){
  unsigned checking=t->phase==M16_RG_CHECK_SECOND;
  s=t->maps+(t->frame*24u+t->zone)*325u;if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){
   if(allowed(t->first,i)&&(t->second==325u||s[i]>s[t->second]))t->second=i;
   t->last_items++;
  }
  t->cursor=end;if(end==325u){
   if(t->second==325u)return fail(t,M16_RG_INPUT);
   t->cursor=0;
   if(checking&&(!t->computed[t->first]||!t->computed[t->second])){
    t->report.full_zones++;t->full=1;t->next_grid=t->batch_count=0;t->phase=M16_RG_LIST;
   }else t->phase=checking?M16_RG_CHECK_OUTSIDE:M16_RG_SELECT;
  }
  return 1;
 }
 if(t->phase==M16_RG_SELECT){
  s=t->maps+(t->frame*24u+t->zone)*325u;if(end>325u)end=325u;
  for(i=t->cursor;i<end;i++){
   if(s[i]>=s[t->first]-2.f*GUARD||(allowed(t->first,i)&&s[i]>=s[t->second]-2.f*GUARD))t->selected[i]=1;
   t->last_items++;
  }
  t->cursor=end;if(end==325u){
   if(++t->frame==20u){t->phase=M16_RG_LIST;t->next_grid=t->batch_count=0;}
   else first_pass(t,M16_RG_FIRST);
  }
  return 1;
 }
 if(t->phase==M16_RG_LIST){
  unsigned work=0;
  while(t->next_grid<325u&&t->batch_count<16u&&work<64u){
   unsigned g=t->next_grid++;
   if(!t->computed[g]&&(t->full||t->selected[g]))t->batch[t->batch_count++]=g;
   work++;
  }
  t->last_items=work;
  if(t->batch_count==16u||(t->next_grid==325u&&t->batch_count))t->phase=M16_RG_LAUNCH;
  else if(t->next_grid==325u){
   if(t->full)next_zone(t);else{t->frame=0;first_pass(t,M16_RG_CHECK_FIRST);}
  }
  return t->phase==M16_RG_COMPLETE?0:1;
 }
 if(t->phase==M16_RG_LAUNCH){
  if(t->batches>=M16_RG_MAX_BATCHES)return fail(t,M16_RG_TAGS);
  rc=m16_physical_projection_begin_grids(&t->projection,t->phat,M16_SP_PHAT_FLOATS,t->batch,t->batch_count,t->zone,
       t->maps,M16_SP_MAP_FLOATS,t->epoch,t->first_tag+(uint64_t)t->batches*168u);
  if(rc)return fail(t,rc);
  t->phase=M16_RG_DEVICE;t->last_items=1;return 1;
 }
 if(t->phase==M16_RG_DEVICE){
  rc=m16_physical_projection_step(&t->projection);t->last_items=t->projection.last_step_items;
  if(rc<0)return fail(t,rc);
  if(rc==1)return 1;
  if(t->stop_requested||rc==2){t->phase=M16_RG_STOPPED;return 2;}
  for(i=0;i<t->batch_count;i++)t->computed[t->batch[i]]=1;
  t->report.refined_grids+=t->batch_count;t->batches++;t->batch_count=0;t->phase=M16_RG_LIST;return 1;
 }
 if(t->phase!=M16_RG_CHECK_OUTSIDE)return fail(t,M16_RG_INVALID);
 s=t->maps+(t->frame*24u+t->zone)*325u;if(end>325u)end=325u;
 for(i=t->cursor;i<end;i++){
  /* 保留原FP32加法舍入，不提升到FP64比较保护带。 */
  if(!t->computed[i]&&(s[i]+GUARD>=s[t->first]||(allowed(t->first,i)&&s[i]+GUARD>=s[t->second]))){
   t->report.full_zones++;t->full=1;t->next_grid=t->batch_count=0;t->phase=M16_RG_LIST;t->last_items++;return 1;
  }
  t->last_items++;
 }
 t->cursor=end;if(end==325u){if(++t->frame==20u)next_zone(t);else first_pass(t,M16_RG_CHECK_FIRST);}
 return t->phase==M16_RG_COMPLETE?0:1;
}
int m16_grid_refinement_step(m16_grid_refinement_task *t){
 int rc;if(!valid(t))return M16_RG_INVALID;t->last_items=0;t->steps++;
 rc=run_step(t);if(rc!=1)m16_physical_projection_reuse_end(&t->projection);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_grid_refinement_stop(m16_grid_refinement_task *t){
 int rc;if(!valid(t))return M16_RG_INVALID;
 if(t->phase==M16_RG_FAILED)return t->error;
 t->stop_requested=1;
 if(t->phase==M16_RG_DEVICE){
  rc=m16_physical_projection_stop(&t->projection);if(rc<0)return fail(t,rc);if(!rc)return 0;
 }
 m16_physical_projection_reuse_end(&t->projection);t->phase=M16_RG_STOPPED;return 1;
}
