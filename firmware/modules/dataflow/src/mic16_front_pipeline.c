#include "mic16_front_pipeline.h"
#include <stdint.h>
#define MAGIC 0x46504c31u
static int valid(const m16_front_pipeline *p){return p&&p->magic==MAGIC&&p->ring;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int drained(const m16_front_pipeline *p){
 return p->front_phase==M16_FPL_NONE&&p->ring->evidence.phase==M16_EC_IDLE&&
  !p->ring->evidence.owned&&!p->ring->input;
}
static int fail(m16_front_pipeline *p,int rc){
 if(!p->error)p->error=rc;
 p->ring->error=p->error;p->ring->phase=M16_FR_FAILED;return p->error;
}
int m16_front_pipeline_init(m16_front_pipeline *p,m16_front_ring *r){
 if(!p||!r||p->magic||(uintptr_t)p%_Alignof(m16_front_pipeline)||!m16_front_ring_releasable(r)||
  !r->evidence_enabled||!r->evidence.coarse||
  !apart(p,sizeof(*p),r,sizeof(*r))||!apart(p,sizeof(*p),r->storage,sizeof(*r->storage))||
  !apart(p,sizeof(*p),r->work,sizeof(*r->work))||
  !apart(p,sizeof(*p),r->evidence.coarse,sizeof(*r->evidence.coarse)))return M16_FR_INVALID;
 *p=(m16_front_pipeline){0};p->magic=MAGIC;p->ring=r;return 0;
}
int m16_front_pipeline_releasable(const m16_front_pipeline *p){
 return valid(p)&&!p->active&&m16_front_ring_releasable(p->ring);
}
int m16_front_pipeline_start(m16_front_pipeline *p,uint64_t epoch,uint64_t tick){
 int rc;
 if(!valid(p)||!m16_front_pipeline_releasable(p))return M16_FR_BUSY;
 rc=m16_front_ring_start(p->ring,epoch,tick);if(rc)return rc;
 p->active=1;p->front_phase=M16_FPL_NONE;p->pause_requested=p->cursor=p->front_slot=0;
 p->error=0;p->next_accept=p->front_frame=0;p->last_tick=tick;
 p->steps=p->overlap_steps=p->backpressure=p->committed=p->discarded_on_stop=0;
 p->ring->phase=M16_FR_PIPELINED;return 0;
}
int m16_front_pipeline_can_accept(const m16_front_pipeline *p){
 return valid(p)&&p->active&&!p->error&&!p->pause_requested&&p->ring->phase==M16_FR_PIPELINED&&
  !p->ring->reader&&p->front_phase==M16_FPL_NONE&&p->next_accept-p->ring->next_frame<2u;
}
int m16_front_pipeline_begin(m16_front_pipeline *p,const float *x,unsigned n,uint64_t epoch,
 uint64_t frame,uint64_t first,uint64_t tick){
 m16_front_ring *r;unsigned slot;m16_spatial_evidence_slot *s;
 if(!valid(p))return M16_FR_INVALID;
 r=p->ring;
 if(!x||(uintptr_t)x%_Alignof(float)||n!=M16_FR_PCM_VALUES||
  !apart(x,n*4u,p,sizeof(*p))||!apart(x,n*4u,r,sizeof(*r))||
  !apart(x,n*4u,r->storage,sizeof(*r->storage))||!apart(x,n*4u,r->work,sizeof(*r->work))||
  !apart(x,n*4u,r->evidence.coarse,sizeof(*r->evidence.coarse)))return M16_FR_INVALID;
 if(!m16_front_pipeline_can_accept(p)){p->backpressure++;return M16_FR_BUSY;}
 if(epoch!=r->epoch||frame!=p->next_accept||frame>UINT64_MAX/2048u-1u||first!=frame*2048u||
  r->frames_in==UINT64_MAX||r->generation>UINT64_MAX-(p->next_accept-r->next_frame+1u))return M16_FR_SEQUENCE;
 if(tick<r->last_received_tick)return M16_FR_CLOCK;
 slot=(unsigned)(frame%20u);
 /* 新写入即使旧槽失效；不能把仍有20块物理存储误报成20帧已提交历史。 */
 if(r->metadata[slot].ready&&r->valid_frames)r->valid_frames--;
 r->metadata[slot]=(m16_front_frame_metadata){epoch,frame,first,tick,0,0};
 s=&r->storage->evidence.slots[slot];s->ready=0;
 r->last_received_tick=tick;r->input=x;r->frames_in++;
 p->front_slot=slot;p->front_frame=frame;p->cursor=0;p->front_phase=M16_FPL_COPY;p->next_accept++;
 return 0;
}
static int submit_ready(m16_front_pipeline *p){
 m16_front_ring *r=p->ring;int rc;
 if(p->front_phase!=M16_FPL_READY||r->evidence.phase!=M16_EC_IDLE)return 0;
 rc=m16_spatial_evidence_reserve(&r->evidence,r->epoch,p->front_frame);if(rc)return fail(p,rc);
 rc=m16_spatial_evidence_begin(&r->evidence,r->storage->spectrum[p->front_slot].phat,807u*32u*2u);
 if(rc)return fail(p,rc);
 p->front_phase=M16_FPL_NONE;return 0;
}
int m16_front_pipeline_step(m16_front_pipeline *p,uint64_t tick){
 m16_front_ring *r;int rc;unsigned end;
 if(!valid(p))return M16_FR_INVALID;
 if(!p->active)return 2;
 if(p->error)return p->error;
 r=p->ring;
 if(tick<p->last_tick)return fail(p,M16_FR_CLOCK);
 p->last_tick=tick;r->last_tick=tick;p->steps++;
 if(r->evidence.phase!=M16_EC_IDLE){
  rc=m16_spatial_evidence_step(&r->evidence);if(rc<0)return fail(p,rc);
  if(!rc){
   unsigned slot=(unsigned)(r->next_frame%20u);
   if(!r->storage->evidence.slots[slot].ready||r->metadata[slot].frame_seq!=r->next_frame||
    r->metadata[slot].epoch!=r->epoch)return fail(p,M16_FR_SEQUENCE);
   r->metadata[slot].front_done_tick=tick;r->metadata[slot].ready=1;
   r->head=(slot+1u)%20u;if(r->valid_frames<20u)r->valid_frames++;
   r->next_frame++;r->generation++;r->frames_out++;p->committed++;
  }
 }
 if(p->front_phase==M16_FPL_COPY){
  end=p->cursor+64u;if(end>M16_FR_PCM_VALUES)end=M16_FR_PCM_VALUES;
  for(unsigned i=p->cursor;i<end;i++){
   float v=r->input[i];if(!(v>=-1.f&&v<=1.f))return fail(p,M16_FR_INPUT);
   r->storage->pcm[p->front_slot][i]=v;
  }
  p->cursor=end;
  if(end==M16_FR_PCM_VALUES){
   r->input=0;rc=m16_public_spectrum_retarget(&r->front,&r->storage->spectrum[p->front_slot]);
   if(rc)return fail(p,rc);
   rc=m16_public_spectrum_begin(&r->front,r->storage->pcm[p->front_slot],M16_FR_PCM_VALUES,r->epoch,p->front_frame);
   if(rc)return fail(p,rc);
   p->front_phase=M16_FPL_FRONT;
  }
 }else if(p->front_phase==M16_FPL_FRONT){
  if(r->evidence.owned&&r->evidence.coarse->phase==M16_CS_DEVICE)p->overlap_steps++;
  rc=m16_public_spectrum_step(&r->front);if(rc<0)return fail(p,rc);
  if(rc==2)return fail(p,M16_FR_SEQUENCE);
  if(!rc)p->front_phase=M16_FPL_READY;
 }
 rc=submit_ready(p);if(rc)return rc;
 if(drained(p)){
  if(p->pause_requested)r->phase=M16_FR_IDLE;
  return 0;
 }
 return 1;
}
int m16_front_pipeline_pause(m16_front_pipeline *p){
 if(!valid(p)||!p->active||p->error)return M16_FR_INVALID;
 p->pause_requested=1;
 if(drained(p)){p->ring->phase=M16_FR_IDLE;return 1;}
 return 0;
}
int m16_front_pipeline_resume(m16_front_pipeline *p){
 if(!valid(p)||!p->active||p->error)return M16_FR_INVALID;
 if(!p->pause_requested||!drained(p)||p->ring->reader||p->ring->phase!=M16_FR_IDLE)return M16_FR_BUSY;
 p->pause_requested=0;p->ring->phase=M16_FR_PIPELINED;return 0;
}
int m16_front_pipeline_stop(m16_front_pipeline *p){
 int rc;
 if(!valid(p))return M16_FR_INVALID;
 if(!p->active)return m16_front_pipeline_releasable(p)?1:M16_FR_BUSY;
 p->pause_requested=1;
 rc=m16_front_ring_stop(p->ring);
 if(rc<0&&!p->error)p->error=rc;
 if(!m16_front_ring_releasable(p->ring))return rc<0?rc:0;
 p->discarded_on_stop=p->next_accept-p->ring->next_frame;
 p->front_phase=M16_FPL_NONE;p->active=0;
 return p->error?p->error:1;
}
