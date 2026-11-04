#include "mic16_spatial_evidence_cache.h"
#include <string.h>
#define MAGIC 0x4d454331u
static int valid(const m16_spatial_evidence_cache *c){return c&&c->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int finite_value(float x){return x>=-2.000005f&&x<=2.000005f;}
static int fail(m16_spatial_evidence_cache *c,int rc){if(!c->error)c->error=rc;c->phase=M16_EC_FAILED;return c->error;}
static int lookup(void *p,uint64_t epoch,uint64_t frame,unsigned zone,unsigned grid,float *value){
 m16_spatial_evidence_cache *c=p;m16_spatial_evidence_slot *s;
 if(!valid(c)||!value||!epoch||zone>=24u||grid>=325u)return M16_EC_INVALID;
 s=&c->storage->slots[frame%20u];
 if(c->epoch!=epoch||!s->ready||s->epoch!=epoch||s->frame!=frame||!(s->exact_valid[zone][grid/8u]&(1u<<(grid%8u)))){c->exact_misses++;return 0;}
 *value=s->exact[zone*325u+grid];if(!finite_value(*value))return M16_EC_VALUE;
 c->exact_hits++;return 1;
}
static int store(void *p,uint64_t epoch,uint64_t frame,unsigned zone,unsigned grid,float value){
 m16_spatial_evidence_cache *c=p;m16_spatial_evidence_slot *s;unsigned char mask;
 if(!valid(c)||!epoch||zone>=24u||grid>=325u||!finite_value(value))return M16_EC_INVALID;
 s=&c->storage->slots[frame%20u];
 /* 前端可继续覆盖离开历史窗的槽；旧后端仍用自己的PHAT计算，只是不再回填已淘汰的槽。 */
 if(c->epoch!=epoch||!s->ready||s->epoch!=epoch||s->frame!=frame){c->evicted_stores++;return 1;}
 mask=(unsigned char)(1u<<(grid%8u));
 if(s->exact_valid[zone][grid/8u]&mask){
  if(memcmp(&s->exact[zone*325u+grid],&value,4u))return M16_EC_VALUE;
 }else{s->exact[zone*325u+grid]=value;s->exact_valid[zone][grid/8u]|=mask;c->exact_stores++;}
 return 0;
}
int m16_spatial_evidence_init(m16_spatial_evidence_cache *c,m16_spatial_evidence_storage *s,m16_coarse_scan_runtime *r){
 if(!c||!s||!r||c->magic||(uintptr_t)c%_Alignof(m16_spatial_evidence_cache)||(uintptr_t)s%_Alignof(m16_spatial_evidence_storage)||
  !m16_coarse_scan_available(r)||!apart(c,sizeof(*c),s,sizeof(*s))||!apart(c,sizeof(*c),r,sizeof(*r))||!apart(s,sizeof(*s),r,sizeof(*r)))return M16_EC_INVALID;
 *c=(m16_spatial_evidence_cache){0};c->magic=MAGIC;c->storage=s;c->coarse=r;
 c->view=(m16_pp_grid_cache){c,lookup,store,0};return 0;
}
int m16_spatial_evidence_releasable(const m16_spatial_evidence_cache *c){return valid(c)&&!c->owned&&(c->phase==M16_EC_IDLE||c->phase==M16_EC_RESERVED||c->phase==M16_EC_FAILED);}
int m16_spatial_evidence_start(m16_spatial_evidence_cache *c,uint64_t epoch){
 if(!valid(c)||!epoch||epoch<=c->epoch||!m16_spatial_evidence_releasable(c)||c->error)return M16_EC_INVALID;
 if(!m16_coarse_scan_available(c->coarse))return M16_EC_BUSY;
 for(unsigned i=0;i<20u;i++)c->storage->slots[i].ready=0;
 c->epoch=epoch;c->next_frame=0;c->phase=M16_EC_IDLE;c->phat=0;
 c->coarse_frames=c->exact_hits=c->exact_misses=c->exact_stores=c->evicted_stores=0;return 0;
}
int m16_spatial_evidence_reserve(m16_spatial_evidence_cache *c,uint64_t epoch,uint64_t frame){
 m16_spatial_evidence_slot *s;
 if(!valid(c)||c->phase!=M16_EC_IDLE||c->owned||epoch!=c->epoch||frame!=c->next_frame||frame==UINT64_MAX)return M16_EC_SEQUENCE;
 c->slot=(unsigned)(frame%20u);s=&c->storage->slots[c->slot];s->ready=0;s->epoch=epoch;s->frame=frame;
 c->phase=M16_EC_RESERVED;return 0;
}
int m16_spatial_evidence_begin(m16_spatial_evidence_cache *c,const float *p,unsigned values){
 if(!valid(c)||c->phase!=M16_EC_RESERVED||!p||values!=807u*32u*2u||(uintptr_t)p%4u||
  !apart(p,values*4u,c,sizeof(*c))||!apart(p,values*4u,c->storage,sizeof(*c->storage))||
  !apart(p,values*4u,c->coarse,sizeof(*c->coarse)))return M16_EC_INVALID;
 c->phat=p;c->cursor=0;c->phase=M16_EC_CLEAR;return 0;
}
int m16_spatial_evidence_step(m16_spatial_evidence_cache *c){
 int rc;unsigned end;m16_spatial_evidence_slot *s;
 if(!valid(c))return M16_EC_INVALID;
 if(c->phase==M16_EC_IDLE)return 0;
 if(c->phase==M16_EC_FAILED)return c->error;
 if(c->phase==M16_EC_STOPPING)return m16_spatial_evidence_stop(c);
 if(c->phase==M16_EC_RESERVED)return 1;
 s=&c->storage->slots[c->slot];
 if(c->phase==M16_EC_CLEAR){
  end=c->cursor+64u;if(end>24u*41u)end=24u*41u;
  memset((unsigned char *)s->exact_valid+c->cursor,0,end-c->cursor);c->cursor=end;
  if(end==24u*41u){
   rc=m16_coarse_scan_submit(c->coarse,c->phat,807u*32u*2u,s->coarse,24u*325u,c->epoch,c->next_frame);
   if(rc)return fail(c,rc);
   c->owned=1;c->phase=M16_EC_WAIT;
  }return 1;
 }
 if(c->phase!=M16_EC_WAIT)return fail(c,M16_EC_INVALID);
 rc=m16_coarse_scan_poll(c->coarse);if(rc<0)return fail(c,rc);if(rc==1)return 1;
 if(rc==2)return fail(c,M16_EC_SEQUENCE);
 rc=m16_coarse_scan_retire(c->coarse);if(rc)return fail(c,rc);
 c->owned=0;c->phat=0;s->ready=1;c->next_frame++;c->coarse_frames++;c->phase=M16_EC_IDLE;return 0;
}
int m16_spatial_evidence_stop(m16_spatial_evidence_cache *c){
 int rc;if(!valid(c))return M16_EC_INVALID;
 if(c->owned){
  rc=m16_coarse_scan_cancel(c->coarse);if(rc<0)return fail(c,rc);
  if(!rc){
   rc=m16_coarse_scan_poll(c->coarse);if(rc<0)return fail(c,rc);
   if(rc==1){c->phase=M16_EC_STOPPING;return 0;}
  }
  if(!m16_coarse_scan_available(c->coarse)){
   rc=m16_coarse_scan_retire(c->coarse);if(rc)return fail(c,rc);
  }
  c->owned=0;
 }
 c->phat=0;c->phase=c->error?M16_EC_FAILED:M16_EC_IDLE;return c->error?c->error:1;
}
int m16_spatial_evidence_coarse(const m16_spatial_evidence_cache *c,uint64_t epoch,uint64_t frame,const float **out){
 const m16_spatial_evidence_slot *s;
 if(!valid(c)||!out||c->epoch!=epoch)return M16_EC_INVALID;
 s=&c->storage->slots[frame%20u];
 if(!s->ready||s->epoch!=epoch||s->frame!=frame)return M16_EC_SEQUENCE;
 *out=s->coarse;return 0;
}
