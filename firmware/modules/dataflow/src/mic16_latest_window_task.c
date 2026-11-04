#include "mic16_latest_window_task.h"
#include <string.h>
#define MAGIC 0x4c575431u
static int valid(const m16_latest_window_task *t){return t&&t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int fail(m16_latest_window_task *t,int rc){if(!t->error)t->error=rc<0?rc:M16_LW_INVALID;return t->error;}
static void record(m16_latest_window_task *t){
 uint64_t index=t->results+t->cancelled_jobs;
 if(index>=M16_LW_RECORDS)t->records_evicted++;
 t->records[index%M16_LW_RECORDS]=t->current;
}
int m16_latest_window_init(m16_latest_window_task *t,m16_pcm_acquisition *p,m16_front_ring *r,
 m16_fast_doa_task *d,m16_latest_window_storage *s){
 if(!t||t->magic||!p||!r||!d||!s||(uintptr_t)t%_Alignof(m16_latest_window_task)||
  (uintptr_t)s%_Alignof(m16_latest_window_storage)||!m16_front_ring_releasable(r)||!r->evidence_enabled||d->active||
  !apart(t,sizeof(*t),p,sizeof(*p))||!apart(t,sizeof(*t),r,sizeof(*r))||!apart(t,sizeof(*t),d,sizeof(*d))||
  !apart(t,sizeof(*t),s,sizeof(*s))||!apart(s,sizeof(*s),r->storage,sizeof(*r->storage))||
  !apart(s,sizeof(*s),r->work,sizeof(*r->work))||!apart(s,sizeof(*s),p,sizeof(*p))||
  !apart(s,sizeof(*s),d,sizeof(*d))||p->pcm!=r->storage->pcm)return M16_LW_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->producer=p;t->ring=r;t->direction=d;t->storage=s;t->phase=M16_LW_STOPPED;return 0;
}
int m16_latest_window_start(m16_latest_window_task *t,uint64_t epoch,uint64_t tick){
 int rc;
 if(!valid(t)||t->active||t->error||!epoch||epoch<=t->epoch||t->producer->epoch!=epoch||!t->producer->active)return M16_LW_SEQUENCE;
 rc=m16_front_ring_start(t->ring,epoch,tick);if(rc)return fail(t,rc);
 rc=m16_fast_doa_start(t->direction,epoch,tick);if(rc)return fail(t,rc);
 t->ring->phase=M16_FR_LATEST;t->active=1;t->phase=M16_LW_IDLE;t->epoch=epoch;t->last_tick=tick;
 t->analyzed_end=t->skipped_frames=t->analyzed_frames=t->selections=t->snapshot_retries=t->copy_steps=0;
 t->results=t->records_evicted=t->cancelled_jobs=0;t->front_busy=t->front_ready=t->coarse_busy=0;
 memset(t->records,0,sizeof(t->records));memset(&t->current,0,sizeof(t->current));
 return 0;
}
static int admit(m16_latest_window_task *t,uint64_t tick){
 m16_fast_doa_task *d=t->direction;m16_spatial_evidence_cache *e=&t->ring->evidence;
 if(t->producer->next_frame<t->analyzed_end||t->producer->epoch!=t->epoch)return fail(t,M16_LW_SEQUENCE);
 if(t->producer->next_frame-t->analyzed_end<4u)return 0;
 if(!m16_fast_doa_can_accept(d)||d->history_count||e->phase!=M16_EC_IDLE||e->owned)return fail(t,M16_LW_SEQUENCE);
 t->selected_first=t->producer->next_frame-4u;t->copy_frame=t->copy_cursor=0;t->selections++;
 t->current=(m16_latest_window_record){t->epoch,d->sequence,t->selected_first,t->selected_first+4u,
  t->analyzed_end,t->selected_first,tick,0,0,0,0};
 t->phase=M16_LW_COPY;return 1;
}
static int snapshot(m16_latest_window_task *t,uint64_t tick){
 const float *p;const m16_pcm_frame_metadata *m;unsigned end;int rc;
 uint64_t frame=t->selected_first+t->copy_frame;
 rc=m16_pcm_acquisition_frame(t->producer,t->epoch,frame,&m,&p);
 if(rc==M16_PA_EXPIRED){t->snapshot_retries++;t->phase=M16_LW_IDLE;return 1;}
 if(rc)return fail(t,rc);
 if(!t->copy_cursor){
  if(m->frame!=frame||m->first_sample!=frame*2048u||m->received_tick>tick||
   (t->copy_frame&&m->received_tick<t->metadata[t->copy_frame-1u].received_tick))return fail(t,M16_LW_SEQUENCE);
  t->metadata[t->copy_frame]=*m;
 }else if(memcmp(m,&t->metadata[t->copy_frame],sizeof(*m)))return fail(t,M16_LW_SEQUENCE);
 end=t->copy_cursor+256u;if(end>M16_PA_VALUES)end=M16_PA_VALUES;
 memcpy(t->storage->pcm[t->copy_frame]+t->copy_cursor,p+t->copy_cursor,(end-t->copy_cursor)*4u);
 t->copy_cursor=end;t->copy_steps++;
 if(end==M16_PA_VALUES){t->copy_cursor=0;t->copy_frame++;}
 if(t->copy_frame==4u){
  /* 只在历史清空且结果已归还时移动物理帧起点；四帧内部仍严格相邻。 */
  if(t->selected_first<t->direction->next_frame||t->selected_first<t->ring->evidence.next_frame)return fail(t,M16_LW_SEQUENCE);
  /* 新epoch首组也可能因快照覆盖而从非零帧开始；只移动空闲驱动的软件提交轴。
   * 粗扫核没有epoch/帧号寄存器。实际采样轴仍完整保存在独立快照和跳过记录中。 */
  if(t->selected_first&&t->ring->evidence.coarse->last_epoch<t->epoch){
   if(!m16_coarse_scan_available(t->ring->evidence.coarse))return fail(t,M16_LW_SEQUENCE);
   t->ring->evidence.coarse->last_epoch=t->epoch;
   t->ring->evidence.coarse->last_frame=t->selected_first-1u;
  }
  t->direction->next_frame=t->selected_first;t->ring->evidence.next_frame=t->selected_first;
  t->next_front=t->next_doa=0;t->front_busy=t->front_ready=t->coarse_busy=0;
  t->current.snapshot_tick=tick;t->phase=M16_LW_COMPUTE;
 }return 1;
}
static int compute(m16_latest_window_task *t,uint64_t tick){
 m16_front_ring *r=t->ring;const float *map;unsigned i,slot;int rc;
 rc=m16_fast_doa_step(t->direction,tick);if(rc<0)return fail(t,rc);
 if(t->coarse_busy){
  rc=m16_spatial_evidence_step(&r->evidence);if(rc<0)return fail(t,rc);
  if(!rc){
   uint64_t frame=r->evidence.next_frame-1u;slot=(unsigned)(frame%20u);
   r->metadata[slot].front_done_tick=tick;r->metadata[slot].ready=1;r->frames_out++;r->next_frame=frame+1u;
   t->coarse_busy=0;
  }
 }
 if(t->next_doa<4u&&m16_fast_doa_can_accept(t->direction)){
  uint64_t frame=t->selected_first+t->next_doa;slot=(unsigned)(frame%20u);
  if(r->metadata[slot].ready&&r->metadata[slot].epoch==t->epoch&&r->metadata[slot].frame_seq==frame){
   rc=m16_spatial_evidence_coarse(&r->evidence,t->epoch,frame,&map);if(rc)return fail(t,rc);
   rc=m16_fast_doa_begin(t->direction,map,r->storage->spectrum[slot].zone_level_db,t->epoch,frame,
    frame*2048u,t->metadata[t->next_doa].received_tick,r->metadata[slot].front_done_tick);
   if(rc)return fail(t,rc);t->next_doa++;
  }
 }
 if(t->front_busy){
  rc=m16_public_spectrum_step(&r->front);if(rc<0)return fail(t,rc);
  if(!rc){t->front_busy=0;t->front_ready=1;}
 }
 if(t->front_ready&&!t->coarse_busy){
  uint64_t frame=t->selected_first+t->next_front;slot=(unsigned)(frame%20u);
  rc=m16_spatial_evidence_reserve(&r->evidence,t->epoch,frame);if(rc)return fail(t,rc);
  rc=m16_spatial_evidence_begin(&r->evidence,r->storage->spectrum[slot].phat,807u*32u*2u);if(rc)return fail(t,rc);
  t->coarse_busy=1;t->front_ready=0;t->next_front++;
 }
 if(!t->front_busy&&!t->front_ready&&t->next_front<4u){
  uint64_t frame=t->selected_first+t->next_front;i=t->next_front;slot=(unsigned)(frame%20u);
  r->metadata[slot]=(m16_front_frame_metadata){t->epoch,frame,frame*2048u,t->metadata[i].received_tick,0,0};
  rc=m16_public_spectrum_retarget(&r->front,&r->storage->spectrum[slot]);if(rc)return fail(t,rc);
  rc=m16_public_spectrum_begin(&r->front,t->storage->pcm[i],M16_PA_VALUES,t->epoch,frame);if(rc)return fail(t,rc);
  t->front_busy=1;r->frames_in++;
 }
 if(t->direction->output_ready){
  if(t->next_doa!=4u||t->coarse_busy||t->front_busy||t->front_ready||t->next_front!=4u)return fail(t,M16_LW_SEQUENCE);
  t->current.completed_tick=tick;t->current.completed=1;record(t);
  t->skipped_frames+=t->selected_first-t->analyzed_end;t->analyzed_frames+=4u;
  t->analyzed_end=t->selected_first+4u;t->results++;t->phase=M16_LW_OUTPUT;
 }
 return 1;
}
int m16_latest_window_step(m16_latest_window_task *t,uint64_t tick){
 if(!valid(t))return M16_LW_INVALID;
 if(tick<t->last_tick)return fail(t,M16_LW_CLOCK);t->last_tick=tick;
 if(t->error)return t->error;
 if(t->phase==M16_LW_STOPPING)return m16_latest_window_stop(t);
 if(!t->active)return 2;
 if(t->phase==M16_LW_IDLE)return admit(t,tick);
 if(t->phase==M16_LW_COPY)return snapshot(t,tick);
 if(t->phase==M16_LW_COMPUTE)return compute(t,tick);
 if(t->phase==M16_LW_OUTPUT)return 1;
 return fail(t,M16_LW_INVALID);
}
int m16_latest_window_ack(m16_latest_window_task *t,uint64_t epoch,uint64_t sequence){
 int rc;
 if(!valid(t)||t->phase!=M16_LW_OUTPUT)return M16_LW_SEQUENCE;
 rc=m16_fast_doa_ack(t->direction,epoch,sequence);if(rc)return rc;
 t->phase=M16_LW_IDLE;return 0;
}
int m16_latest_window_stop(m16_latest_window_task *t){
 int rc;
 if(!valid(t))return M16_LW_INVALID;
 if(t->phase!=M16_LW_STOPPING&&t->phase!=M16_LW_STOPPED){
  if(t->phase==M16_LW_COPY||t->phase==M16_LW_COMPUTE){
   t->current.cancelled=1;t->current.completed_tick=t->last_tick;record(t);t->cancelled_jobs++;
  }
  (void)m16_fast_doa_stop(t->direction);t->phase=M16_LW_STOPPING;
 }
 rc=m16_front_ring_stop(t->ring);
 if(rc<0){(void)fail(t,rc);return rc;}
 if(m16_front_ring_releasable(t->ring)){
  t->active=0;t->front_busy=t->front_ready=t->coarse_busy=0;t->phase=M16_LW_STOPPED;return 0;
 }return 1;
}
int m16_latest_window_releasable(const m16_latest_window_task *t){
 return valid(t)&&!t->active&&t->phase==M16_LW_STOPPED&&!t->direction->active&&m16_front_ring_releasable(t->ring);
}
