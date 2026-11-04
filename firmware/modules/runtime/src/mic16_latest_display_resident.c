#include "mic16_latest_display_resident.h"
#include <string.h>
#define MAGIC 0x4c445231u
static int fail(m16_latest_display_resident *t,int rc){
 if(!t->error)t->error=rc<0?rc:M16_LW_INVALID;
 t->state=M16_CTL_FAULT;return t->error;
}
static void measured(m16_latest_display_resident *t,unsigned stage,uint64_t began){
 uint64_t end=t->ticks(t->context),delta;
 /* 待机后冻结运行阶段统计，不能让空轮询把稍后读取的计算总耗时越加越大。 */
 if(t->state!=M16_CTL_RUNNING&&t->state!=M16_CTL_DRAINING)return;
 if(end<began){(void)fail(t,M16_LW_CLOCK);return;}
 delta=end-began;
 if(delta>t->service_max_ticks[stage])t->service_max_ticks[stage]=delta;
 if(UINT64_MAX-t->service_total_ticks[stage]<delta)t->service_total_ticks[stage]=UINT64_MAX;
 else t->service_total_ticks[stage]+=delta;
}
int m16_latest_display_resident_init(m16_latest_display_resident *t,m16_window_capture *capture,m16_front_ring *r,
 m16_fast_doa_task *d,m16_resident_control *c,uint64_t hz,void *context,uint64_t (*ticks)(void *),
 int (*send)(void *,unsigned,const unsigned char *,unsigned),int (*backend)(void *),void *backend_context){
 int rc;
 if(!t||t->magic||!capture||!r||!d||!c||!hz||!ticks||!send||!backend||!backend_context)return M16_LW_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->control=c;t->tick_hz=hz;t->context=context;
 t->ticks=ticks;t->send=send;t->backend=backend;t->backend_context=backend_context;t->latest_sequence=UINT64_MAX;
 rc=m16_pcm_acquisition_init(&t->producer,capture,r->storage->pcm);if(rc)return rc;
 rc=m16_latest_window_init(&t->analysis,&t->producer,r,d,&t->snapshot);if(rc)return rc;
 return m16_pcm_wire_init(&t->raw_wire);
}
static int start(m16_latest_display_resident *t){
 int rc;
 if(t->state!=M16_CTL_IDLE||t->error)return fail(t,M16_LW_SEQUENCE);
 if(t->producer.capture->phase==M16_WC_STOPPED){rc=m16_pcm_acquisition_restart(&t->producer);if(rc)return fail(t,rc);}
 t->raw_next_frame=t->raw_copied=t->raw_sent=t->raw_packets=t->wire_packets=t->wire_backpressure=0;
 t->raw_head=t->raw_tail=t->raw_count=t->raw_sending=t->raw_copying=t->wire_pending=t->wire_part=t->raw_waiting=0;
 t->discarded_raw=t->discarded_results=t->last_compute_ticks=0;
 memset(t->service_total_ticks,0,sizeof(t->service_total_ticks));memset(t->service_max_ticks,0,sizeof(t->service_max_ticks));
 t->first_output_tick=t->last_output_tick=t->max_output_gap_ticks=t->longest_step_ticks=0;
 t->latest_sequence=UINT64_MAX;t->state=M16_CTL_STARTING;return 0;
}
static int raw_copy(m16_latest_display_resident *t){
 const m16_pcm_frame_metadata *m;const float *p;unsigned end;int rc;
 if(!t->raw_copying){
  if(t->raw_next_frame>=t->producer.next_frame||t->raw_count>=2u)return 0;
  t->raw_copying=1;t->raw_cursor=0;t->raw_frame[t->raw_tail]=t->raw_next_frame;
 }
 rc=m16_pcm_acquisition_frame(&t->producer,t->control->epoch,t->raw_next_frame,&m,&p);if(rc)return fail(t,rc);
 if(m->first_sample!=t->raw_next_frame*2048u)return fail(t,M16_LW_SEQUENCE);
 end=t->raw_cursor+256u;if(end>M16_PA_VALUES)end=M16_PA_VALUES;
 memcpy(t->raw_pcm[t->raw_tail]+t->raw_cursor,p+t->raw_cursor,(end-t->raw_cursor)*4u);t->raw_cursor=end;
 if(end==M16_PA_VALUES){t->raw_copying=0;t->raw_count++;t->raw_tail=(t->raw_tail+1u)%2u;t->raw_next_frame++;t->raw_copied++;}
 return 0;
}
static int send_one(m16_latest_display_resident *t){
 const m16_fast_doa_result *result;const unsigned char *data;unsigned n,port;int rc;
 if(t->raw_waiting&&t->ticks(t->context)-t->raw_wait_tick>t->tick_hz)return fail(t,-1820);
 rc=m16_fast_doa_peek(t->analysis.direction,&result);if(rc<0)return fail(t,rc);
 if(rc==1){
  if(!t->wire_pending){
   rc=m16_latest_display_encode(result,t->control->boot,t->tick_hz,t->wire_part,t->packet,sizeof(t->packet),&t->wire_bytes);
   if(rc)return fail(t,rc);t->wire_pending=1;t->wire_wait_tick=t->ticks(t->context);
  }
  data=t->packet;n=t->wire_bytes;port=t->wire_part?5003u:5001u;
 }else{
  if(!t->raw_count)return 0;
  if(!t->raw_sending){
   rc=m16_pcm_wire_begin(&t->raw_wire,t->raw_pcm[t->raw_head],t->control->epoch,t->raw_frame[t->raw_head]*2048u,
    t->producer.capture->config.scale_exponent);if(rc)return fail(t,rc);t->raw_sending=1;
  }
  rc=m16_pcm_wire_packet(&t->raw_wire,&data,&n);if(rc!=1)return fail(t,rc<0?rc:M16_LW_SEQUENCE);port=5005u;
 }
 rc=t->send(t->context,port,data,n);
 if(rc==1){
  t->wire_backpressure++;
  if(port==5005u){
   if(!t->raw_waiting){t->raw_waiting=1;t->raw_wait_tick=t->ticks(t->context);}
   if(t->ticks(t->context)-t->raw_wait_tick>t->tick_hz)return fail(t,-1820);
  }else if(t->ticks(t->context)-t->wire_wait_tick>t->tick_hz)return fail(t,-1820);
  return 0;
 }
 if(rc)return fail(t,rc);
 if(port==5005u){
  t->raw_waiting=0;rc=m16_pcm_wire_ack(&t->raw_wire);if(rc)return fail(t,rc);t->raw_packets++;
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
   t->latest_sequence=result->sequence;t->last_compute_ticks=result->completed_tick-t->analysis.current.copy_started_tick;
   rc=m16_latest_window_ack(&t->analysis,result->epoch,result->sequence);if(rc)return fail(t,rc);t->wire_part=0;
  }
 }return 0;
}
static int drain(m16_latest_display_resident *t){
 int rc;
 if(t->analysis.direction->output_ready)t->discarded_results++;
 t->wire_pending=t->wire_part=0;
 rc=m16_latest_window_stop(&t->analysis);if(rc<0)(void)fail(t,rc);
 if(t->error){
  (void)m16_pcm_acquisition_abort(&t->producer,t->error);
  t->discarded_raw+=t->raw_count+t->raw_copying;t->raw_count=t->raw_copying=t->raw_sending=t->raw_waiting=0;
  m16_pcm_wire_stop(&t->raw_wire);return t->error;
 }
 if(!t->producer.active){
  /* START尚在准备阶段即收到STOP，没有新epoch音频需要排空。 */
  if(t->producer.capture->phase!=M16_WC_STOPPED)(void)m16_pcm_acquisition_abort(&t->producer,0);
 }else if(!t->producer.finish_requested&&!t->producer.closing){
  rc=m16_pcm_acquisition_request_finish(&t->producer);if(rc)return fail(t,rc);
 }
 if(t->producer.active&&!t->producer.closing){
  if(raw_copy(t))return t->error;
  if(send_one(t))return t->error;
  if(m16_pcm_acquisition_input_complete(&t->producer)&&t->raw_next_frame==t->producer.next_frame&&
   !t->raw_copying&&!t->raw_count&&m16_latest_window_releasable(&t->analysis)){
   m16_window_capture *c=t->producer.capture;
   if(c->completed<t->producer.capture_completed_begin||c->warmup_completed<t->producer.warmup_completed_begin||
    c->completed-t->producer.capture_completed_begin<c->warmup_completed-t->producer.warmup_completed_begin||
    (c->completed-t->producer.capture_completed_begin)-(c->warmup_completed-t->producer.warmup_completed_begin)!=t->producer.next_frame||
    t->producer.frames_taken!=t->producer.leases_released)return fail(t,M16_LW_SEQUENCE);
   rc=m16_pcm_acquisition_finish_stop(&t->producer);if(rc)return fail(t,rc);
  }
 }
 if(m16_pcm_acquisition_releasable(&t->producer)&&m16_latest_window_releasable(&t->analysis))t->state=M16_CTL_IDLE;
 return 0;
}
int m16_latest_display_resident_step(m16_latest_display_resident *t){
 unsigned action,busy=0,was_busy;int rc;uint64_t began,part,end,us=M16_CONTROL_UNKNOWN;m16_control_progress progress;
 if(!t||t->magic!=MAGIC)return M16_LW_INVALID;
 began=t->ticks(t->context);t->iterations++;was_busy=t->state==M16_CTL_RUNNING||t->state==M16_CTL_DRAINING;
 /* 采集在每轮最前面推进；它不查看分析进度，也不等待RAW交接。 */
 part=t->ticks(t->context);rc=m16_pcm_acquisition_step(&t->producer);measured(t,0,part);if(rc<0)(void)fail(t,rc);
 action=m16_control_take_action(t->control);
 if(action==M16_CTL_START)(void)start(t);
 else if(action==M16_CTL_STOP&&t->state!=M16_CTL_FAULT)t->state=M16_CTL_DRAINING;
 else if(action&&action!=M16_CTL_STOP)(void)fail(t,M16_LW_SEQUENCE);
 part=t->ticks(t->context);rc=t->backend(t->backend_context);measured(t,1,part);if(rc<0)(void)fail(t,rc);
 if(t->state==M16_CTL_STARTING&&!t->error&&t->producer.capture->phase==M16_WC_READY){
  rc=m16_pcm_acquisition_start(&t->producer,t->control->epoch);if(rc)(void)fail(t,rc);
  else{
   rc=m16_latest_window_start(&t->analysis,t->control->epoch,t->ticks(t->context));
   if(rc)(void)fail(t,rc);else t->state=M16_CTL_RUNNING;
  }
 }
 if(t->state==M16_CTL_DRAINING||t->state==M16_CTL_FAULT)(void)drain(t);
 else if(t->state==M16_CTL_RUNNING&&!t->error){
  part=t->ticks(t->context);(void)raw_copy(t);measured(t,2,part);
  if(!t->error){part=t->ticks(t->context);(void)send_one(t);measured(t,3,part);}
  if(!t->error){part=t->ticks(t->context);rc=m16_latest_window_step(&t->analysis,part);measured(t,4,part);if(rc<0)(void)fail(t,rc);}
  if(t->producer.next_frame<t->raw_next_frame||t->producer.next_frame-t->raw_next_frame>=20u)(void)fail(t,M16_PA_EXPIRED);
 }
 if(t->producer.capture->phase==M16_WC_RUNNING||t->producer.capture->phase==M16_WC_FINISHING||
  t->producer.capture->phase==M16_WC_DRAINING||t->producer.capture->phase==M16_WC_QUARANTINE)busy|=M16_CTL_DMA_BUSY;
 if(!m16_front_ring_releasable(t->analysis.ring))busy|=M16_CTL_PL_BUSY;
 if(t->analysis.active)busy|=M16_CTL_CPU_BUSY;
 if(t->raw_count||t->raw_copying||t->analysis.direction->output_ready)busy|=M16_CTL_OUTPUT_BUSY;
 if(t->latest_sequence!=UINT64_MAX)us=(t->last_compute_ticks/t->tick_hz)*1000000u+(t->last_compute_ticks%t->tick_hz)*1000000u/t->tick_hz;
 progress=(m16_control_progress){t->state,t->control->epoch,busy,t->error,t->latest_sequence,us,
  t->state==M16_CTL_IDLE?0u:t->raw_count+t->raw_copying+(t->analysis.phase==M16_LW_IDLE?0u:1u)};
 rc=m16_control_update(t->control,&progress);if(rc)(void)fail(t,rc);
 end=t->ticks(t->context);if(end<began)(void)fail(t,M16_LW_CLOCK);else if(was_busy&&end-began>t->longest_step_ticks)t->longest_step_ticks=end-began;
 return t->error?t->error:(t->state==M16_CTL_IDLE?0:1);
}
