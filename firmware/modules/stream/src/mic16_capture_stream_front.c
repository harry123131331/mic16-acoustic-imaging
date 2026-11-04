#include "mic16_capture_stream_front.h"
#include <string.h>
#define MAGIC 0x4d435353u
static int valid(const m16_capture_stream *s){return s&&s->magic==MAGIC&&s->front&&s->front_context;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static uint64_t now(m16_capture_stream *s){return s->capture->io.ticks(s->capture->io.context);}
static int equal(const mic16_stream_ticket *a,const mic16_stream_ticket *b){
 return a->pool==b->pool&&a->slot==b->slot&&a->owner==b->owner&&a->epoch==b->epoch&&a->generation==b->generation;
}
int m16_capture_stream_init_front(m16_capture_stream *s,m16_frame_front_stream *front,m16_front_context_task *context,mic16_stream_scheduler *scheduler){
 const void *p[9];uintptr_t n[9];m16_window_capture *c;
 if(!s||!front||!context||!scheduler||s->magic||(uintptr_t)s%_Alignof(m16_capture_stream)||
  !front->capture||!front->ring||context->ring!=front->ring||context->phase!=M16_FC_IDLE||
  front->held||front->stopping||!m16_front_ring_releasable(front->ring)||!scheduler->epoch||scheduler->failure||scheduler->config.input_slots!=3)return M16_STREAM_INVALID;
 c=front->capture;if(c->config.packet_samples!=2048u||(c->phase!=M16_WC_PREPARING&&c->phase!=M16_WC_READY))return M16_STREAM_INVALID;
 p[0]=s;n[0]=sizeof(*s);p[1]=front;n[1]=sizeof(*front);p[2]=context;n[2]=sizeof(*context);p[3]=scheduler;n[3]=sizeof(*scheduler);
 p[4]=c;n[4]=sizeof(*c);p[5]=front->ring;n[5]=sizeof(*front->ring);p[6]=front->ring->storage;n[6]=sizeof(*front->ring->storage);
 p[7]=front->ring->work;n[7]=sizeof(*front->ring->work);p[8]=context->pcm;n[8]=40960u*64u;
 for(unsigned i=0;i<9;i++)for(unsigned j=i+1;j<9;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_STREAM_INVALID;
 for(unsigned i=0;i<3;i++){
  if(scheduler->input[i].state!=M16_STREAM_FREE)return M16_STREAM_INVALID;
  for(unsigned j=0;j<9;j++)if(!apart(c->config.storage[i],M16_WC_FRAME_STORAGE,p[j],n[j]))return M16_STREAM_INVALID;
 }
 if(mic16_stream_poll(scheduler,c->io.ticks(c->io.context)))return M16_STREAM_INVALID;
 *s=(m16_capture_stream){0};s->magic=MAGIC;s->capture=c;s->scheduler=scheduler;s->front=front;s->front_context=context;return 0;
}
int m16_front_capture_start(m16_capture_stream *s){
 int rc;if(!valid(s)||s->stopping||s->scheduler->failure||s->scheduler->next_input_first)return M16_STREAM_INVALID;
 rc=m16_frame_front_stream_limit(s->front,20);if(rc)return rc;
 rc=m16_frame_front_stream_start(s->front,s->scheduler->epoch);
 if(!rc&&s->front->capture_once)s->finish_sample=40960u;
 return rc;
}
int m16_front_capture_stop(m16_capture_stream *s,int reason){
 int rc;if(!valid(s))return M16_STREAM_INVALID;
 if(reason&&!s->error)s->error=reason;
 if(!s->scheduler->failure)(void)mic16_stream_abort(s->scheduler,reason?reason:M16_STREAM_CANCELLED);
 s->stopping=1;(void)m16_frame_front_stream_stop(s->front);
 /* 已交计算的上下文仍由父图排空；未交付的COPY/READY可安全取消。 */
 if(!s->front_context->submitted&&s->front_context->phase!=M16_FC_IDLE){
  rc=m16_front_context_stop(s->front_context);if(rc<0&&!s->error)s->error=rc;
 }
 for(unsigned i=0;i<3;i++)if(s->bindings[i].state==1u){s->bindings[i].state=0;s->context_state=0;}
 return 0;
}
static int fail(m16_capture_stream *s,int rc){(void)m16_front_capture_stop(s,rc);return 1;}
int m16_front_capture_step(m16_capture_stream *s){
 int rc;uint64_t frame_end;m16_front_context_task *c;mic16_stream_ticket ticket;
 if(!valid(s))return M16_STREAM_INVALID;
 rc=m16_frame_front_stream_step(s->front);
 if(s->stopping)return m16_front_capture_releasable(s)?(s->error?s->error:2):1;
 if(rc<0)return fail(s,rc==M16_WC_RANGE?M16_STREAM_INPUT_INVALID:M16_STREAM_GAP);
 if(s->scheduler->failure)return fail(s,s->scheduler->failure);
 if(mic16_stream_poll(s->scheduler,now(s)))return fail(s,s->scheduler->failure);
 if(s->capture->phase==M16_WC_READY)return 0;
 if(s->capture->phase!=M16_WC_RUNNING&&s->capture->phase!=M16_WC_FINISHING)return 1;
 c=s->front_context;
 if(c->phase==M16_FC_COPY){rc=m16_front_context_step(c);if(rc<0)return fail(s,rc);return 1;}
 if(!s->context_state&&c->phase==M16_FC_READY){
  if(c->first_sample!=s->next_context_first)return fail(s,M16_STREAM_GAP);
  rc=mic16_stream_input_reserve(s->scheduler,c->first_sample,now(s),&ticket);if(rc)return fail(s,rc);
  rc=mic16_stream_input_commit_received(s->scheduler,&ticket,s->context_received_tick,now(s));
  if(rc){(void)mic16_stream_input_release(s->scheduler,&ticket);return fail(s,rc);}
  if(s->bindings[ticket.slot].state)return fail(s,M16_STREAM_GAP);
  s->bindings[ticket.slot].state=1;s->bindings[ticket.slot].ticket=ticket;s->context_state=1;
  if(s->next_context_first>UINT64_MAX-40960u)return fail(s,M16_STREAM_COUNTER);
  s->next_context_first+=40960u;frame_end=s->next_context_first/2048u+20u;
  rc=m16_frame_front_stream_limit(s->front,frame_end);if(rc)return fail(s,rc);return 1;
 }
 frame_end=s->next_context_first/2048u+20u;
 if(!s->context_state&&c->phase==M16_FC_IDLE&&s->front->ring->next_frame==frame_end){
  const m16_front_frame_metadata *m;const float *pcm;const mic16_spectral_frame *spectrum;
  rc=m16_front_context_begin(c);if(rc==M16_FC_BUSY)return 1;if(rc)return fail(s,rc);
  rc=m16_front_ring_frame(c->ring,&c->lease,19u,&m,&pcm,&spectrum);if(rc)return fail(s,rc);
  s->context_received_tick=m->received_tick;return 1;
 }
 return 1;
}
int m16_front_capture_take(m16_capture_stream *s,m16_capture_stream_view *out){
 mic16_stream_ticket ticket;mic16_stream_window window;int rc;
 if(!valid(s)||!out||s->stopping||(uintptr_t)out%_Alignof(m16_capture_stream_view)||
  !apart(out,sizeof(*out),s,sizeof(*s))||!apart(out,sizeof(*out),s->front_context,sizeof(*s->front_context))||
  !apart(out,sizeof(*out),s->front_context->pcm,40960u*64u))return M16_STREAM_INVALID;
 rc=mic16_stream_input_acquire(s->scheduler,now(s),&ticket,&window);if(rc)return rc;
 if(s->context_state!=1u||s->bindings[ticket.slot].state!=1u||s->front_context->phase!=M16_FC_READY||
  window.first_sample!=s->front_context->first_sample){(void)mic16_stream_input_release(s->scheduler,&ticket);return fail(s,M16_STREAM_GAP),M16_STREAM_GAP;}
 s->bindings[ticket.slot].state=2;s->bindings[ticket.slot].ticket=ticket;s->context_state=2;
 *out=(m16_capture_stream_view){ticket,window,s->front_context->pcm,s->front_context};return 0;
}
int m16_front_capture_release(m16_capture_stream *s,const m16_capture_stream_view *v){
 m16_capture_stream_binding *b;int rc;
 if(!valid(s)||!v||v->ticket.slot>=3u||v->prepared!=s->front_context||v->pcm!=s->front_context->pcm)return M16_STREAM_LEASE;
 b=&s->bindings[v->ticket.slot];
 if(!equal(&b->ticket,&v->ticket))return M16_STREAM_LEASE;
 if(b->state==3u){b->state=0;return 0;}
 if(b->state!=2u||s->context_state!=2u)return M16_STREAM_LEASE;
 if(s->front_context->submitted)rc=m16_front_context_retire(s->front_context);
 else rc=m16_front_context_stop(s->front_context)==1?0:M16_STREAM_BUSY;
 if(rc)return rc;
 rc=mic16_stream_input_release(s->scheduler,&v->ticket);
 if(rc==M16_STREAM_LEASE||rc==M16_STREAM_INVALID)return rc;
 s->context_state=0;
 if(rc){b->state=3;(void)m16_front_capture_stop(s,rc);return rc;}
 b->state=0;return 0;
}
int m16_front_capture_finish(m16_capture_stream *s){
 if(!valid(s)||s->stopping||s->scheduler->failure)return M16_STREAM_INVALID;
 if(s->finish_sample)return 0;
 return m16_window_capture_request_finish_aligned(s->capture,40960u,&s->finish_sample);
}
int m16_front_capture_input_complete(const m16_capture_stream *s){
 return valid(s)&&!s->stopping&&!s->scheduler->failure&&s->finish_sample&&
  m16_window_capture_input_complete(s->capture)&&!s->front->held&&s->front->ring->phase==M16_FR_IDLE&&
  s->front->ring->next_frame==s->finish_sample/2048u&&
  /* COPY转READY与逻辑入队分属两步；末上下文未消费时不能提前flush。 */
  s->next_context_first==s->finish_sample&&!s->context_state&&s->front_context->phase==M16_FC_IDLE;
}
int m16_front_capture_releasable(const m16_capture_stream *s){
 if(!valid(s)||s->context_state||s->front_context->phase!=M16_FC_IDLE||!m16_frame_front_stream_releasable(s->front))return 0;
 for(unsigned i=0;i<3;i++)if(s->bindings[i].state||s->scheduler->input[i].state!=M16_STREAM_FREE)return 0;
 return 1;
}
int m16_front_capture_restart(m16_capture_stream *s,unsigned epoch,uint64_t context,uint64_t reference){
 int rc;if(!valid(s)||!m16_front_capture_releasable(s))return M16_STREAM_BUSY;
 rc=mic16_stream_restart(s->scheduler,epoch,context,reference,now(s));if(rc)return rc;
 rc=m16_frame_front_stream_restart(s->front,epoch);if(rc)return rc;
 rc=m16_frame_front_stream_limit(s->front,20u);if(rc)return rc;
 memset(s->bindings,0,sizeof(s->bindings));s->next_context_first=s->context_received_tick=s->finish_sample=0;
 s->error=0;s->stopping=s->context_state=0;return 0;
}
