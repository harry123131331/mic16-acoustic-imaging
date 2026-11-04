#include "mic16_front_ring.h"
#include <string.h>
#define MAGIC 0x4d465231u
static int valid(const m16_front_ring *r){return r&&r->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int device_free(const m16_front_ring *r){
 return (!r->front.provider||!r->front.provider->owns(r->front.provider->context,&r->front))&&
  (!r->evidence_enabled||!r->evidence.owned);
}
static int fail(m16_front_ring *r,int rc){r->error=rc;r->input=0;r->phase=M16_FR_FAILED;return rc;}
int m16_front_ring_init(m16_front_ring *r,m16_front_ring_storage *s,mic16_spectral_workspace *w){
 int rc;
 if(!r||!s||!w||r->magic||(uintptr_t)r%_Alignof(m16_front_ring)||
  (uintptr_t)s%_Alignof(m16_front_ring_storage)||(uintptr_t)w%_Alignof(mic16_spectral_workspace)||
  !apart(r,sizeof(*r),s,sizeof(*s))||!apart(r,sizeof(*r),w,sizeof(*w))||!apart(s,sizeof(*s),w,sizeof(*w)))return M16_FR_INVALID;
 *r=(m16_front_ring){0};rc=m16_public_spectrum_init(&r->front,&s->spectrum[0],w);if(rc)return rc;
 r->magic=MAGIC;r->storage=s;r->work=w;r->phase=M16_FR_STOPPED;return 0;
}
int m16_front_ring_bind_evidence(m16_front_ring *r,m16_coarse_scan_runtime *c){
 int rc;
 if(!valid(r)||!c||(M16_PROJECTION_VERSION!=5u&&M16_PROJECTION_VERSION!=6u)||r->evidence_enabled||!m16_front_ring_releasable(r)||
  !apart(r,sizeof(*r),c,sizeof(*c))||!apart(r->storage,sizeof(*r->storage),c,sizeof(*c))||
  !apart(r->work,sizeof(*r->work),c,sizeof(*c)))return M16_FR_INVALID;
 rc=m16_spatial_evidence_init(&r->evidence,&r->storage->evidence,c);if(rc)return rc;
 r->evidence_enabled=1;return 0;
}
int m16_front_ring_releasable(const m16_front_ring *r){
 return valid(r)&&r->phase==M16_FR_STOPPED&&!r->reader&&!r->input&&device_free(r);
}
int m16_front_ring_start(m16_front_ring *r,uint64_t epoch,uint64_t tick){
 if(!valid(r)||!epoch||epoch<=r->epoch)return M16_FR_SEQUENCE;
 if(!m16_front_ring_releasable(r))return M16_FR_BUSY;
 if(r->evidence_enabled){int rc=m16_spatial_evidence_start(&r->evidence,epoch);if(rc)return rc;}
 memset(r->metadata,0,sizeof(r->metadata));r->epoch=epoch;r->next_frame=r->generation=0;
 r->head=r->valid_frames=r->cursor=0;r->frames_in=r->frames_out=r->contexts=0;
 r->last_tick=tick;r->last_received_tick=tick;r->error=0;r->phase=M16_FR_IDLE;return 0;
}
int m16_front_ring_begin(m16_front_ring *r,const float *x,unsigned n,uint64_t epoch,uint64_t frame,uint64_t first,uint64_t tick){
 if(!valid(r)||!x||(uintptr_t)x%_Alignof(float)||n!=M16_FR_PCM_VALUES||
  !apart(x,n*4u,r,sizeof(*r))||!apart(x,n*4u,r->storage,sizeof(*r->storage))||!apart(x,n*4u,r->work,sizeof(*r->work)))return M16_FR_INVALID;
 if(r->phase!=M16_FR_IDLE||r->reader||!device_free(r))return M16_FR_BUSY;
 if(epoch!=r->epoch||frame!=r->next_frame||frame>UINT64_MAX/2048u-1u||first!=frame*2048u)return M16_FR_SEQUENCE;
 if(tick<r->last_received_tick)return M16_FR_CLOCK;
 if(r->generation==UINT64_MAX||r->frames_in==UINT64_MAX)return M16_FR_SEQUENCE;
 if(r->evidence_enabled){int rc=m16_spatial_evidence_reserve(&r->evidence,epoch,frame);if(rc)return rc;}
 r->last_received_tick=tick;r->metadata[r->head]=(m16_front_frame_metadata){epoch,frame,first,tick,0,0};
 r->input=x;r->cursor=0;r->frames_in++;r->phase=M16_FR_COPY;return 0;
}
int m16_front_ring_begin_buffered(m16_front_ring *r,uint64_t epoch,uint64_t frame,uint64_t tick){
 int rc;
 if(!valid(r)||frame>=M16_FR_FRAMES||epoch!=r->epoch||frame!=r->next_frame||r->head!=frame)return M16_FR_SEQUENCE;
 if(r->phase!=M16_FR_IDLE||r->reader||r->input||!device_free(r))return M16_FR_BUSY;
 if(tick<r->last_received_tick)return M16_FR_CLOCK;
 if(r->generation==UINT64_MAX||r->frames_in==UINT64_MAX)return M16_FR_SEQUENCE;
 if(r->evidence_enabled){rc=m16_spatial_evidence_reserve(&r->evidence,epoch,frame);if(rc)return rc;}
 r->last_received_tick=tick;r->metadata[r->head]=(m16_front_frame_metadata){epoch,frame,frame*2048u,tick,0,0};
 r->cursor=0;r->frames_in++;
 rc=m16_public_spectrum_retarget(&r->front,&r->storage->spectrum[r->head]);if(rc)return fail(r,rc);
 rc=m16_public_spectrum_begin(&r->front,r->storage->pcm[r->head],M16_FR_PCM_VALUES,r->epoch,r->next_frame);
 if(rc)return fail(r,rc);
 r->phase=M16_FR_FRONT;return 0;
}
int m16_front_ring_input_releasable(const m16_front_ring *r){return valid(r)&&!r->input;}
int m16_front_ring_step(m16_front_ring *r,uint64_t tick){
 unsigned end,i;int rc;
 if(!valid(r))return M16_FR_INVALID;
 if(tick<r->last_tick)return fail(r,M16_FR_CLOCK);
 r->last_tick=tick;
 if(r->phase==M16_FR_IDLE)return 0;
 if(r->phase==M16_FR_STOPPED)return 2;
 if(r->phase==M16_FR_FAILED)return r->error;
 if(r->phase==M16_FR_STOPPING)return m16_front_ring_stop(r);
 if(r->phase==M16_FR_COPY){
  end=r->cursor+64u;if(end>M16_FR_PCM_VALUES)end=M16_FR_PCM_VALUES;
  for(i=r->cursor;i<end;i++){
   float v=r->input[i];if(!(v>=-1.f&&v<=1.f))return fail(r,M16_FR_INPUT);
   r->storage->pcm[r->head][i]=v;
  }
  r->cursor=end;if(end==M16_FR_PCM_VALUES){
   r->input=0;rc=m16_public_spectrum_retarget(&r->front,&r->storage->spectrum[r->head]);if(rc)return fail(r,rc);
   rc=m16_public_spectrum_begin(&r->front,r->storage->pcm[r->head],M16_FR_PCM_VALUES,r->epoch,r->next_frame);
   if(rc)return fail(r,rc);
   r->phase=M16_FR_FRONT;
  }return 1;
 }
 if(r->phase==M16_FR_FRONT){
  rc=m16_public_spectrum_step(&r->front);if(rc<0)return fail(r,rc);if(rc==1)return 1;
  if(rc==2)return fail(r,M16_FR_SEQUENCE);
  if(r->evidence_enabled){
   rc=m16_spatial_evidence_begin(&r->evidence,r->storage->spectrum[r->head].phat,807u*32u*2u);if(rc)return fail(r,rc);
   r->phase=M16_FR_SPATIAL;return 1;
  }
 }else if(r->phase==M16_FR_SPATIAL){
  rc=m16_spatial_evidence_step(&r->evidence);if(rc<0)return fail(r,rc);if(rc==1)return 1;
 }else return fail(r,M16_FR_INVALID);
 r->metadata[r->head].front_done_tick=tick;r->metadata[r->head].ready=1;
 r->head=(r->head+1u)%M16_FR_FRAMES;if(r->valid_frames<M16_FR_FRAMES)r->valid_frames++;
 r->next_frame++;r->generation++;r->frames_out++;r->phase=M16_FR_IDLE;return 0;
}
static int lease_valid(const m16_front_ring *r,const m16_front_context_lease *v){
 return valid(r)&&v&&r->reader&&v->owner==r&&v->epoch==r->epoch&&v->generation==r->generation&&
  r->valid_frames==20u&&v->head==r->head&&v->first_frame==r->next_frame-20u;
}
int m16_front_ring_acquire(m16_front_ring *r,m16_front_context_lease *v){
 if(!valid(r)||!v||(uintptr_t)v%_Alignof(m16_front_context_lease)||!apart(v,sizeof(*v),r,sizeof(*r))||
  !apart(v,sizeof(*v),r->storage,sizeof(*r->storage))||!apart(v,sizeof(*v),r->work,sizeof(*r->work)))return M16_FR_INVALID;
 if(r->phase!=M16_FR_IDLE||r->reader)return M16_FR_BUSY;
 if(r->valid_frames!=20u)return M16_FR_EMPTY;
 *v=(m16_front_context_lease){r,r->epoch,r->generation,r->next_frame-20u,r->head};r->reader=1;r->contexts++;return 0;
}
int m16_front_ring_frame(const m16_front_ring *r,const m16_front_context_lease *v,unsigned index,
 const m16_front_frame_metadata **meta,const float **pcm,const mic16_spectral_frame **spectrum){
 unsigned slot;
 if(!lease_valid(r,v)||index>=20u||!meta||!pcm||!spectrum)return M16_FR_LEASE;
 slot=(v->head+index)%20u;
 if(!r->metadata[slot].ready||r->metadata[slot].epoch!=v->epoch||r->metadata[slot].frame_seq!=v->first_frame+index)return M16_FR_SEQUENCE;
 *meta=&r->metadata[slot];*pcm=r->storage->pcm[slot];*spectrum=&r->storage->spectrum[slot];return 0;
}
int m16_front_ring_release(m16_front_ring *r,const m16_front_context_lease *v){
 if(!lease_valid(r,v))return M16_FR_LEASE;
 r->reader=0;return 0;
}
int m16_front_ring_stop(m16_front_ring *r){
 int rc;if(!valid(r))return M16_FR_INVALID;
 r->input=0;rc=m16_public_spectrum_stop(&r->front);
 if(rc<0){r->phase=M16_FR_STOPPING;if(!r->error)r->error=rc;return rc;}
 if(r->evidence_enabled){
  rc=m16_spatial_evidence_stop(&r->evidence);
  if(rc<0){
   if(!r->error)r->error=rc;
   r->phase=m16_spatial_evidence_releasable(&r->evidence)&&device_free(r)?M16_FR_STOPPED:M16_FR_STOPPING;
   return rc;
  }
 }
 if(!device_free(r)){r->phase=M16_FR_STOPPING;return 0;}
 r->phase=M16_FR_STOPPED;return 1;
}
