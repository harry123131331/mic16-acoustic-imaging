#include "mic16_fast_display_resident.h"
#include <string.h>
#define MAGIC 0x46524431u
static int fail(m16_fast_display_resident *t,int rc){
 if(!t->error)t->error=rc<0?rc:M16_FD_INVALID;
 t->state=M16_CTL_FAULT;return t->error;
}
static void measured(m16_fast_display_resident *t,unsigned stage,uint64_t started){
 uint64_t end=t->ticks(t->context),delta;
 if(end<started){(void)fail(t,M16_FD_CLOCK);return;}
 delta=end-started;
 if(delta>t->service_max_ticks[stage])t->service_max_ticks[stage]=delta;
 if(UINT64_MAX-t->service_total_ticks[stage]<delta)t->service_total_ticks[stage]=UINT64_MAX;
 else t->service_total_ticks[stage]+=delta;
}
int m16_fast_display_resident_init(m16_fast_display_resident *t,m16_pipeline_front_stream *s,
 m16_fast_doa_task *a,m16_resident_control *c,uint64_t hz,void *context,uint64_t (*ticks)(void *),
 int (*send)(void *,unsigned,const unsigned char *,unsigned),int (*backend)(void *),void *backend_context){
 if(!t||t->magic||!s||!a||!c||!hz||!ticks||!send||!backend||!backend_context||
  !m16_front_pipeline_releasable(s->front)||s->held||a->active||
  (s->capture->phase!=M16_WC_PREPARING&&s->capture->phase!=M16_WC_READY))return M16_FD_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->stream=s;t->frontend=(m16_fast_doa_front){s->front->ring,a,0,0,0,0};
 t->control=c;t->tick_hz=hz;t->context=context;t->ticks=ticks;t->send=send;t->backend=backend;t->backend_context=backend_context;
 t->latest_sequence=UINT64_MAX;return m16_pcm_wire_init(&t->raw_wire);
}
static int stop(m16_fast_display_resident *t){
 int rc;
 if(t->frontend.task->active){
  t->discarded_results+=t->frontend.task->output_ready;
  (void)m16_fast_doa_stop(t->frontend.task);
 }
 if(t->raw_count||t->raw_copying)t->discarded_raw+=t->raw_count+t->raw_copying;
 t->raw_count=t->raw_copying=t->raw_sending=t->wire_pending=t->wire_part=t->raw_waiting=0;m16_pcm_wire_stop(&t->raw_wire);
 rc=m16_pipeline_front_stream_stop(t->stream);if(rc<0)return fail(t,rc);
 /* STOP也必须继续驱动DMA复位确认，不能仅反复登记stop而永远停在DRAINING。 */
 rc=m16_pipeline_front_stream_step(t->stream);if(rc<0&&!t->error)return fail(t,rc);
 if(m16_pipeline_front_stream_releasable(t->stream)){
  if(!t->error)t->state=M16_CTL_IDLE;
  return 0;
 }
 return 1;
}
static int start(m16_fast_display_resident *t){
 int rc;
 if(t->state!=M16_CTL_IDLE||t->error)return fail(t,M16_FD_SEQUENCE);
 if(t->stream->stopping){rc=m16_pipeline_front_stream_restart(t->stream);if(rc)return fail(t,rc);}
 t->raw_next_frame=t->raw_copied=t->raw_sent=t->raw_packets=t->wire_packets=t->wire_backpressure=0;
 t->raw_head=t->raw_tail=t->raw_count=t->raw_sending=t->raw_copying=t->wire_pending=t->wire_part=t->raw_waiting=0;
 memset(t->service_total_ticks,0,sizeof(t->service_total_ticks));memset(t->service_max_ticks,0,sizeof(t->service_max_ticks));
 t->first_output_tick=t->last_output_tick=t->max_output_gap_ticks=t->longest_step_ticks=0;
 t->latest_sequence=UINT64_MAX;t->state=M16_CTL_STARTING;return 0;
}
static int raw_copy(m16_fast_display_resident *t){
 m16_front_ring *r=t->frontend.ring;const m16_front_frame_metadata *m;unsigned end,slot;
 if(!t->raw_copying){
  if(t->raw_next_frame>=r->next_frame||t->raw_count>=2u)return 0;
  t->raw_copying=1;t->raw_cursor=0;t->raw_frame[t->raw_tail]=t->raw_next_frame;
 }
 slot=(unsigned)(t->raw_next_frame%20u);m=&r->metadata[slot];
 if(!m->ready||m->epoch!=t->control->epoch||m->frame_seq!=t->raw_next_frame)return fail(t,M16_FD_SEQUENCE);
 end=t->raw_cursor+64u;if(end>32768u)end=32768u;
 memcpy(t->raw_pcm[t->raw_tail]+t->raw_cursor,r->storage->pcm[slot]+t->raw_cursor,(end-t->raw_cursor)*4u);
 t->raw_cursor=end;
 if(end==32768u){t->raw_copying=0;t->raw_count++;t->raw_tail=(t->raw_tail+1u)%2u;t->raw_next_frame++;t->raw_copied++;}
 return 0;
}
static int send_one(m16_fast_display_resident *t){
 const m16_fast_doa_result *result;const unsigned char *data;unsigned n,port;int rc;
 /* 新方向临时获得发送优先权时，仍须检查此前RAW的持续背压期限。 */
 if(t->raw_waiting&&t->ticks(t->context)-t->raw_wait_tick>t->tick_hz)return fail(t,-1820);
 rc=m16_fast_doa_peek(t->frontend.task,&result);if(rc<0)return fail(t,rc);
 if(rc==1){
  if(!t->wire_pending){
   rc=m16_fast_display_encode(result,t->control->boot,t->tick_hz,t->wire_part,t->packet,sizeof(t->packet),&t->wire_bytes);
   if(rc)return fail(t,rc);
   t->wire_pending=1;t->wire_wait_tick=t->ticks(t->context);
  }
  data=t->packet;n=t->wire_bytes;port=t->wire_part?5003u:5001u;
 }else{
  if(!t->raw_count)return 0;
  if(!t->raw_sending){
   rc=m16_pcm_wire_begin(&t->raw_wire,t->raw_pcm[t->raw_head],t->control->epoch,
    t->raw_frame[t->raw_head]*2048u,t->stream->capture->config.scale_exponent);
   if(rc)return fail(t,rc);
   t->raw_sending=1;
  }
  rc=m16_pcm_wire_packet(&t->raw_wire,&data,&n);if(rc!=1)return fail(t,rc<0?rc:M16_FD_SEQUENCE);
  port=5005u;
 }
 rc=t->send(t->context,port,data,n);
 if(rc==1){
  t->wire_backpressure++;
  /* 设备、采样和网络都继续推进；超时公开结束候选，不能无限积压。 */
  if(port==5005u){
   if(!t->raw_waiting){t->raw_waiting=1;t->raw_wait_tick=t->ticks(t->context);}
   if(t->ticks(t->context)-t->raw_wait_tick>t->tick_hz)return fail(t,-1820);
  }else if(t->ticks(t->context)-t->wire_wait_tick>t->tick_hz)return fail(t,-1820);
  return 0;
 }
 if(rc)return fail(t,rc);
 if(port==5005u){
  t->raw_waiting=0;
  rc=m16_pcm_wire_ack(&t->raw_wire);if(rc)return fail(t,rc);t->raw_packets++;
  if(!t->raw_wire.pcm){t->raw_sending=0;t->raw_count--;t->raw_head=(t->raw_head+1u)%2u;t->raw_sent++;}
 }else{
  t->wire_pending=0;t->wire_packets++;
  if(port==5001u){
   uint64_t tick=t->ticks(t->context);
   if(!t->first_output_tick)t->first_output_tick=tick;
   if(t->last_output_tick&&tick-t->last_output_tick>t->max_output_gap_ticks)t->max_output_gap_ticks=tick-t->last_output_tick;
   t->last_output_tick=tick;
  }
  if(++t->wire_part==(result->map_valid?3u:1u)){
   t->latest_sequence=result->sequence;
   rc=m16_fast_doa_ack(t->frontend.task,result->epoch,result->sequence);if(rc)return fail(t,rc);
   t->wire_part=0;
  }
 }
 return 0;
}
int m16_fast_display_resident_step(m16_fast_display_resident *t){
 unsigned action,busy=0;int rc;uint64_t begin,end,part;m16_control_progress progress;
 if(!t||t->magic!=MAGIC)return M16_FD_INVALID;
 begin=t->ticks(t->context);t->iterations++;
 part=t->ticks(t->context);rc=t->backend(t->backend_context);measured(t,0,part);if(rc<0)(void)fail(t,rc);
 action=m16_control_take_action(t->control);
 if(action==M16_CTL_START)(void)start(t);
 else if(action==M16_CTL_STOP&&t->state!=M16_CTL_FAULT)t->state=M16_CTL_DRAINING;
 else if(action&&action!=M16_CTL_STOP)(void)fail(t,M16_FD_SEQUENCE);
 if(t->state==M16_CTL_FAULT||t->state==M16_CTL_DRAINING){(void)stop(t);}
 else{
  part=t->ticks(t->context);rc=m16_pipeline_front_stream_step(t->stream);measured(t,1,part);if(rc<0)(void)fail(t,rc);
  if(!t->error&&t->state==M16_CTL_STARTING&&t->stream->capture->phase==M16_WC_READY){
   rc=m16_pipeline_front_stream_start(t->stream,t->control->epoch);if(rc)(void)fail(t,rc);
   else{
    rc=m16_fast_doa_start(t->frontend.task,t->control->epoch,t->ticks(t->context));
    if(rc)(void)fail(t,rc);else t->state=M16_CTL_RUNNING;
   }
  }
  if(!t->error&&t->state==M16_CTL_RUNNING){
   part=t->ticks(t->context);rc=m16_fast_doa_front_step(&t->frontend,part);measured(t,2,part);if(rc<0)(void)fail(t,rc);
   if(!t->error){part=t->ticks(t->context);(void)raw_copy(t);measured(t,3,part);}
   if(!t->error){part=t->ticks(t->context);(void)send_one(t);measured(t,4,part);}
   /* RAW无法跟上且原环已将未保存帧覆盖时，明确故障，不跳过未发送区间。 */
   if(t->raw_next_frame<t->frontend.ring->next_frame&&
    t->frontend.ring->next_frame-t->raw_next_frame>=20u)(void)fail(t,M16_FD_SEQUENCE);
  }
 }
 if(t->stream->capture->phase==M16_WC_RUNNING||t->stream->capture->phase==M16_WC_DRAINING||
  t->stream->capture->phase==M16_WC_QUARANTINE)busy|=M16_CTL_DMA_BUSY;
 if(!m16_front_pipeline_releasable(t->stream->front))busy|=M16_CTL_PL_BUSY;
 if(t->frontend.task->phase!=M16_FD_IDLE)busy|=M16_CTL_CPU_BUSY;
 if(t->raw_count||t->raw_copying||t->frontend.task->output_ready)busy|=M16_CTL_OUTPUT_BUSY;
 progress=(m16_control_progress){t->state,t->control->epoch,busy,t->error,t->latest_sequence,M16_CONTROL_UNKNOWN,
  t->state==M16_CTL_IDLE?0u:t->raw_count+t->raw_copying+t->frontend.task->output_ready};
 rc=m16_control_update(t->control,&progress);if(rc)(void)fail(t,rc);
 end=t->ticks(t->context);if(end<begin)(void)fail(t,M16_FD_CLOCK);
 else if(end-begin>t->longest_step_ticks)t->longest_step_ticks=end-begin;
 return t->error?t->error:(t->state==M16_CTL_IDLE?0:1);
}
