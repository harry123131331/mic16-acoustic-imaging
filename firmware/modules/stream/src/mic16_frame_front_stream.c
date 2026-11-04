#include "mic16_frame_front_stream.h"
#define MAGIC 0x4d464253u
static int valid(const m16_frame_front_stream *s){return s&&s->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
int m16_frame_front_stream_init(m16_frame_front_stream *s,m16_window_capture *c,m16_front_ring *r){
 if(!s||!c||!r||s->magic||(uintptr_t)s%_Alignof(m16_frame_front_stream)||
  !apart(s,sizeof(*s),c,sizeof(*c))||!apart(s,sizeof(*s),r,sizeof(*r))||!apart(c,sizeof(*c),r,sizeof(*r))||
  !m16_front_ring_releasable(r)||c->config.packet_samples!=2048u||
  (c->phase!=M16_WC_PREPARING&&c->phase!=M16_WC_READY))return M16_FR_INVALID;
 for(unsigned i=0;i<3;i++)if(!apart(c->config.storage[i],M16_WC_FRAME_STORAGE,s,sizeof(*s))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r,sizeof(*r))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r->storage,sizeof(*r->storage))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r->work,sizeof(*r->work)))return M16_FR_INVALID;
 *s=(m16_frame_front_stream){0};s->magic=MAGIC;s->capture=c;s->ring=r;s->frame_limit=UINT64_MAX;return 0;
}
int m16_frame_front_stream_set_capture_once(m16_frame_front_stream *s,unsigned enabled){
 if(!valid(s)||enabled>1u)return M16_FR_INVALID;
 if(s->ring->epoch||s->held||s->stopping||!m16_front_ring_releasable(s->ring)||
  (s->capture->phase!=M16_WC_PREPARING&&s->capture->phase!=M16_WC_READY))return M16_FR_BUSY;
 s->capture_once=enabled;return 0;
}
int m16_frame_front_stream_limit(m16_frame_front_stream *s,uint64_t end){
 if(!valid(s)||!end)return M16_FR_INVALID;
 if(s->ring->phase!=M16_FR_STOPPED&&(end<s->ring->next_frame||end<s->frame_limit))return M16_FR_SEQUENCE;
 s->frame_limit=end;return 0;
}
int m16_frame_front_stream_start(m16_frame_front_stream *s,unsigned epoch){
 int rc;if(!valid(s)||s->stopping||s->held)return M16_FR_INVALID;
 if(s->capture->phase!=M16_WC_READY)return M16_FR_BUSY;
 rc=m16_front_ring_start(s->ring,epoch,s->capture->io.ticks(s->capture->io.context));if(rc)return rc;
 s->prefilled=s->prefill_cursor=0;
 rc=m16_window_capture_start(s->capture,epoch);
 if(!rc&&s->capture_once)rc=m16_window_capture_request_finish_at(s->capture,40960u);
 if(rc)(void)m16_front_ring_stop(s->ring);
 return rc;
}
int m16_frame_front_stream_releasable(const m16_frame_front_stream *s){
 return valid(s)&&!s->held&&m16_front_ring_releasable(s->ring)&&m16_window_capture_releasable(s->capture);
}
static int release_input(m16_frame_front_stream *s){
 int rc;
 if(!s->held||!m16_front_ring_input_releasable(s->ring))return 0;
 rc=m16_window_capture_release(s->capture,&s->lease);
 if(rc){if(!s->error)s->error=rc;return rc;}
 s->held=0;s->leases_released++;return 0;
}
int m16_frame_front_stream_stop(m16_frame_front_stream *s){
 int rc;if(!valid(s))return M16_FR_INVALID;
 s->stopping=1;(void)m16_window_capture_stop(s->capture);rc=m16_front_ring_stop(s->ring);
 if(rc<0&&!s->error)s->error=rc;
 (void)release_input(s);return m16_frame_front_stream_releasable(s)?1:0;
}
static int prefill_one_step(m16_frame_front_stream *s){
 int rc;unsigned end;
 if(!s->held){
  rc=m16_window_capture_take(s->capture,&s->lease);if(rc==M16_WC_EMPTY)return 1;
  if(rc)return rc;
  s->held=1;s->frames_taken++;
  if(s->lease.epoch!=s->ring->epoch||s->lease.first_sample!=(uint64_t)s->prefilled*2048u)return M16_FR_SEQUENCE;
  s->prefill_cursor=0;s->prefill_received_tick[s->prefilled]=s->lease.received_tick;
 }
 end=s->prefill_cursor+64u;if(end>M16_FR_PCM_VALUES)end=M16_FR_PCM_VALUES;
 for(unsigned i=s->prefill_cursor;i<end;i++){
  float v=s->lease.pcm[i];if(!(v>=-1.f&&v<=1.f))return M16_FR_INPUT;
  s->ring->storage->pcm[s->prefilled][i]=v;
 }
 s->prefill_cursor=end;
 if(end==M16_FR_PCM_VALUES){
  rc=m16_window_capture_release(s->capture,&s->lease);if(rc)return rc;
  s->held=0;s->leases_released++;s->prefilled++;s->prefill_cursor=0;
 }
 return 1;
}
int m16_frame_front_stream_step(m16_frame_front_stream *s){
 int rc;m16_window_capture_lease lease;
 if(!valid(s))return M16_FR_INVALID;
 rc=m16_window_capture_step(s->capture);
 if(rc<0&&!s->error)s->error=rc;
 if(s->error||s->stopping){
  (void)m16_frame_front_stream_stop(s);
  return m16_frame_front_stream_releasable(s)?(s->error?s->error:2):1;
 }
 if(s->capture->phase==M16_WC_READY)return 0;
 if(s->capture_once&&s->prefilled<M16_FR_FRAMES&&(s->capture->phase==M16_WC_RUNNING||s->capture->phase==M16_WC_FINISHING)){
  rc=prefill_one_step(s);
  if(rc<0){s->error=rc;(void)m16_frame_front_stream_stop(s);}
  return 1;
 }
 if(s->ring->phase==M16_FR_COPY||s->ring->phase==M16_FR_FRONT||s->ring->phase==M16_FR_SPATIAL){
  if(s->capture->phase==M16_WC_RUNNING)s->front_while_dma++;
  rc=m16_front_ring_step(s->ring,s->capture->io.ticks(s->capture->io.context));
  if(rc<0){s->error=rc;(void)m16_frame_front_stream_stop(s);return 1;}
 }
 if(release_input(s)){(void)m16_frame_front_stream_stop(s);return 1;}
 if(s->held||s->ring->phase!=M16_FR_IDLE||s->ring->reader)return 1;
 if(s->ring->next_frame>=s->frame_limit){s->credit_stalls++;return 1;}
 if(s->capture_once){
  if(s->capture->phase!=M16_WC_FINISHING){s->error=M16_FR_SEQUENCE;(void)m16_frame_front_stream_stop(s);return 1;}
  if(s->ring->next_frame>=s->prefilled)return 1;
  rc=m16_front_ring_begin_buffered(s->ring,s->ring->epoch,s->ring->next_frame,s->prefill_received_tick[s->ring->next_frame]);
  if(rc){s->error=rc;(void)m16_frame_front_stream_stop(s);}
  return 1;
 }
 rc=m16_window_capture_take(s->capture,&lease);if(rc==M16_WC_EMPTY)return 1;
 if(rc){s->error=rc;(void)m16_frame_front_stream_stop(s);return 1;}
 s->lease=lease;s->held=1;
 rc=m16_front_ring_begin(s->ring,lease.pcm,32768u,lease.epoch,lease.first_sample/2048u,lease.first_sample,lease.received_tick);
 if(rc){s->error=rc;(void)m16_frame_front_stream_stop(s);return 1;}
 s->frames_taken++;return 1;
}
int m16_frame_front_stream_restart(m16_frame_front_stream *s,unsigned epoch){
 int rc;if(!valid(s)||!m16_frame_front_stream_releasable(s)||epoch<=s->ring->epoch)return M16_FR_BUSY;
 rc=m16_window_capture_restart(s->capture);if(rc)return rc;
 s->prefilled=s->prefill_cursor=0;
 s->stopping=0;s->error=0;s->frames_taken=s->leases_released=s->front_while_dma=0;
 /* 仍须通过step完成DMA缓冲清理，再显式start，不能默默采集。 */
 return 0;
}
