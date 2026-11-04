#include "mic16_pipeline_front_stream.h"
#define MAGIC 0x50534631u
static int valid(const m16_pipeline_front_stream *s){return s&&s->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
int m16_pipeline_front_stream_init(m16_pipeline_front_stream *s,m16_window_capture *c,m16_front_pipeline *p){
 m16_front_ring *r;
 if(!s||!c||!p||s->magic||(uintptr_t)s%_Alignof(m16_pipeline_front_stream)||!m16_front_pipeline_releasable(p)||
  c->config.packet_samples!=2048u||(c->phase!=M16_WC_PREPARING&&c->phase!=M16_WC_READY))return M16_FR_INVALID;
 r=p->ring;
 if(!apart(s,sizeof(*s),c,sizeof(*c))||!apart(s,sizeof(*s),p,sizeof(*p))||!apart(s,sizeof(*s),r,sizeof(*r))||
  !apart(s,sizeof(*s),r->storage,sizeof(*r->storage))||!apart(s,sizeof(*s),r->work,sizeof(*r->work))||
  !apart(c,sizeof(*c),p,sizeof(*p))||!apart(c,sizeof(*c),r,sizeof(*r))||
  !apart(c,sizeof(*c),r->storage,sizeof(*r->storage))||!apart(c,sizeof(*c),r->work,sizeof(*r->work)))return M16_FR_INVALID;
 for(unsigned i=0;i<3;i++)if(!apart(c->config.storage[i],M16_WC_FRAME_STORAGE,s,sizeof(*s))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,p,sizeof(*p))||!apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r,sizeof(*r))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r->storage,sizeof(*r->storage))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r->work,sizeof(*r->work))||
  !apart(c->config.storage[i],M16_WC_FRAME_STORAGE,r->evidence.coarse,sizeof(*r->evidence.coarse))||
  !apart((const void *)(uintptr_t)c->config.physical[i],M16_WC_FRAME_STORAGE,
   (const void *)(uintptr_t)(r->evidence.coarse->io.arena_address-64u),sizeof(r->evidence.coarse->arena)))return M16_FR_INVALID;
 *s=(m16_pipeline_front_stream){0};s->magic=MAGIC;s->capture=c;s->front=p;s->credit_end=UINT64_MAX;return 0;
}
int m16_pipeline_front_stream_releasable(const m16_pipeline_front_stream *s){
 return valid(s)&&!s->held&&m16_front_pipeline_releasable(s->front)&&m16_window_capture_releasable(s->capture);
}
int m16_pipeline_front_stream_limit(m16_pipeline_front_stream *s,uint64_t end){
 if(!valid(s)||!end)return M16_FR_INVALID;
 if(s->front->active&&(end<s->front->next_accept||end<s->credit_end))return M16_FR_SEQUENCE;
 s->credit_end=end;return 0;
}
int m16_pipeline_front_stream_start(m16_pipeline_front_stream *s,unsigned epoch){
 int rc;
 if(!valid(s)||s->held||s->stopping||s->error)return M16_FR_INVALID;
 if(s->capture->phase!=M16_WC_READY)return M16_FR_BUSY;
 rc=m16_front_pipeline_start(s->front,epoch,s->capture->io.ticks(s->capture->io.context));if(rc)return rc;
 rc=m16_window_capture_start(s->capture,epoch);
 if(rc){s->error=rc;s->stopping=1;(void)m16_front_pipeline_stop(s->front);}
 return rc;
}
static int release_input(m16_pipeline_front_stream *s){
 int rc;
 if(!s->held||!m16_front_ring_input_releasable(s->front->ring))return 0;
 rc=m16_window_capture_release(s->capture,&s->lease);if(rc)return rc;
 s->held=0;s->leases_released++;return 0;
}
int m16_pipeline_front_stream_stop(m16_pipeline_front_stream *s){
 int rc;
 if(!valid(s))return M16_FR_INVALID;
 s->stopping=1;(void)m16_window_capture_stop(s->capture);
 rc=m16_front_pipeline_stop(s->front);if(rc<0&&!s->error)s->error=rc;
 rc=release_input(s);if(rc&&!s->error)s->error=rc;
 return m16_pipeline_front_stream_releasable(s)?1:0;
}
int m16_pipeline_front_stream_step(m16_pipeline_front_stream *s){
 int rc;m16_window_capture_lease lease;
 if(!valid(s))return M16_FR_INVALID;
 /* 始终服务原DMA；计算暂停和B快照不会静默关掉采样。 */
 rc=m16_window_capture_step(s->capture);if(rc<0&&!s->error)s->error=rc;
 /* IRQ可已记录短包/溢出，DMA仍在复位排空；立即报告原因，继续保留租约。 */
 if(s->capture->failure&&!s->error)s->error=s->capture->failure;
 if(s->error||s->stopping){
  (void)m16_pipeline_front_stream_stop(s);
  return m16_pipeline_front_stream_releasable(s)?(s->error?s->error:2):1;
 }
 if(!s->front->active)return 0;
 rc=m16_front_pipeline_step(s->front,s->capture->io.ticks(s->capture->io.context));
 if(rc<0){s->error=rc;(void)m16_pipeline_front_stream_stop(s);return 1;}
 rc=release_input(s);if(rc){s->error=rc;(void)m16_pipeline_front_stream_stop(s);return 1;}
 if(s->held||!m16_front_pipeline_can_accept(s->front))return 1;
 if(s->front->next_accept>=s->credit_end){s->credit_stalls++;return 1;}
 if(s->capture->phase!=M16_WC_RUNNING&&s->capture->phase!=M16_WC_FINISHING)return 1;
 rc=m16_window_capture_take(s->capture,&lease);if(rc==M16_WC_EMPTY)return 1;
 if(rc){s->error=rc;(void)m16_pipeline_front_stream_stop(s);return 1;}
 s->lease=lease;s->held=1;
 rc=m16_front_pipeline_begin(s->front,lease.pcm,32768u,lease.epoch,lease.first_sample/2048u,lease.first_sample,lease.received_tick);
 if(rc){s->error=rc;(void)m16_pipeline_front_stream_stop(s);return 1;}
 s->frames_taken++;return 1;
}
int m16_pipeline_front_stream_restart(m16_pipeline_front_stream *s){
 int rc;
 if(!valid(s)||!m16_pipeline_front_stream_releasable(s))return M16_FR_BUSY;
 rc=m16_window_capture_restart(s->capture);if(rc)return rc;
 s->held=s->stopping=0;s->error=0;s->frames_taken=s->leases_released=s->credit_stalls=0;return 0;
}
