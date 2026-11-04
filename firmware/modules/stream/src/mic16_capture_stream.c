#include "mic16_capture_stream.h"
#include "mic16_capture_stream_front.h"
#include <string.h>
#define MAGIC 0x4d435353u
static int apart(const void *a,size_t an,const void *b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int valid(const m16_capture_stream *t){return t && t->magic==MAGIC;}
static uint64_t ticks(m16_capture_stream *t){return t->capture->io.ticks(t->capture->io.context);}
static void stop(m16_capture_stream *t,int reason){
 unsigned i;
 if(reason && !t->error)t->error=reason;
 if(!t->scheduler->failure)(void)mic16_stream_abort(t->scheduler,reason?reason:M16_STREAM_CANCELLED);
 (void)m16_window_capture_stop(t->capture);t->stopping=1;
 /* 调度器已撤销自有READY队列，尚未交计算的采集租约可以安全归还。 */
 for(i=0;i<3;i++)if(t->bindings[i].state==1){
  if(m16_window_capture_release(t->capture,&t->bindings[i].lease)==0)t->bindings[i].state=0;
 }
}
int m16_capture_stream_init(m16_capture_stream *t,m16_window_capture *c,mic16_stream_scheduler *s){
 unsigned i;
 if(!t || !c || !s || (uintptr_t)t%_Alignof(m16_capture_stream) || t->magic ||
  !apart(t,sizeof(*t),c,sizeof(*c)) || !apart(t,sizeof(*t),s,sizeof(*s)) || !apart(c,sizeof(*c),s,sizeof(*s)) ||
  c->config.packet_samples!=M16_WC_SAMPLES || (c->phase!=M16_WC_PREPARING && c->phase!=M16_WC_READY) || s->config.input_slots!=3 || s->failure || !s->epoch || !c->io.ticks)return M16_STREAM_INVALID;
 for(i=0;i<3;i++)if(!apart(t,sizeof(*t),c->config.storage[i],M16_WC_STORAGE) ||
  !apart(s,sizeof(*s),c->config.storage[i],M16_WC_STORAGE) || s->input[i].state!=M16_STREAM_FREE)return M16_STREAM_INVALID;
 if(mic16_stream_poll(s,c->io.ticks(c->io.context)))return M16_STREAM_INVALID;
 *t=(m16_capture_stream){0};t->magic=MAGIC;t->capture=c;t->scheduler=s;return 0;
}
int m16_capture_stream_start(m16_capture_stream *t){
 if(!valid(t) || t->stopping || t->scheduler->failure || t->scheduler->next_input_first)return M16_STREAM_INVALID;
 return t->front?m16_front_capture_start(t):m16_window_capture_start(t->capture,t->scheduler->epoch);
}
int m16_capture_stream_step(m16_capture_stream *t){
 m16_window_capture_lease lease;mic16_stream_ticket ticket;int rc,why;unsigned phase;uintptr_t key;
 if(!valid(t))return M16_STREAM_INVALID;
 if(t->front)return m16_front_capture_step(t);
 rc=m16_window_capture_step(t->capture);
 key=t->capture->io.lock(t->capture->io.context);phase=t->capture->phase;why=t->capture->failure;
 t->capture->io.unlock(t->capture->io.context,key);
 if(why && !t->stopping)stop(t,why==M16_WC_RANGE?M16_STREAM_INPUT_INVALID:M16_STREAM_GAP);
 if(t->scheduler->failure && !t->stopping)stop(t,t->scheduler->failure);
 if(t->stopping)return m16_capture_stream_releasable(t)?(t->error?t->error:2):1;
 if(rc<0){stop(t,M16_STREAM_GAP);return 1;}
 rc=mic16_stream_poll(t->scheduler,ticks(t));
 if(rc){stop(t,t->scheduler->failure);return 1;}
 if(phase==M16_WC_READY)return 0;
 if(phase!=M16_WC_RUNNING && phase!=M16_WC_FINISHING)return 1;
 rc=m16_window_capture_take(t->capture,&lease);
 if(rc==M16_WC_EMPTY)return 1;
 if(rc){stop(t,M16_STREAM_GAP);return 1;}
 if(lease.epoch!=t->scheduler->epoch){(void)m16_window_capture_release(t->capture,&lease);stop(t,M16_STREAM_GAP);return 1;}
 rc=mic16_stream_input_reserve(t->scheduler,lease.first_sample,ticks(t),&ticket);
 if(!rc){
  rc=mic16_stream_input_commit_received(t->scheduler,&ticket,lease.received_tick,ticks(t));
  if(rc)(void)mic16_stream_input_release(t->scheduler,&ticket);
 }
 if(rc){(void)m16_window_capture_release(t->capture,&lease);stop(t,t->scheduler->failure?t->scheduler->failure:M16_STREAM_GAP);return 1;}
 if(t->bindings[ticket.slot].state){
  (void)m16_window_capture_release(t->capture,&lease);stop(t,M16_STREAM_GAP);return 1;
 }
 t->bindings[ticket.slot]=(m16_capture_stream_binding){1,lease,ticket};return 1;
}
static int ticket_equal(const mic16_stream_ticket *a,const mic16_stream_ticket *b){
 return a->pool==b->pool && a->slot==b->slot && a->owner==b->owner && a->epoch==b->epoch && a->generation==b->generation;
}
int m16_capture_stream_take(m16_capture_stream *t,m16_capture_stream_view *out){
 mic16_stream_ticket ticket;mic16_stream_window window;m16_capture_stream_binding *b;int rc;unsigned i;
 if(!valid(t) || !out || (uintptr_t)out%_Alignof(m16_capture_stream_view) || t->stopping ||
  !apart(out,sizeof(*out),t,sizeof(*t)) || !apart(out,sizeof(*out),t->capture,sizeof(*t->capture)) ||
  !apart(out,sizeof(*out),t->scheduler,sizeof(*t->scheduler)))return M16_STREAM_INVALID;
 if(t->front)return m16_front_capture_take(t,out);
 for(i=0;i<3;i++)if(!apart(out,sizeof(*out),t->capture->config.storage[i],M16_WC_STORAGE))return M16_STREAM_INVALID;
 rc=mic16_stream_input_acquire(t->scheduler,ticks(t),&ticket,&window);if(rc)return rc;
 b=t->bindings+ticket.slot;
 if(b->state!=1 || b->lease.epoch!=ticket.epoch || b->lease.first_sample!=window.first_sample || b->lease.received_tick!=window.received_tick){
  stop(t,M16_STREAM_GAP);(void)mic16_stream_input_release(t->scheduler,&ticket);return M16_STREAM_GAP;
 }
 b->state=2;b->ticket=ticket;*out=(m16_capture_stream_view){ticket,window,b->lease.pcm,0};return 0;
}
int m16_capture_stream_release(m16_capture_stream *t,const m16_capture_stream_view *v){
 m16_capture_stream_binding *b;int a,rc;
 if(!valid(t) || !v || (uintptr_t)v%_Alignof(m16_capture_stream_view) ||
  !apart(t,sizeof(*t),v,sizeof(*v)) || v->ticket.slot>=3)return M16_STREAM_INVALID;
 if(t->front)return m16_front_capture_release(t,v);
 b=t->bindings+v->ticket.slot;
 if(b->state!=2 || !ticket_equal(&b->ticket,&v->ticket) || b->lease.pcm!=v->pcm ||
  b->lease.first_sample!=v->window.first_sample || b->lease.received_tick!=v->window.received_tick)return M16_STREAM_LEASE;
 a=mic16_stream_input_release(t->scheduler,&v->ticket);
 /* 即便因缺少输出槽触发时段故障，归还已明确停止访问的计算输入仍须向下传递。 */
 if(a==M16_STREAM_LEASE || a==M16_STREAM_INVALID)return a;
 rc=m16_window_capture_release(t->capture,&b->lease);
 if(rc){stop(t,M16_STREAM_GAP);return rc;}
 b->state=0;
 if(a && !t->stopping)stop(t,t->scheduler->failure);
 return a;
}
int m16_capture_stream_request_finish(m16_capture_stream *t){
 if(!valid(t)||t->stopping||t->scheduler->failure)return M16_STREAM_INVALID;
 return t->front?m16_front_capture_finish(t):m16_window_capture_request_finish(t->capture);
}
int m16_capture_stream_input_complete(const m16_capture_stream *t){
 if(valid(t)&&t->front)return m16_front_capture_input_complete(t);
 return valid(t)&&!t->stopping&&!t->scheduler->failure&&m16_window_capture_input_complete(t->capture);
}
int m16_capture_stream_stop(m16_capture_stream *t){if(!valid(t))return M16_STREAM_INVALID;if(t->front)return m16_front_capture_stop(t,0);stop(t,0);return 0;}
int m16_capture_stream_releasable(const m16_capture_stream *t){
 unsigned i;if(valid(t)&&t->front)return m16_front_capture_releasable(t);
 if(!valid(t) || !m16_window_capture_releasable(t->capture))return 0;
 for(i=0;i<3;i++)if(t->bindings[i].state || t->scheduler->input[i].state!=M16_STREAM_FREE)return 0;
 return 1;
}
int m16_capture_stream_restart(m16_capture_stream *t,unsigned epoch,uint64_t context,uint64_t reference){
 int rc;if(!valid(t) || !m16_capture_stream_releasable(t))return M16_STREAM_BUSY;
 if(t->front)return m16_front_capture_restart(t,epoch,context,reference);
 rc=mic16_stream_restart(t->scheduler,epoch,context,reference,ticks(t));if(rc)return rc;
 rc=m16_window_capture_restart(t->capture);
 if(rc){stop(t,M16_STREAM_GAP);return rc;}
 memset(t->bindings,0,sizeof(t->bindings));t->error=0;t->stopping=0;return 0;
}
