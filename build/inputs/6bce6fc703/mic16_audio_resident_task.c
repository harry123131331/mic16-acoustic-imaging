#include "mic16_audio_resident_task.h"
#include "mic16_capture_stream_front.h"
#include <string.h>
#define MAGIC 0x4d415231u
static int valid(const m16_audio_resident_task *t){return t&&!((uintptr_t)t%_Alignof(m16_audio_resident_task))&&t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static uint64_t now(const m16_audio_resident_task *t){return t->capture->capture->io.ticks(t->capture->capture->io.context);}
static void increment(uint64_t *n){if(*n!=UINT64_MAX)++*n;}
static unsigned key_size(const char *key){unsigned n;if(!key)return 0;for(n=0;n<512u;n++)if(!key[n])return n;return 0;}
static uint64_t microseconds(uint64_t ticks,uint64_t hz){
 uint64_t whole=ticks/hz,part=ticks%hz;
 if(whole>UINT64_MAX/1000000u||part>UINT64_MAX/1000000u)return UINT64_MAX;
 whole*=1000000u;part=part*1000000u/hz;
 return whole>UINT64_MAX-part?UINT64_MAX:whole+part;
}
static int regions(m16_audio_resident_task *t,m16_capture_stream *c,m16_audio_pipeline_task *a,m16_output_pool *o,
 m16_resident_control *control,const float *reference,const void *extra,uintptr_t extra_bytes){
 const void *p[33];uintptr_t size[33];unsigned i,j,count=26;m16_detection_task *d=a->detection;m16_source_audio_task *s=a->source_audio;
 uint64_t physical[6],length[6];
 p[0]=t;size[0]=sizeof(*t);p[1]=c;size[1]=sizeof(*c);p[2]=c->scheduler;size[2]=sizeof(*c->scheduler);
 p[3]=c->capture;size[3]=sizeof(*c->capture);p[4]=o;size[4]=sizeof(*o);p[5]=o->storage;size[5]=o->storage_bytes;
 p[6]=a;size[6]=sizeof(*a);p[7]=d;size[7]=sizeof(*d);p[8]=d->work;size[8]=sizeof(*d->work);
 p[9]=d->coarse;size[9]=sizeof(*d->coarse);p[10]=d->spectrum;size[10]=sizeof(*d->spectrum);
 p[11]=d->spectrum->owner;size[11]=sizeof(*d->spectrum->owner);p[12]=d->runtime;size[12]=sizeof(*d->runtime);
 p[13]=d->runtime->arena;size[13]=(uintptr_t)d->runtime->words*8u;p[14]=d->statistics_storage;size[14]=sizeof(*d->statistics_storage);
 p[15]=s;size[15]=sizeof(*s);p[16]=s->enhancement;size[16]=sizeof(*s->enhancement);p[17]=s->enhancement->work;size[17]=sizeof(*s->enhancement->work);
 p[18]=s->continuous;size[18]=sizeof(*s->continuous);p[19]=s->continuous->work;size[19]=sizeof(*s->continuous->work);
 p[20]=a->observation;size[20]=sizeof(*a->observation);p[21]=control;size[21]=sizeof(*control);p[22]=reference;size[22]=8192u*16u*4u;
 for(i=0;i<3;i++){p[23+i]=c->capture->config.storage[i];size[23+i]=c->capture->config.packet_samples*64u+128u;physical[i]=c->capture->config.physical[i];length[i]=size[23+i];}
 if(c->front){
  p[count]=c->front;size[count++]=sizeof(*c->front);p[count]=c->front_context;size[count++]=sizeof(*c->front_context);
  p[count]=c->front->ring;size[count++]=sizeof(*c->front->ring);p[count]=c->front->ring->storage;size[count++]=sizeof(*c->front->ring->storage);
  p[count]=c->front->ring->work;size[count++]=sizeof(*c->front->ring->work);p[count]=c->front_context->pcm;size[count++]=40960u*64u;
 }
 if(extra){p[count]=extra;size[count++]=extra_bytes;}
 for(i=0;i<count;i++)for(j=i+1;j<count;j++)if(!apart(p[i],size[i],p[j],size[j]))return 0;
 if(d->coarse->io.arena_address<64u||d->spectrum->owner->io.arena_address<64u||d->runtime->io.arena_address<64u)return 0;
 /* 前端地址指向words；投影区额外保守覆盖首尾保护行，最终地址对应仍由平台证明。 */
 physical[3]=(uint64_t)d->coarse->io.arena_address-64u;length[3]=sizeof(*d->coarse);
 physical[4]=(uint64_t)d->spectrum->owner->io.arena_address-64u;length[4]=sizeof(*d->spectrum->owner);
 physical[5]=(uint64_t)d->runtime->io.arena_address-64u;length[5]=(uint64_t)d->runtime->words*8u+128u;
 for(i=0;i<6;i++){
  if(physical[i]+length[i]>UINT64_C(0x100000000))return 0;
  for(j=i+1;j<6;j++)if(!(physical[i]+length[i]<=physical[j]||physical[j]+length[j]<=physical[i]))return 0;
 }
 return 1;
}
int m16_audio_resident_init(m16_audio_resident_task *t,m16_capture_stream *c,m16_audio_pipeline_task *a,
 m16_output_pool *o,m16_resident_control *control,const float *reference,const char *key,const m16_audio_resident_io *io){
 unsigned i,n=key_size(key);m16_control_progress progress={M16_CTL_IDLE,0,0,0,UINT64_MAX,UINT64_MAX,0};
 if(!t||(uintptr_t)t%_Alignof(m16_audio_resident_task)||t->magic||!c||!a||!o||!control||!reference||(uintptr_t)reference%4u||!io||!io->try_send||!n||
  !apart(t,sizeof(*t),io,sizeof(*io))||!apart(t,sizeof(*t),key,n+1u)||
  !m16_audio_pipeline_releasable(a)||!m16_output_pool_releasable(o)||c->scheduler!=o->scheduler||c->stopping||c->scheduler->failure||
  c->scheduler->epoch!=1u||o->epoch!=1u||a->source_audio->epoch!=1u||a->source_audio->next_sample||a->source_audio->closed||
  a->source_audio->continuous->work->windows||control->epoch||control->state!=M16_CTL_IDLE||control->action||
  (control->capabilities&(M16_CTL_CONTINUOUS|M16_CTL_SOURCE_AUDIO))!=(M16_CTL_CONTINUOUS|M16_CTL_SOURCE_AUDIO)||
  (control->capabilities&M16_CTL_VIDEO)||
  (c->capture->phase!=M16_WC_PREPARING&&c->capture->phase!=M16_WC_READY)||
  memcmp(key,a->source_audio->pose_reference_key,n+1u)||!regions(t,c,a,o,control,reference,0,0))return M16_AR_INVALID;
 for(i=0;i<3;i++)if(c->bindings[i].state||c->scheduler->input[i].state!=M16_STREAM_FREE||c->scheduler->output[i].state!=M16_STREAM_FREE)return M16_AR_INVALID;
 if(m16_control_update(control,&progress))return M16_AR_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->epoch=1;t->reference=reference;t->io=*io;t->capture=c;t->pipeline=a;t->output=o;t->control=control;
 memcpy(t->pose_reference_key,key,n+1u);t->last_window=t->last_detection_us=t->last_compute_us=UINT64_MAX;return 0;
}
int m16_audio_resident_bind_backend(m16_audio_resident_task *t,void *context,uintptr_t bytes,int (*service)(void *)){
 if(!valid(t)||!context||!bytes||!service||t->service_backend||t->used||t->in_step||t->phase!=M16_AR_IDLE||
  !regions(t,t->capture,t->pipeline,t->output,t->control,t->reference,context,bytes))return M16_AR_INVALID;
 t->backend_context=context;t->backend_bytes=bytes;t->service_backend=service;return 0;
}
static int fail(m16_audio_resident_task *t,int reason){
 if(!t->error)t->error=reason?reason:M16_AR_INVALID;
 if(t->phase!=M16_AR_DRAIN_ERROR&&t->phase!=M16_AR_FAILED){t->phase=M16_AR_DRAIN_ERROR;t->stop_sent=0;}
 return t->error;
}
static int stopped(const m16_audio_resident_task *t){
 return !t->input_held&&!t->computing&&m16_audio_pipeline_releasable(t->pipeline)&&
  m16_capture_stream_releasable(t->capture)&&m16_output_pool_releasable(t->output);
}
int m16_audio_resident_releasable(const m16_audio_resident_task *t){
 return valid(t)&&!t->in_step&&(t->phase==M16_AR_STOPPED||t->phase==M16_AR_FAILED)&&stopped(t);
}
static int start(m16_audio_resident_task *t){
 int rc;uint32_t epoch=t->control->epoch;
 if((t->phase!=M16_AR_IDLE&&t->phase!=M16_AR_STOPPED)||t->error)return fail(t,M16_AR_SEQUENCE);
 if(!t->used){if(epoch!=1u)return fail(t,M16_AR_SEQUENCE);}
 else{
  if(!stopped(t)||epoch<=t->epoch)return fail(t,M16_AR_SEQUENCE);
  rc=m16_capture_stream_restart(t->capture,epoch,t->capture->scheduler->config.context_id,t->capture->scheduler->config.reference_id);if(rc)return fail(t,rc);
  rc=m16_audio_pipeline_reset(t->pipeline,epoch,t->pose_reference_key);if(rc)return fail(t,rc);
  rc=m16_output_pool_restart(t->output,epoch);if(rc)return fail(t,rc);
 }
 t->epoch=epoch;t->used=1;t->phase=M16_AR_STARTING;t->computed_windows=0;
 t->last_window=t->last_detection_us=t->last_compute_us=UINT64_MAX;
 t->input_held=t->output_reserved=t->output_bound=t->computing=t->finishing=0;
 t->location_started=t->location_held=t->audio_copying=t->audio_pending=t->stop_sent=0;return 0;
}
static int reset_hardware(m16_audio_resident_task *t){
 int rc=m16_output_pool_stop(t->output);if(rc)return fail(t,rc);
 rc=m16_capture_stream_stop(t->capture);if(rc)return fail(t,rc);
 t->phase=M16_AR_RESETTING;return 0;
}
static int request_stop(m16_audio_resident_task *t){
 int rc;
 if(t->error||t->phase==M16_AR_DRAIN_ERROR||t->phase==M16_AR_FAILED)return t->error;
 if(t->phase==M16_AR_IDLE||t->phase==M16_AR_STARTING){
  t->epoch=t->control->epoch;t->used=1;
  if(t->capture->capture->phase==M16_WC_RUNNING){rc=m16_capture_stream_request_finish(t->capture);if(rc)return fail(t,rc);t->phase=M16_AR_FINISH_INPUT;return 0;}
  return reset_hardware(t);
 }
 if(t->phase==M16_AR_RUNNING){
  rc=m16_capture_stream_request_finish(t->capture);if(rc)return fail(t,rc);t->phase=M16_AR_FINISH_INPUT;
 }
 return 0;
}
static int drain_error(m16_audio_resident_task *t){
 int rc;
 if(!t->stop_sent){
  t->stop_sent=1;
  /* 先停止输出对连续工作区的CPU读取，再使计算租约失效并排空设备。 */
  (void)m16_output_pool_stop(t->output);(void)m16_audio_pipeline_stop(t->pipeline);(void)m16_capture_stream_stop(t->capture);
  if(t->output_reserved&&!t->output_bound)(void)mic16_stream_output_cancel(t->capture->scheduler,&t->build);
 }
 (void)m16_capture_stream_step(t->capture);
 if(!m16_audio_pipeline_releasable(t->pipeline))(void)m16_audio_pipeline_step(t->pipeline);
 if(m16_audio_pipeline_releasable(t->pipeline)){
  t->computing=0;
  if(t->input_held){rc=m16_capture_stream_release(t->capture,&t->input);if(!rc)t->input_held=0;}
 }
 if(stopped(t)){t->phase=M16_AR_FAILED;t->output_reserved=t->output_bound=0;}
 return t->error;
}
static unsigned queued(const m16_audio_resident_task *t){
 const m16_window_capture *c=t->capture->capture;const mic16_stream_scheduler *s=t->capture->scheduler;
 unsigned i,count=t->input_held&&!t->computing;
 if(t->capture->front){
  count+=t->capture->context_state==1u;
  if(t->capture->front->ring->next_frame==t->capture->next_context_first/2048u+20u)count++;
  return count;
 }
 uintptr_t key=c->io.lock(c->io.context);
 for(i=0;i<3;i++)if(c->slots[i].state==M16_WC_DONE||c->slots[i].state==M16_WC_INVALIDATE||c->slots[i].state==M16_WC_NORMALIZE||c->slots[i].state==M16_WC_PCM)count++;
 c->io.unlock(c->io.context,key);
 for(i=0;i<s->config.input_slots;i++)count+=s->input[i].state==M16_STREAM_READY;
 return count;
}
int m16_audio_resident_progress(const m16_audio_resident_task *t,m16_control_progress *p){
 unsigned phase,busy=0;const m16_window_capture *c;uintptr_t key;
 if(!valid(t)||!p||(uintptr_t)p%_Alignof(m16_control_progress)||!apart(t,sizeof(*t),p,sizeof(*p)))return M16_AR_INVALID;
 c=t->capture->capture;key=c->io.lock(c->io.context);phase=c->phase;c->io.unlock(c->io.context,key);
 if(phase==M16_WC_RUNNING||phase==M16_WC_FINISHING||phase==M16_WC_DRAINING||phase==M16_WC_QUARANTINE)busy|=M16_CTL_DMA_BUSY;
 if(!m16_detection_releasable(t->pipeline->detection))busy|=M16_CTL_PL_BUSY;
 if(t->input_held||!m16_audio_pipeline_releasable(t->pipeline))busy|=M16_CTL_CPU_BUSY;
 if(!m16_output_pool_releasable(t->output))busy|=M16_CTL_OUTPUT_BUSY;
 p->epoch=t->control->epoch;p->busy=busy;p->window=t->last_window;p->compute_us=t->last_compute_us;p->queued_windows=queued(t);p->code=0;
 if(t->error){p->state=M16_CTL_FAULT;p->code=t->error;}
 else if(t->phase==M16_AR_IDLE||t->phase==M16_AR_STOPPED)p->state=M16_CTL_IDLE;
 else if(t->phase==M16_AR_STARTING)p->state=M16_CTL_STARTING;
 else if(t->phase==M16_AR_RUNNING)p->state=M16_CTL_RUNNING;
 else p->state=M16_CTL_DRAINING;
 return 0;
}

static int transmit(m16_audio_resident_task *t){
 m16_output_packet_view view;int rc=m16_output_pool_packet(t->output,now(t),&view);
 if(rc<0)return fail(t,rc);
 if(!rc)return 0;
 rc=t->io.try_send(t->io.context,view.port,view.data,view.bytes);
 if(rc==1){increment(&t->backpressure);return 0;}
 if(rc)return fail(t,rc<0?rc:M16_AR_OUTPUT);
 rc=m16_output_pool_packet_ack(t->output,&view.ticket,now(t));if(rc)return fail(t,rc);
 increment(&t->packets_sent);return 0;
}
static int handoff(m16_audio_resident_task *t){
 const mic16_cycle_output *tracks;const mic16_pipeline_observation *observation;
 const m16_cont_info *info,*copied;const double *a,*b;unsigned n;int rc;
 uint64_t window;
 if(!t->computing)return 0;
 rc=m16_audio_pipeline_location(t->pipeline,&tracks,&observation);if(rc<0)return fail(t,rc);
 if(rc==1&&!t->location_started){
  if(t->finishing||!t->output_bound||observation!=t->pipeline->observation)return fail(t,M16_AR_SEQUENCE);
  rc=m16_output_pool_location(t->output,&t->build,tracks,t->pipeline->detection->work->maps,now(t));if(rc)return fail(t,rc);
  t->location_started=t->location_held=1;increment(&t->locations_handed);
 }
 if(t->location_held&&m16_output_pool_location_releasable(t->output,&t->build)){
  rc=m16_audio_pipeline_location_release(t->pipeline,t->epoch,(uint32_t)(t->input.window.first_sample/40960u));if(rc)return fail(t,rc);
  t->location_held=0;
 }
 if(t->audio_copying){
  rc=m16_output_pool_audio_copied(t->output,&copied);if(rc<0)return fail(t,rc);
  if(rc==1){
   rc=m16_audio_pipeline_output(t->pipeline,&info,&a,&b,&n);
   if(rc!=1||info->epoch!=copied->epoch||info->window!=copied->window||info->source_id!=copied->source_id)return fail(t,M16_AR_SEQUENCE);
   if(!info->source_id){
    if(t->audio_pending)return fail(t,M16_AR_SEQUENCE);
    t->pending_audio_window=info->window;t->audio_pending=1;
   }
   /* 本层复制已结束，先归还原F64，再确认输出池副本；MIC0不提前提交整窗。 */
   rc=m16_audio_pipeline_output_release(t->pipeline,copied->epoch,copied->window,copied->source_id);if(rc)return fail(t,rc);
   rc=m16_output_pool_audio_release(t->output,copied->epoch,copied->window,copied->source_id);if(rc)return fail(t,rc);
   t->audio_copying=0;
  }
 }
 if(!t->audio_copying){
  rc=m16_audio_pipeline_output(t->pipeline,&info,&a,&b,&n);if(rc<0)return fail(t,rc);
  if(rc==1){
   if(!t->computed_windows)return fail(t,M16_AR_SEQUENCE);
   window=t->computed_windows-1u;
   if(t->audio_pending||info->epoch!=t->epoch||info->window!=window||info->final!=t->finishing||
    info->available_input!=(t->computed_windows+(t->finishing?0u:1u))*40960u)return fail(t,M16_AR_SEQUENCE);
   rc=m16_output_pool_audio_begin(t->output,info,a,b,n,now(t));if(rc)return fail(t,rc);
   t->audio_copying=1;
  }
 }
 return 0;
}
static int complete(m16_audio_resident_task *t){
 uint64_t end=now(t),hz=t->capture->scheduler->config.tick_hz;int rc;
 if(!m16_audio_pipeline_releasable(t->pipeline)||t->location_held||t->audio_copying)return fail(t,M16_AR_SEQUENCE);
 if(t->computed_windows){
  if(!t->audio_pending||t->pending_audio_window!=t->computed_windows-1u)return fail(t,M16_AR_SEQUENCE);
  rc=m16_output_pool_audio_commit(t->output,t->epoch,t->pending_audio_window,end);if(rc)return fail(t,rc);
 }else if(t->audio_pending)return fail(t,M16_AR_SEQUENCE);
 t->audio_pending=0;
 if(t->finishing){t->computing=0;t->phase=M16_AR_FINISH_OUTPUT;return 0;}
 if(!t->input_held||!t->output_bound||!t->location_started||end<t->begin_tick||t->detection_end_tick<t->begin_tick)return fail(t,M16_AR_SEQUENCE);
 t->last_window=t->input.window.first_sample/40960u;
 t->last_detection_us=microseconds(t->detection_end_tick-t->begin_tick,hz);
 t->last_compute_us=microseconds(end-t->begin_tick,hz);
 increment(&t->computed_windows);
 rc=m16_capture_stream_release(t->capture,&t->input);if(rc)return fail(t,rc);
 t->input_held=t->output_reserved=t->output_bound=t->computing=0;
 t->location_started=0;return 0;
}
static int begin_next(m16_audio_resident_task *t){
 int rc;
 if(!t->input_held){
  rc=m16_capture_stream_take(t->capture,&t->input);
  if(rc==M16_STREAM_EMPTY){
   if(t->phase==M16_AR_FINISH_INPUT&&m16_capture_stream_input_complete(t->capture)){
    rc=m16_audio_pipeline_finish(t->pipeline);if(rc)return fail(t,rc);
    t->computing=t->finishing=1;t->audio_pending=t->audio_copying=0;t->phase=M16_AR_FINISH_AUDIO;
   }
   return 0;
  }
  if(rc)return fail(t,rc);
  t->input_held=1;
 }
 if(t->input.window.first_sample/40960u!=t->computed_windows)return fail(t,M16_AR_SEQUENCE);
 if(!t->output_reserved){
  rc=mic16_stream_output_reserve(t->capture->scheduler,&t->input.ticket,now(t),&t->build);
  if(rc==M16_STREAM_BUSY)return 0;
  if(rc)return fail(t,rc);
  t->output_reserved=1;
 }
 if(!t->output_bound){rc=m16_output_pool_bind(t->output,&t->build,now(t));if(rc)return fail(t,rc);t->output_bound=1;}
 rc=t->input.prepared?m16_audio_pipeline_begin_prepared(t->pipeline,t->input.prepared,t->reference):
  m16_audio_pipeline_begin(t->pipeline,t->input.pcm,t->reference,t->input.window.first_sample);if(rc)return fail(t,rc);
 t->computing=1;t->finishing=0;t->location_started=t->location_held=t->audio_copying=t->audio_pending=0;
 t->begin_tick=now(t);t->detection_end_tick=t->begin_tick;return 0;
}
static int advance(m16_audio_resident_task *t){
 int rc;unsigned before;
 if(t->phase==M16_AR_FAILED)return t->error;
 if(t->phase==M16_AR_DRAIN_ERROR)return drain_error(t);
 if(t->phase==M16_AR_STOPPED)return 0;
 rc=m16_capture_stream_step(t->capture);
 if(t->phase==M16_AR_RESETTING){
  if(rc<0)return fail(t,rc);
  if(stopped(t)){t->phase=M16_AR_STOPPED;return 0;}
  return 1;
 }
 if(rc<0||t->capture->stopping||t->capture->scheduler->failure)return fail(t,rc<0?rc:t->capture->scheduler->failure?t->capture->scheduler->failure:M16_AR_SEQUENCE);
 if(t->phase==M16_AR_IDLE)return 0;
 if(t->phase==M16_AR_STARTING){
  if(t->capture->capture->phase==M16_WC_READY){rc=m16_capture_stream_start(t->capture);if(rc)return fail(t,rc);t->phase=M16_AR_RUNNING;}
  return 1;
 }
 rc=m16_output_pool_step(t->output,now(t));if(rc<0)return fail(t,rc);
 rc=transmit(t);if(rc)return rc;
 if(t->phase==M16_AR_FINISH_OUTPUT){
  if(m16_output_pool_releasable(t->output)){rc=reset_hardware(t);if(rc)return rc;}
  return 1;
 }
 rc=handoff(t);if(rc)return rc;
 if(t->computing){
  before=t->pipeline->phase;rc=m16_audio_pipeline_step(t->pipeline);
  if(before==M16_AP_DETECT&&t->pipeline->phase==M16_AP_SOURCE_AUDIO)t->detection_end_tick=now(t);
  if(rc<0||rc==2)return fail(t,rc<0?rc:M16_AR_SEQUENCE);
  if(!rc){rc=complete(t);if(rc)return rc;}
  return 1;
 }
 rc=begin_next(t);return rc?rc:1;
}
int m16_audio_resident_step(m16_audio_resident_task *t){
 m16_control_progress p;unsigned action;int rc,published;
 if(!valid(t))return M16_AR_INVALID;
 if(t->in_step)return M16_AR_BUSY;
 t->in_step=1;increment(&t->steps);
 /* 不能因负返回提前退出：其它采集/输出设备也必须继续安全排空。 */
 if(t->service_backend){rc=t->service_backend(t->backend_context);if(rc<0)(void)fail(t,rc);}
 action=m16_control_take_action(t->control);
 if(t->control->state==M16_CTL_FAULT&&!t->error)(void)fail(t,t->control->code);
 if(!t->error){
  if(action==M16_CTL_START)(void)start(t);
  else if(action==M16_CTL_STOP)(void)request_stop(t);
  else if(action)(void)fail(t,M16_AR_SEQUENCE);
 }
 rc=advance(t);
 if(m16_audio_resident_progress(t,&p))rc=fail(t,M16_AR_INVALID);
 else{
  published=m16_control_update(t->control,&p);
  if(published){
   rc=fail(t,published);
   if(!m16_audio_resident_progress(t,&p))(void)m16_control_update(t->control,&p);
  }
 }
 t->in_step=0;return t->error?t->error:rc;
}
