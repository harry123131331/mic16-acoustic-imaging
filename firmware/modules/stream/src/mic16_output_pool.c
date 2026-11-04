#include "mic16_output_pool.h"
#include <string.h>
#define MAGIC 0x4d4f5031u
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int valid(const m16_output_pool *p){return p&&!((uintptr_t)p%_Alignof(m16_output_pool))&&p->magic==MAGIC;}
static void increment(uint64_t *n){if(*n!=UINT64_MAX)++*n;}
static int separate(const m16_output_pool *p,const void *x,uintptr_t n){
 return apart(x,n,p,sizeof(*p))&&apart(x,n,p->scheduler,sizeof(*p->scheduler))&&apart(x,n,p->storage,p->storage_bytes);
}
static int equal(const mic16_stream_ticket *a,const mic16_stream_ticket *b){
 return a->pool==b->pool&&a->slot==b->slot&&a->owner==b->owner&&a->epoch==b->epoch&&a->generation==b->generation;
}
static int live(const m16_output_pool *p,unsigned i){
 const m16_output_pool_slot *d;const mic16_stream_slot *s;
 if(i>=p->scheduler->config.output_slots)return 0;
 d=p->slots+i;s=p->scheduler->output+i;
 return d->bound&&d->epoch==p->epoch&&p->epoch==p->scheduler->epoch&&
  (s->state==M16_STREAM_BUILD||s->state==M16_STREAM_READY||s->state==M16_STREAM_SEND)&&s->generation==d->ticket.generation;
}
static m16_output_pool_slot *build(m16_output_pool *p,const mic16_stream_ticket *t){
 if(!t||t->slot>=p->scheduler->config.output_slots||!live(p,t->slot)||
  p->scheduler->output[t->slot].state!=M16_STREAM_BUILD||!equal(t,&p->slots[t->slot].ticket))return 0;
 return p->slots+t->slot;
}
static m16_output_pool_slot *window_slot(m16_output_pool *p,uint32_t epoch,uint32_t window){
 unsigned i;for(i=0;i<p->scheduler->config.output_slots;i++)if(live(p,i)&&p->slots[i].epoch==epoch&&p->slots[i].window==window)return p->slots+i;
 return 0;
}
static int fail(m16_output_pool *p,int why){
 if(!p->error)p->error=why;
 (void)m16_output_pool_stop(p);return p->error;
}
static int poll(m16_output_pool *p,uint64_t now){
 int rc;if(!valid(p))return M16_OP_INVALID;
 if(p->stopping)return p->error?p->error:M16_STREAM_STOPPED;
 if(p->epoch!=p->scheduler->epoch)return fail(p,M16_OP_LEASE);
 rc=mic16_stream_poll(p->scheduler,now);if(rc)return fail(p,p->scheduler->failure?p->scheduler->failure:rc);
 return 0;
}
static float *audio_at(m16_output_pool *p,unsigned slot,unsigned ordinal){
 return p->storage+(uintptr_t)slot*M16_OUTPUT_SLOT_FLOATS+(uintptr_t)ordinal*M16_OUTPUT_SOURCE_FLOATS;
}
static int finish_send(m16_output_pool *p,uint64_t now);

int m16_output_pool_init(m16_output_pool *p,mic16_stream_scheduler *s,float *storage,unsigned floats){
 unsigned i;uintptr_t bytes;
 if(!p||!s||!storage||(uintptr_t)p%_Alignof(m16_output_pool)||p->magic||
  (uintptr_t)s%_Alignof(mic16_stream_scheduler)||(uintptr_t)storage%_Alignof(float)||
  s->config.output_slots<2||s->config.output_slots>3||!s->epoch||s->failure)return M16_OP_INVALID;
 bytes=(uintptr_t)s->config.output_slots*M16_OUTPUT_SLOT_FLOATS*4u;
 if(floats<bytes/4u||!apart(p,sizeof(*p),s,sizeof(*s))||!apart(p,sizeof(*p),storage,bytes)||!apart(s,sizeof(*s),storage,bytes))return M16_OP_INVALID;
 if(mic16_stream_poll(s,s->last_tick))return M16_OP_INVALID;
 for(i=0;i<s->config.output_slots;i++)if(s->output[i].state!=M16_STREAM_FREE)return M16_OP_INVALID;
 memset(p,0,sizeof(*p));p->magic=MAGIC;p->epoch=s->epoch;p->scheduler=s;p->storage=storage;p->storage_bytes=bytes;
 if(m16_audio_wire_task_init(&p->audio_wire))return M16_OP_INVALID;
 for(i=0;i<3;i++)if(m16_heatmap_wire_task_init(&p->slots[i].heatmap))return M16_OP_INVALID;
 return 0;
}
int m16_output_pool_set_localization_only(m16_output_pool *p,unsigned enabled){
 if(!valid(p)||enabled>1u)return M16_OP_INVALID;
 if(p->packets_sent||p->windows_sent||!m16_output_pool_releasable(p))return M16_OP_BUSY;
 p->localization_only=enabled;return 0;
}
int m16_output_pool_location_commit(m16_output_pool *p,uint32_t epoch,uint32_t window,uint64_t now){
 int rc=poll(p,now);if(rc)return rc;
 if(!p->localization_only)return M16_OP_INVALID;
 m16_output_pool_slot *d=window_slot(p,epoch,window);
 if(!d||p->scheduler->output[d-p->slots].state!=M16_STREAM_BUILD)return M16_OP_LEASE;
 if(!d->location_ready||!m16_heatmap_wire_task_input_releasable(&d->heatmap)||p->copy_phase!=M16_OP_COPY_IDLE)return M16_OP_BUSY;
 rc=mic16_stream_output_commit(p->scheduler,&d->ticket,now);if(rc)return fail(p,rc);
 /* 此标志只证明输出事务完整；仅定位发送器显式跳过音频分支。 */
 d->audio_verified=1;return 0;
}
int m16_output_pool_bind(m16_output_pool *p,const mic16_stream_ticket *ticket,uint64_t now){
 m16_output_pool_slot *d;const mic16_stream_slot *s;int rc;unsigned i;
 rc=poll(p,now);if(rc)return rc;
 if(!ticket||!separate(p,ticket,sizeof(*ticket))||ticket->pool!=M16_STREAM_OUTPUT||ticket->owner!=M16_STREAM_BUILD||
  ticket->slot>=p->scheduler->config.output_slots||ticket->epoch!=p->epoch)return M16_OP_LEASE;
 s=p->scheduler->output+ticket->slot;d=p->slots+ticket->slot;
 if(s->state!=M16_STREAM_BUILD||s->generation!=ticket->generation||d->bound)return M16_OP_LEASE;
 if(s->window.first_sample%40960u||s->window.first_sample/40960u>UINT32_MAX||s->window.end_sample-s->window.first_sample!=40960u)return M16_OP_INPUT;
 for(i=0;i<p->scheduler->config.output_slots;i++)if(p->slots[i].bound&&p->slots[i].window==s->window.first_sample/40960u)return M16_OP_LEASE;
 d->bound=1;d->location_ready=d->location_sent=d->location_bytes=0;
 d->count_set=d->stream_count=d->audio_count=d->reference_seen=d->audio_verified=d->samples=0;
 d->epoch=p->epoch;d->window=(uint32_t)(s->window.first_sample/40960u);d->ticket=*ticket;
 return 0;
}
static int save_location(void *context,const unsigned char *data,unsigned bytes){
 m16_output_pool_slot *d=(m16_output_pool_slot *)context;
 if(bytes>sizeof(d->location)||d->location_bytes)return -1;
 memcpy(d->location,data,bytes);d->location_bytes=bytes;return 0;
}
int m16_output_pool_location(m16_output_pool *p,const mic16_stream_ticket *ticket,const mic16_cycle_output *tracks,const float *maps,uint64_t now){
 m16_output_pool_slot *d;int rc=poll(p,now);if(rc)return rc;
 d=build(p,ticket);if(!d)return M16_OP_LEASE;
 if(d->location_ready)return M16_OP_BUSY;
 if(!tracks||!maps||(uintptr_t)tracks%_Alignof(mic16_cycle_output)||(uintptr_t)maps%_Alignof(float)||
  !separate(p,tracks,sizeof(*tracks))||!separate(p,maps,480u*325u*4u))return M16_OP_INVALID;
 d->location_bytes=0;
 rc=m16_directions_encode(d->epoch,d->window,tracks,save_location,d);if(rc!=1)return M16_OP_INPUT;
 rc=m16_heatmap_wire_task_begin(&d->heatmap,d->epoch,d->window,maps);if(rc)return fail(p,rc);
 d->location_ready=1;return 0;
}
int m16_output_pool_location_releasable(const m16_output_pool *p,const mic16_stream_ticket *ticket){
 if(!valid(p)||!ticket||ticket->slot>=p->scheduler->config.output_slots||!live(p,ticket->slot)||
  !equal(ticket,&p->slots[ticket->slot].ticket))return 0;
 return p->slots[ticket->slot].location_ready&&m16_heatmap_wire_task_input_releasable(&p->slots[ticket->slot].heatmap);
}
int m16_output_pool_audio_begin(m16_output_pool *p,const m16_cont_info *info,const double *a,const double *b,unsigned samples,uint64_t now){
 m16_output_pool_slot *d;m16_cont_info copy;uint64_t first,last;int rc=poll(p,now);if(rc)return rc;
 if(p->localization_only)return M16_OP_INVALID;
 if(p->copy_phase!=M16_OP_COPY_IDLE)return M16_OP_BUSY;
 if(!info||(uintptr_t)info%_Alignof(m16_cont_info)||!separate(p,info,sizeof(*info)))return M16_OP_INVALID;
 copy=*info;d=window_slot(p,copy.epoch,copy.window);
 if(!d||p->scheduler->output[d-p->slots].state!=M16_STREAM_BUILD)return M16_OP_LEASE;
 first=((uint64_t)copy.window*40960u*256u+624u)/625u;last=(((uint64_t)copy.window+1u)*40960u*256u+624u)/625u;
 if(copy.current_confirmed>1||copy.final>1||copy.stream_count>24||copy.output_start!=first||copy.output_end!=last||
  samples!=last-first||copy.input_start!=(uint64_t)copy.window*40960u||copy.input_end!=((uint64_t)copy.window+1u)*40960u||
  copy.available_input<copy.input_end||d->reference_seen||d->audio_verified||
  (d->count_set&&(d->stream_count!=copy.stream_count||d->samples!=samples||
   d->reference.final!=copy.final||d->reference.available_input!=copy.available_input)))return M16_OP_INPUT;
 if(copy.source_id){
  if(!copy.stream_count||copy.ordinal!=d->audio_count||copy.ordinal>=copy.stream_count||!a||!b||(uintptr_t)a%_Alignof(double)||(uintptr_t)b%_Alignof(double)||
   !separate(p,a,(uintptr_t)samples*8u)||!separate(p,b,(uintptr_t)samples*8u)||
   (d->audio_count&&d->audio[d->audio_count-1u].source_id>=copy.source_id))return M16_OP_INPUT;
 }else if(copy.current_confirmed||copy.ordinal!=copy.stream_count||d->audio_count!=copy.stream_count)return M16_OP_INPUT;
 if(!d->count_set)d->reference=copy;
 d->count_set=1;d->stream_count=copy.stream_count;d->samples=samples;
 p->copy_info=copy;p->copy_slot=(unsigned)(d-p->slots);p->copy_index=0;p->copy_samples=samples;
 p->copy_a=copy.source_id?a:0;p->copy_b=copy.source_id?b:0;
 if(!copy.source_id){d->reference=copy;d->reference_seen=1;p->copy_phase=M16_OP_COPY_READY;}
 else p->copy_phase=M16_OP_COPY_ACTIVE;
 return 0;
}
int m16_output_pool_audio_copied(const m16_output_pool *p,const m16_cont_info **info){
 if(!valid(p)||!info||(uintptr_t)info%_Alignof(const m16_cont_info *)||!separate(p,info,sizeof(*info)))return M16_OP_INVALID;
 if(p->stopping||p->copy_phase!=M16_OP_COPY_READY)return 0;
 *info=&p->copy_info;return 1;
}
int m16_output_pool_audio_release(m16_output_pool *p,uint32_t epoch,uint32_t window,uint32_t source){
 if(!valid(p)||p->copy_phase!=M16_OP_COPY_READY||p->copy_info.epoch!=epoch||p->copy_info.window!=window||p->copy_info.source_id!=source)return M16_OP_LEASE;
 p->copy_phase=M16_OP_COPY_IDLE;return 0;
}
int m16_output_pool_audio_commit(m16_output_pool *p,uint32_t epoch,uint32_t window,uint64_t now){
 m16_output_pool_slot *d;int rc=poll(p,now);if(rc)return rc;
 d=window_slot(p,epoch,window);if(!d||p->scheduler->output[d-p->slots].state!=M16_STREAM_BUILD)return M16_OP_LEASE;
 if(!d->reference_seen||!d->location_ready||!d->count_set||d->audio_count!=d->stream_count||
  !m16_heatmap_wire_task_input_releasable(&d->heatmap)||p->copy_phase!=M16_OP_COPY_IDLE)return M16_OP_BUSY;
 rc=mic16_stream_output_commit(p->scheduler,&d->ticket,now);if(rc)return fail(p,rc);
 d->audio_verified=1;return 0;
}
int m16_output_pool_step(m16_output_pool *p,uint64_t now){
 unsigned i,n,slot;int rc=poll(p,now);if(rc)return rc;
 if(p->copy_phase==M16_OP_COPY_ACTIVE){
  m16_output_pool_slot *d=p->slots+p->copy_slot;float *out;
  if(!live(p,p->copy_slot)||p->scheduler->output[p->copy_slot].state!=M16_STREAM_BUILD)return fail(p,M16_OP_LEASE);
  out=audio_at(p,p->copy_slot,p->copy_info.ordinal);n=p->copy_samples-p->copy_index;if(n>32)n=32;
  for(i=0;i<n;i++){
   float a=(float)p->copy_a[p->copy_index+i],b=(float)p->copy_b[p->copy_index+i];
   if(!(a>=-3.402823466e38f&&a<=3.402823466e38f&&b>=-3.402823466e38f&&b<=3.402823466e38f))return fail(p,M16_OP_INPUT);
   out[2u*(p->copy_index+i)]=a;out[2u*(p->copy_index+i)+1u]=b;
  }
  p->copy_index+=n;p->copied_pairs+=n;
  if(p->copy_index==p->copy_samples){d->audio[d->audio_count++]=p->copy_info;p->copy_a=p->copy_b=0;p->copy_phase=M16_OP_COPY_READY;}
 }
 /* 轮转最多推进一个热图，另一个槽发送或借用不会被改写。 */
 for(i=0;i<p->scheduler->config.output_slots;i++){
  slot=(p->map_cursor+i)%p->scheduler->config.output_slots;
  if(live(p,slot)&&p->slots[slot].location_ready&&p->slots[slot].heatmap.phase!=M16_OW_COMPLETE){
   rc=m16_heatmap_wire_task_step(&p->slots[slot].heatmap);if(rc<0)return fail(p,rc);
   p->map_cursor=(slot+1u)%p->scheduler->config.output_slots;break;
  }
 }
 if(p->send_active){
  m16_output_pool_slot *d=p->slots+p->send_slot;
  if(!live(p,p->send_slot)||p->scheduler->output[p->send_slot].state!=M16_STREAM_SEND)return fail(p,M16_OP_LEASE);
  if(p->send_index<(p->localization_only?0u:(d->stream_count?d->stream_count:1u))){
   if(!p->audio_started){
    const m16_cont_info *info=d->stream_count?d->audio+p->send_index:&d->reference;
    rc=m16_audio_wire_task_begin(&p->audio_wire,info,d->stream_count?audio_at(p,p->send_slot,p->send_index):0,d->samples);
    if(rc<0)return fail(p,rc);
    p->audio_started=1;
   }
   rc=m16_audio_wire_task_step(&p->audio_wire);if(rc<0)return fail(p,rc);
  }
  rc=finish_send(p,now);if(rc)return rc;
 }
 return 0;
}

static int finish_send(m16_output_pool *p,uint64_t now){
 m16_output_pool_slot *d;int rc;
 if(!p->send_active)return 0;
 d=p->slots+p->send_slot;
 if(p->send_index<(p->localization_only?0u:(d->stream_count?d->stream_count:1u))||!d->location_sent||d->heatmap.phase!=M16_OW_COMPLETE)return 0;
 if(p->pending&&p->packet.ticket.slot==p->send_slot)return 0;
 rc=mic16_stream_output_finish(p->scheduler,&d->ticket,now,1);if(rc)return fail(p,rc);
 d->bound=0;p->send_active=p->audio_started=0;increment(&p->windows_sent);return 0;
}
static int queue_packet(m16_output_pool *p,unsigned slot,unsigned kind,const unsigned char *data,unsigned bytes,unsigned port){
 m16_output_pool_slot *d=p->slots+slot;
 if(p->packet_serial==UINT64_MAX)return fail(p,M16_OP_COUNTER);
 p->packet=(m16_output_packet_view){0};p->packet.data=data;p->packet.bytes=bytes;p->packet.port=port;
 p->packet.ticket.owner=(uintptr_t)p;p->packet.ticket.serial=++p->packet_serial;
 p->packet.ticket.generation=d->ticket.generation;p->packet.ticket.epoch=d->epoch;p->packet.ticket.window=d->window;
 p->packet.ticket.slot=slot;p->packet.ticket.kind=kind;p->pending=1;return 1;
}
static int pick(const m16_output_pool *p,unsigned kind){
 unsigned i;int chosen=-1;
 for(i=0;i<p->scheduler->config.output_slots;i++)if(live(p,i)&&p->slots[i].location_ready){
  const m16_output_pool_slot *d=p->slots+i;
  int ready=kind==M16_OP_DIRECTION?!d->location_sent:d->heatmap.phase==M16_OW_READY;
  if(ready&&(chosen<0||d->window<p->slots[chosen].window))chosen=(int)i;
 }
 return chosen;
}
int m16_output_pool_packet(m16_output_pool *p,uint64_t now,m16_output_packet_view *view){
 int rc,chosen;unsigned i,bytes;const unsigned char *data;mic16_stream_ticket ticket;mic16_stream_window window;
 if(!valid(p)||!view||(uintptr_t)view%_Alignof(m16_output_packet_view)||!separate(p,view,sizeof(*view)))return M16_OP_INVALID;
 rc=poll(p,now);if(rc)return rc;
 if(p->pending){
  if(!live(p,p->packet.ticket.slot)||p->packet.ticket.generation!=p->slots[p->packet.ticket.slot].ticket.generation)return fail(p,M16_OP_LEASE);
  *view=p->packet;return 1;
 }
 chosen=pick(p,M16_OP_DIRECTION);
 if(chosen>=0){
  rc=queue_packet(p,(unsigned)chosen,M16_OP_DIRECTION,p->slots[chosen].location,p->slots[chosen].location_bytes,5001);
  if(rc>0)*view=p->packet;
  return rc;
 }
 chosen=pick(p,M16_OP_HEATMAP);
 if(chosen>=0){
  rc=m16_heatmap_wire_task_packet(&p->slots[chosen].heatmap,&data,&bytes,&p->wire_ticket);if(rc)return fail(p,rc);
  rc=queue_packet(p,(unsigned)chosen,M16_OP_HEATMAP,data,bytes,5003);
  if(rc>0)*view=p->packet;
  return rc;
 }
 if(!p->send_active){
  /* 先核对READY载荷，再取得唯一SEND租约，不能接管没有本层内容的槽。 */
  for(i=0;i<p->scheduler->config.output_slots;i++)if(p->scheduler->output[i].state==M16_STREAM_READY&&
   (!live(p,i)||!p->slots[i].audio_verified))return fail(p,M16_OP_LEASE);
  rc=mic16_stream_output_acquire(p->scheduler,now,&ticket,&window);
  if(rc==M16_STREAM_EMPTY||rc==M16_STREAM_BUSY)return 0;
  if(rc)return fail(p,rc);
  p->send_active=1;p->send_slot=ticket.slot;p->send_index=p->audio_started=0;p->slots[ticket.slot].ticket=ticket;
  if(p->slots[ticket.slot].window!=window.first_sample/40960u)return fail(p,M16_OP_LEASE);
 }
 rc=mic16_stream_output_current(p->scheduler,&p->slots[p->send_slot].ticket,now);if(rc)return fail(p,rc);
 if(p->audio_started){
  rc=m16_audio_wire_task_packet(&p->audio_wire,&data,&bytes,&p->wire_ticket);
  if(rc==M16_OW_BUSY)return 0;
  if(rc)return fail(p,rc);
  rc=queue_packet(p,p->send_slot,M16_OP_AUDIO,data,bytes,5001);
  if(rc>0)*view=p->packet;
  return rc;
 }
 return 0;
}
int m16_output_pool_packet_ack(m16_output_pool *p,const m16_output_packet_ticket *ticket,uint64_t now){
 const m16_output_packet_ticket *want;m16_output_pool_slot *d;int rc;
 if(!valid(p)||!ticket||(uintptr_t)ticket%_Alignof(m16_output_packet_ticket)||!separate(p,ticket,sizeof(*ticket)))return M16_OP_INVALID;
 rc=poll(p,now);if(rc)return rc;
 want=&p->packet.ticket;
 if(!p->pending||ticket->owner!=want->owner||ticket->serial!=want->serial||ticket->generation!=want->generation||
  ticket->epoch!=want->epoch||ticket->window!=want->window||ticket->slot!=want->slot||ticket->kind!=want->kind)return M16_OP_LEASE;
 if(!live(p,ticket->slot)||p->slots[ticket->slot].ticket.generation!=ticket->generation)return fail(p,M16_OP_LEASE);
 d=p->slots+ticket->slot;
 if(ticket->kind==M16_OP_DIRECTION)d->location_sent=1;
 else if(ticket->kind==M16_OP_HEATMAP){rc=m16_heatmap_wire_task_ack(&d->heatmap,&p->wire_ticket);if(rc<0)return fail(p,rc);}
 else if(ticket->kind==M16_OP_AUDIO){
  if(!p->send_active||p->send_slot!=ticket->slot)return fail(p,M16_OP_LEASE);
  rc=m16_audio_wire_task_ack(&p->audio_wire,&p->wire_ticket);if(rc<0)return fail(p,rc);
  if(rc==M16_OW_DONE){p->send_index++;p->audio_started=0;}
 }else return fail(p,M16_OP_LEASE);
 p->pending=0;increment(&p->packets_sent);
 return finish_send(p,now);
}
int m16_output_pool_stop(m16_output_pool *p){
 unsigned i;int result=0,rc;
 if(!valid(p))return M16_OP_INVALID;
 p->stopping=1;p->pending=0;p->copy_phase=M16_OP_COPY_IDLE;p->copy_a=p->copy_b=0;
 (void)m16_audio_wire_task_stop(&p->audio_wire);
 if(!p->scheduler->failure)(void)mic16_stream_abort(p->scheduler,p->error?M16_STREAM_GAP:M16_STREAM_CANCELLED);
 for(i=0;i<p->scheduler->config.output_slots;i++){
  m16_output_pool_slot *d=p->slots+i;mic16_stream_slot *s=p->scheduler->output+i;
  (void)m16_heatmap_wire_task_stop(&d->heatmap);
  if(!d->bound)continue;
  if(s->state==M16_STREAM_FREE){d->bound=0;continue;}
  if(s->generation!=d->ticket.generation||d->ticket.epoch!=p->scheduler->epoch||
   (s->state!=M16_STREAM_BUILD&&s->state!=M16_STREAM_SEND)){result=M16_OP_LEASE;continue;}
  rc=mic16_stream_output_cancel(p->scheduler,&d->ticket);
  if(rc){result=M16_OP_LEASE;continue;}
  d->bound=0;
 }
 p->send_active=p->audio_started=0;
 if(result&&!p->error)p->error=result;
 return result;
}
int m16_output_pool_releasable(const m16_output_pool *p){
 unsigned i;if(!valid(p)||p->copy_phase!=M16_OP_COPY_IDLE||p->pending||p->send_active)return 0;
 for(i=0;i<p->scheduler->config.output_slots;i++)if(p->slots[i].bound||p->scheduler->output[i].state!=M16_STREAM_FREE)return 0;
 return 1;
}
int m16_output_pool_restart(m16_output_pool *p,uint32_t epoch){
 if(!valid(p)||!p->stopping||!m16_output_pool_releasable(p)||epoch<=p->epoch||epoch!=p->scheduler->epoch||p->scheduler->failure)return M16_OP_BUSY;
 p->epoch=epoch;p->stopping=0;p->error=0;return 0;
}
