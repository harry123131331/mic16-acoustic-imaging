#include "mic16_pcm_acquisition.h"
#include <string.h>
#define MAGIC 0x50414331u
static int valid(const m16_pcm_acquisition *t){return t&&t->magic==MAGIC&&t->capture&&t->pcm;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
int m16_pcm_acquisition_init(m16_pcm_acquisition *t,m16_window_capture *c,float (*pcm)[M16_PA_VALUES]){
 if(!t||!c||!pcm||t->magic||(uintptr_t)t%_Alignof(m16_pcm_acquisition)||
  (uintptr_t)pcm%_Alignof(float)||c->config.packet_samples!=2048u||
  (c->phase!=M16_WC_PREPARING&&c->phase!=M16_WC_READY)||
  !apart(t,sizeof(*t),c,sizeof(*c))||!apart(t,sizeof(*t),pcm,M16_PA_FRAMES*M16_PA_VALUES*4u)||
  !apart(c,sizeof(*c),pcm,M16_PA_FRAMES*M16_PA_VALUES*4u))return M16_PA_INVALID;
 for(unsigned i=0;i<3u;i++)if(!apart(pcm,M16_PA_FRAMES*M16_PA_VALUES*4u,c->config.storage[i],2048u*64u+128u)||
  !apart(t,sizeof(*t),c->config.storage[i],2048u*64u+128u))return M16_PA_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->capture=c;t->pcm=pcm;return 0;
}
int m16_pcm_acquisition_start(m16_pcm_acquisition *t,uint64_t epoch){
 int rc;uint64_t completed,warmup;
 if(!valid(t)||t->active||t->held||t->error||!epoch||epoch>UINT32_MAX||epoch<=t->epoch||
  t->capture->phase!=M16_WC_READY)return M16_PA_SEQUENCE;
 completed=t->capture->completed;warmup=t->capture->warmup_completed;
 rc=m16_window_capture_start(t->capture,(unsigned)epoch);if(rc)return rc;
 t->capture_completed_begin=completed;t->warmup_completed_begin=warmup;
 t->active=1;t->epoch=epoch;t->next_frame=t->frames_taken=t->leases_released=t->copy_steps=0;
 t->aborted_partial_frames=0;t->copy_cursor=t->finish_requested=t->closing=0;
 memset(t->metadata,0,sizeof(t->metadata));return 0;
}
int m16_pcm_acquisition_abort(m16_pcm_acquisition *t,int reason){
 int rc;
 if(!valid(t))return M16_PA_INVALID;
 if(reason&&!t->error)t->error=reason<0?reason:M16_PA_INVALID;
 (void)m16_window_capture_stop(t->capture);t->closing=1;
 if(t->held){
  t->metadata[t->next_frame%M16_PA_FRAMES].ready=0;t->aborted_partial_frames++;
  rc=m16_window_capture_release(t->capture,&t->lease);
  if(rc){if(!t->error)t->error=rc;return rc;}
  t->held=0;t->leases_released++;
 }
 return t->error?t->error:0;
}
int m16_pcm_acquisition_step(m16_pcm_acquisition *t){
 int rc;unsigned slot,end;
 if(!valid(t))return M16_PA_INVALID;
 rc=m16_window_capture_step(t->capture);
 if(t->capture->failure&&!t->error)t->error=t->capture->failure;
 if(rc<0&&!t->error)t->error=rc;
 if(t->error){
  (void)m16_pcm_acquisition_abort(t,t->error);
  /* 故障码持续保留，但确认DMA已复位、租约已归还后仍可结束隔离。 */
  if(m16_window_capture_releasable(t->capture)&&!t->held)t->active=0;
  return t->error;
 }
 if(t->closing){
  if(m16_window_capture_releasable(t->capture)&&!t->held){t->active=0;return 2;}
  return 1;
 }
 if(!t->active)return 0;
 if(!t->held){
  rc=m16_window_capture_take(t->capture,&t->lease);
  if(rc==M16_WC_EMPTY)return 1;
  if(rc){(void)m16_pcm_acquisition_abort(t,rc);return t->error;}
  t->held=1;t->frames_taken++;t->copy_cursor=0;
  if(t->next_frame>UINT64_MAX/2048u-1u||t->lease.epoch!=t->epoch||t->lease.first_sample!=t->next_frame*2048u){
   (void)m16_pcm_acquisition_abort(t,M16_PA_SEQUENCE);return t->error;
  }
  slot=(unsigned)(t->next_frame%M16_PA_FRAMES);
  /* 旧帧在写入首片段前立即失效，不能让消费者读取新旧混合的PCM。 */
  t->metadata[slot]=(m16_pcm_frame_metadata){t->epoch,t->next_frame,t->lease.first_sample,t->lease.received_tick,0};
 }
 slot=(unsigned)(t->next_frame%M16_PA_FRAMES);
 end=t->copy_cursor+256u;if(end>M16_PA_VALUES)end=M16_PA_VALUES;
 memcpy(t->pcm[slot]+t->copy_cursor,t->lease.pcm+t->copy_cursor,(end-t->copy_cursor)*4u);
 t->copy_steps++;t->copy_cursor=end;
 if(end==M16_PA_VALUES){
  rc=m16_window_capture_release(t->capture,&t->lease);
  if(rc){(void)m16_pcm_acquisition_abort(t,rc);return t->error;}
  t->held=0;t->leases_released++;t->metadata[slot].ready=1;t->next_frame++;
 }
 return 1;
}
int m16_pcm_acquisition_frame(const m16_pcm_acquisition *t,uint64_t epoch,uint64_t frame,
 const m16_pcm_frame_metadata **metadata,const float **pcm){
 unsigned slot;
 if(!valid(t)||!metadata||!pcm||epoch!=t->epoch)return M16_PA_SEQUENCE;
 if(frame>=t->next_frame)return M16_WC_EMPTY;
 slot=(unsigned)(frame%M16_PA_FRAMES);
 if(!t->metadata[slot].ready||t->metadata[slot].epoch!=epoch||t->metadata[slot].frame!=frame)return M16_PA_EXPIRED;
 *metadata=&t->metadata[slot];*pcm=t->pcm[slot];return 0;
}
int m16_pcm_acquisition_request_finish(m16_pcm_acquisition *t){
 int rc;
 if(!valid(t)||!t->active||t->error||t->closing)return M16_PA_SEQUENCE;
 if(t->finish_requested)return 0;
 rc=m16_window_capture_request_finish(t->capture);if(!rc)t->finish_requested=1;return rc;
}
int m16_pcm_acquisition_input_complete(const m16_pcm_acquisition *t){
 return valid(t)&&t->active&&t->finish_requested&&!t->held&&!t->error&&m16_window_capture_input_complete(t->capture);
}
int m16_pcm_acquisition_finish_stop(m16_pcm_acquisition *t){
 if(!m16_pcm_acquisition_input_complete(t))return M16_PA_SEQUENCE;
 t->closing=1;return m16_window_capture_stop(t->capture);
}
int m16_pcm_acquisition_releasable(const m16_pcm_acquisition *t){
 return valid(t)&&!t->active&&!t->held&&m16_window_capture_releasable(t->capture);
}
int m16_pcm_acquisition_restart(m16_pcm_acquisition *t){
 int rc;
 if(!m16_pcm_acquisition_releasable(t))return M16_PA_SEQUENCE;
 rc=m16_window_capture_restart(t->capture);
 if(!rc){t->error=0;t->finish_requested=t->closing=0;t->copy_cursor=0;}
 return rc;
}
