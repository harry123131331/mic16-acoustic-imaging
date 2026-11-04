#include "mic16_graph_benchmark.h"
#include <string.h>
#define MAGIC 0x47423631u
static m16_graph_benchmark *active,*sampling;
unsigned g_m16_graph_profile_sample,g_m16_graph_profile_enabled;
static uint64_t tick(const m16_graph_benchmark *t){return t->io.ticks(t->io.context);}
static m16_graph_benchmark_run *row(m16_graph_benchmark *t){return &t->report->run[t->run];}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static uint32_t crc(uint32_t c,const void *data,unsigned n){
 const unsigned char *p=data;
 while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return c;
}
static void add(m16_graph_benchmark *t,unsigned id,uint64_t duration){
 m16_graph_benchmark_bin *b;
 if(id>=M16_GB_BINS||!row(t)->instrumented)return;
 b=&row(t)->cpu[id];b->calls++;b->ticks+=duration;if(duration>b->maximum)b->maximum=duration;
}
/* 只在宏阶段改变时读一次时钟；区间包含后端、网络及消费者，不冒称纯CPU时间。 */
static unsigned primary(const m16_graph_benchmark *t){
 const m16_audio_pipeline_task *p=t->pipeline;
 if(t->phase==M16_GB_FRONT){
  if(t->ring->phase==M16_FR_COPY)return 1u;
  if(t->ring->phase==M16_FR_SPATIAL)return 6u;
  unsigned z=t->ring->front.phase;
  return z==M16_PS_DEVICE||z==M16_PS_DISPATCH?3u:
   z==M16_PS_RECOVER||z==M16_PS_CONJUGATE||z==M16_PS_STORE?4u:
   z==M16_PS_OBSERVE||z==M16_PS_ZONES||z==M16_PS_CHECK_SPECTRUM?5u:2u;
 }
 if(t->phase==M16_GB_SNAPSHOT)return 7u;
 if(t->phase==M16_GB_PIPELINE||t->phase==M16_GB_FLUSH){
  if(p->phase==M16_AP_DETECT){
   if(p->detection->phase==M16_DT_FEATURES){
    unsigned f=p->detection->features.phase;
    if(f==M16_FP_QUIET||f==M16_FP_OBSERVED)return 40u+p->detection->features.front.phase;
    return 24u+(f<15u?f:14u);
   }
   return 8u+(p->detection->phase<13u?p->detection->phase:12u);
  }
  if(p->source_audio->phase==M16_SA_PREDICT)return 72u+p->source_audio->enhancement->phase;
  if(p->source_audio->phase==M16_SA_REBUILD||p->source_audio->phase==M16_SA_FINISH)
   return 96u+p->source_audio->continuous->phase;
 }
 return 0u;
}
static void boundary(m16_graph_benchmark *t,unsigned id,uint64_t now){
 if(t->boundary_valid){
  if(now<t->boundary_tick)row(t)->profile_clock_errors++;
  else add(t,t->boundary_bin,now-t->boundary_tick);
 }
 t->boundary_valid=1;t->boundary_bin=id;t->boundary_tick=now;
}
void m16_graph_benchmark_external(unsigned id,uint64_t duration){(void)id;(void)duration;}
/* 伪随机间隔128至383步，均值255.5；样本是实际调用时间，外推只是估计。
 * 嵌套子调用从父调用扣除，防止FFT/FIR轮询与父节点重复计算CPU时间。 */
void m16_graph_benchmark_sample_begin(unsigned id){
 m16_graph_benchmark *t=sampling;if(!t||!t->sample_active)return;
 if(t->sample_depth>=8u||id>=M16_GB_SAMPLES){t->sample_row->profile_clock_errors++;return;}
 unsigned d=t->sample_depth++;t->sample_id[d]=id;t->sample_children[d]=0;
 t->sample_start[d]=tick(t);t->sample_row->sample_clock_reads++;
}
void m16_graph_benchmark_sample_end(void){
 m16_graph_benchmark *t=sampling;if(!t||!t->sample_active||!t->sample_depth)return;
 unsigned d=--t->sample_depth;uint64_t now=tick(t);t->sample_row->sample_clock_reads++;
 uint64_t elapsed=now-t->sample_start[d],child=t->sample_children[d];
 if(now<t->sample_start[d]||elapsed<child){t->sample_row->profile_clock_errors++;return;}
 uint64_t own=elapsed-child;m16_graph_benchmark_sample *s=&t->sample_row->sample[t->sample_id[d]];
 s->calls++;s->ticks+=own;if(own>s->maximum)s->maximum=own;
 if(own&&own>UINT64_MAX/own)t->sample_row->profile_clock_errors++;
 else if(UINT64_MAX-s->squares<own*own)t->sample_row->profile_clock_errors++;
 else s->squares+=own*own;
 if(d)t->sample_children[d-1]+=elapsed;
}
static unsigned mode_of(const m16_projection_runtime *r){return r->jobs&&r->plan[0].mode<6u?r->plan[0].mode:0u;}
void m16_graph_benchmark_device_poll(const m16_projection_runtime *r,unsigned before,int rc){
 m16_graph_benchmark *t=active;if(!t||!row(t)->instrumented||r!=t->pipeline->detection->runtime)return;
 if(before==M16_PR_PREPARE&&r->phase==M16_PR_DEVICE)t->not_done_tick=0;
 if(before!=M16_PR_DEVICE||rc<0)return;
 m16_graph_benchmark_mode *m=&row(t)->mode[mode_of(r)];m->device_polls++;
 if(r->phase==M16_PR_DEVICE)t->not_done_tick=r->last_tick;
 else if(r->phase==M16_PR_COLLECT){
  uint64_t not_done=t->not_done_tick>r->submitted?t->not_done_tick-r->submitted:0;
  m->last_not_done+=not_done;
  m->completion_bracket+=r->report.device_ticks>=not_done?r->report.device_ticks-not_done:0;
 }
}
void m16_graph_benchmark_cache(const m16_projection_runtime *r,unsigned collecting,unsigned bytes,uint64_t a,uint64_t b){
 m16_graph_benchmark *t=active;if(!t||!row(t)->instrumented||r!=t->pipeline->detection->runtime)return;
 m16_graph_benchmark_mode *m=&row(t)->mode[mode_of(r)];
 if(b<a){row(t)->profile_clock_errors++;return;}
 if(collecting){m->invalidate_cpu+=b-a;m->invalidate_bytes+=bytes;}
 else{m->flush_cpu+=b-a;m->flush_bytes+=bytes;}
}
void m16_graph_benchmark_claim(const m16_projection_runtime *r,const void *client,int rc){
 m16_graph_benchmark *t=active;if(!t||!row(t)->instrumented||r!=t->pipeline->detection->runtime)return;
 unsigned i,free_slot=8u;for(i=0;i<8;i++){
  if(t->queued_client[i]==client)break;
  if(!t->queued_client[i]&&free_slot==8u)free_slot=i;
 }
 if(rc==1&&i==8u&&free_slot<8u){t->queued_client[free_slot]=client;t->queued_tick[free_slot]=tick(t);row(t)->queue_clock_reads++;}
 else if(rc==0&&i<8u){
  unsigned mode=client==&t->backend->fft.task?4u:client==&t->backend->fir?5u:0u;
  uint64_t now=tick(t);row(t)->queue_clock_reads++;
  row(t)->mode[mode].queue_waits++;row(t)->mode[mode].queue_ticks+=now-t->queued_tick[i];t->queued_client[i]=0;
 }
}
void m16_graph_benchmark_projection_complete(const m16_projection_runtime *r){
 m16_graph_benchmark *t=active;unsigned mode;
 if(!t||r!=t->pipeline->detection->runtime||t->run>=M16_GB_RUNS||r->phase!=M16_PR_COMPLETE)return;
 mode=r->plan[0].mode;if(mode>=6u)mode=0;
 for(unsigned i=1;i<r->jobs;i++)if(r->plan[i].mode!=mode)mode=0;
 m16_graph_benchmark_mode *m=&row(t)->mode[mode];
 m->transactions++;m->jobs+=r->jobs;m->prepare+=r->report.prepare_ticks;
 m->observed_device+=r->report.device_ticks;m->collect+=r->report.collect_ticks;
 m->flush_calls+=r->report.flush_calls;m->invalidate_calls+=r->report.invalidate_calls;
 for(unsigned i=0;i<r->jobs;i++){m->input_words+=r->plan[i].no;m->coefficient_words+=r->plan[i].nc;m->output_words+=r->plan[i].nr;}
}
int m16_graph_benchmark_init(m16_graph_benchmark *t,m16_graph_benchmark_report *report,m16_audio_pipeline_task *p,
 m16_shared_compute_graph *g,m16_front_ring *r,m16_front_context_task *f,const float *x,const float *q,const m16_graph_benchmark_io *io){
 const void *objects[10];uintptr_t lengths[10];
 if(!t||!report||!p||!g||!r||!f||!x||!q||!io||!io->ticks||!io->timer_hz||io->timer_hz>UINT64_MAX/300u||
  t->magic||active||(uintptr_t)t%_Alignof(m16_graph_benchmark)||(uintptr_t)report%_Alignof(m16_graph_benchmark_report)||
  (uintptr_t)x%4u||(uintptr_t)q%4u||!g->bound||g->error||g->fir_clients!=3||g->fft.clients!=7||
  f->ring!=r||f->detector!=p->detection||f->phase!=M16_FC_IDLE||r->epoch||p->source_audio->epoch!=1u||
  !m16_audio_pipeline_releasable(p)||!m16_front_ring_releasable(r))return M16_GB_INVALID;
 objects[0]=p;lengths[0]=sizeof(*p);objects[1]=g;lengths[1]=sizeof(*g);objects[2]=r;lengths[2]=sizeof(*r);
 objects[3]=f;lengths[3]=sizeof(*f);objects[4]=x;lengths[4]=40960u*16u*4u;objects[5]=q;lengths[5]=8192u*16u*4u;
 objects[6]=p->detection->work;lengths[6]=sizeof(*p->detection->work);objects[7]=r->storage;lengths[7]=sizeof(*r->storage);
 objects[8]=p->source_audio->enhancement->work;lengths[8]=sizeof(*p->source_audio->enhancement->work);
 objects[9]=p->source_audio->continuous->work;lengths[9]=sizeof(*p->source_audio->continuous->work);
 if(!apart(t,sizeof(*t),report,sizeof(*report)))return M16_GB_INVALID;
 for(unsigned i=0;i<10;i++)if(!apart(t,sizeof(*t),objects[i],lengths[i])||!apart(report,sizeof(*report),objects[i],lengths[i]))return M16_GB_INVALID;
 *t=(m16_graph_benchmark){0};memset(report,0,sizeof(*report));t->magic=MAGIC;t->pipeline=p;t->backend=g;t->ring=r;t->front_context=f;
 t->input=x;t->reference=q;t->report=report;t->io=*io;t->phase=M16_GB_START;
 memcpy(t->key,p->source_audio->pose_reference_key,sizeof(t->key));
 report->magic=MAGIC;report->version=2;report->timer_hz=io->timer_hz;
 for(unsigned i=0;i<128;i++){uint64_t a=tick(t),b=tick(t);if(b<a)return M16_GB_SEQUENCE;report->clock_pair_ticks+=b-a;}
 report->clock_pair_samples=128;active=t;return 0;
}
static int fail(m16_graph_benchmark *t,int rc){
 if(!t->error)t->error=rc?rc:M16_GB_INVALID;
 t->report->error=(uint32_t)t->error;t->report->done=2;row(t)->error=(uint32_t)t->error;t->phase=M16_GB_DRAIN;return 1;
}
int m16_graph_benchmark_abort(m16_graph_benchmark *t,int rc){
 if(!t||t->magic!=MAGIC||t!=active||t->phase==M16_GB_DONE||t->phase==M16_GB_FAILED)return M16_GB_INVALID;
 return fail(t,rc);
}
static int observe(m16_graph_benchmark *t,unsigned event,const m16_cont_info *info,const double *a,const double *b,unsigned n){
 return t->io.observe?t->io.observe(t->io.context,event,t->pipeline,info,a,b,n):0;
}
static int consume(m16_graph_benchmark *t){
 const mic16_cycle_output *tracks;const mic16_pipeline_observation *o;
 const m16_cont_info *info;const double *a,*b;unsigned n;int rc;m16_graph_benchmark_run *v=row(t);
 rc=m16_audio_pipeline_location(t->pipeline,&tracks,&o);if(rc<0)return rc;
 if(rc){
  rc=observe(t,M16_GB_EVENT_LOCATION,0,0,0,0);if(rc)return rc;
  v->candidates=o->candidate_count;v->sources=tracks->confirmed_count;v->locations++;
  v->crc=crc(v->crc,o->features,sizeof(o->features));v->crc=crc(v->crc,o->logits,sizeof(o->logits));
  v->crc=crc(v->crc,o->scores,sizeof(o->scores));v->crc=crc(v->crc,o->directions,sizeof(o->directions));v->crc=crc(v->crc,o->valid,sizeof(o->valid));
  if(!v->detection_ticks)v->detection_ticks=tick(t)-v->started;
  rc=m16_audio_pipeline_location_release(t->pipeline,t->pipeline->source_audio->epoch,0);if(rc)return rc;
 }
 rc=m16_audio_pipeline_output(t->pipeline,&info,&a,&b,&n);if(rc<0)return rc;
 if(rc){
  rc=observe(t,M16_GB_EVENT_AUDIO,info,a,b,n);if(rc)return rc;
  v->crc=crc(v->crc,a,n*8u);v->crc=crc(v->crc,b,n*8u);v->audio_values+=(uint64_t)n*2u;v->audio_chunks++;
  rc=m16_audio_pipeline_output_release(t->pipeline,info->epoch,info->window,info->source_id);if(rc)return rc;
 }
 return 0;
}
static int step_base(m16_graph_benchmark *t){
 m16_graph_benchmark_run *v;int rc;
 if(!t||t->magic!=MAGIC)return M16_GB_INVALID;
 if(t->phase==M16_GB_DONE)return 0;
 if(t->phase==M16_GB_FAILED)return t->error;
 if(t!=active)return M16_GB_INVALID;
 t->report->phase=t->phase;t->report->current=t->run;v=row(t);v->steps++;
 rc=m16_shared_compute_graph_service(t->backend);
 if(rc<0&&t->phase!=M16_GB_DRAIN)return fail(t,rc);
 if(t->phase==M16_GB_DRAIN){
  if(!t->stop_sent){t->stop_sent=1;(void)m16_audio_pipeline_stop(t->pipeline);}
  if(!m16_audio_pipeline_releasable(t->pipeline))(void)m16_audio_pipeline_step(t->pipeline);
  if(m16_audio_pipeline_releasable(t->pipeline)){
   (void)m16_front_context_stop(t->front_context);(void)m16_front_ring_stop(t->ring);
   if(t->front_context->phase==M16_FC_IDLE&&m16_front_ring_releasable(t->ring)){
    t->report->owners_released=1;t->phase=M16_GB_FAILED;t->report->phase=t->phase;active=0;return t->error;
   }
  }return 1;
 }
 if(t->phase==M16_GB_START){
  if(t->run){rc=m16_audio_pipeline_reset(t->pipeline,t->run+1u,t->key);if(rc)return fail(t,rc);}
  rc=m16_front_ring_start(t->ring,t->run+1u,tick(t));if(rc)return fail(t,rc);
  v->instrumented=t->run==2u||t->run==3u;g_m16_graph_profile_enabled=v->instrumented;v->crc=UINT32_MAX;v->started=tick(t);
  t->FFT_before=t->backend->fft.task.transactions;t->FIR_before=t->backend->fir.transactions;t->fallback_before=t->backend->fft.task.range_fallbacks;
  t->fir_requests_before=t->backend->fir.requests;t->fir_input_before=t->backend->fir.input_words;t->fir_output_before=t->backend->fir.output_words;
  t->frame=t->feeding=0;t->phase=M16_GB_FRONT;t->boundary_valid=0;t->sample_countdown=256u;t->random_state=0x12345678u+t->run*0x9e3779b9u;
  memset(t->queued_client,0,sizeof(t->queued_client));
  if(v->instrumented){boundary(t,primary(t),v->started);}
  return 1;
 }
 if(t->phase==M16_GB_FRONT){
  if(!t->feeding){
   rc=m16_front_ring_begin(t->ring,t->input+t->frame*32768u,32768u,t->run+1u,t->frame,t->frame*2048u,tick(t));if(rc)return fail(t,rc);
   t->feeding=1;
  }
  rc=m16_front_ring_step(t->ring,tick(t));if(rc<0)return fail(t,rc);
  if(!rc){t->feeding=0;v->input_frames++;if(++t->frame==20u){v->front_ticks=tick(t)-v->started;
   rc=m16_front_context_begin(t->front_context);if(rc)return fail(t,rc);t->phase=M16_GB_SNAPSHOT;}}
  return 1;
 }
 if(t->phase==M16_GB_SNAPSHOT){
  rc=m16_front_context_step(t->front_context);if(rc<0)return fail(t,rc);
  if(!rc){v->snapshot_ticks=tick(t)-v->started-v->front_ticks;
   rc=m16_audio_pipeline_begin_prepared(t->pipeline,t->front_context,t->reference);if(rc)return fail(t,rc);t->phase=M16_GB_PIPELINE;}
  return 1;
 }
 if(t->phase==M16_GB_PIPELINE||t->phase==M16_GB_FLUSH){
  rc=consume(t);if(rc)return fail(t,rc);
  rc=m16_audio_pipeline_step(t->pipeline);
  if(rc<0||rc==2)return fail(t,rc<0?rc:M16_GB_SEQUENCE);
  if(!rc){if(t->phase==M16_GB_PIPELINE){v->normal_ticks=tick(t)-v->started;t->phase=M16_GB_RETIRE;}
   else {v->flush_ticks=tick(t)-t->flush_started;t->phase=M16_GB_STOP_FRONT;}}
  return 1;
 }
 if(t->phase==M16_GB_RETIRE){
  rc=m16_front_context_retire(t->front_context);if(rc)return fail(t,rc);
  rc=observe(t,M16_GB_EVENT_FINISH,0,0,0,0);if(rc)return fail(t,rc);
  rc=m16_audio_pipeline_finish(t->pipeline);if(rc)return fail(t,rc);t->flush_started=tick(t);t->phase=M16_GB_FLUSH;return 1;
 }
 if(t->phase==M16_GB_STOP_FRONT){
  rc=m16_front_ring_stop(t->ring);if(rc<0)return fail(t,rc);if(!m16_front_ring_releasable(t->ring))return 1;
  v->crc^=UINT32_MAX;v->total_ticks=tick(t)-v->started;
  if(v->instrumented){boundary(t,0u,v->started+v->total_ticks);t->boundary_valid=0;}
  v->FIR_requests=t->backend->fir.requests-t->fir_requests_before;v->FIR_input_words=t->backend->fir.input_words-t->fir_input_before;
  v->FIR_output_words=t->backend->fir.output_words-t->fir_output_before;
  v->FFT_jobs=t->backend->fft.task.transactions-t->FFT_before;v->FIR_jobs=t->backend->fir.transactions-t->FIR_before;
  v->range_fallbacks=t->backend->fft.task.range_fallbacks-t->fallback_before;
  if(v->input_frames!=20u||v->locations!=1u||!v->FFT_jobs||!v->FIR_jobs)return fail(t,M16_GB_SEQUENCE);
  if((t->io.expected_crc&&v->crc!=t->io.expected_crc)||(t->run&&v->crc!=t->report->run[0].crc))return fail(t,M16_GB_CRC);
  rc=observe(t,M16_GB_EVENT_RUN,0,0,0,0);if(rc)return fail(t,rc);
  g_m16_graph_profile_enabled=0;t->report->runs=t->run+1u;if(++t->run==M16_GB_RUNS){t->report->done=t->report->owners_released=1;t->phase=M16_GB_DONE;t->report->phase=t->phase;active=0;return 0;}
  t->phase=M16_GB_START;return 1;
 }
 return fail(t,M16_GB_SEQUENCE);
}

int m16_graph_benchmark_step(m16_graph_benchmark *t){
 if(!t||t->magic!=MAGIC||t!=active)return step_base(t);
 if(t->run>=M16_GB_RUNS||!row(t)->instrumented||t->phase==M16_GB_START)return step_base(t);
 unsigned id=primary(t);if(id>=M16_GB_BINS)return fail(t,M16_GB_SEQUENCE);
 if(!t->boundary_valid||id!=t->boundary_bin){row(t)->boundary_reads++;boundary(t,id,tick(t));}
 unsigned selected=!--t->sample_countdown;
 if(selected){
  unsigned x=t->random_state;x^=x<<13;x^=x>>17;x^=x<<5;t->random_state=x;
  t->sample_countdown=128u+(x&255u);t->sample_active=1;g_m16_graph_profile_sample=1;sampling=t;t->sample_row=row(t);t->sample_row->sample_roots++;
  m16_graph_benchmark_sample_begin(63u);
 }
 int rc=step_base(t);
 if(selected){m16_graph_benchmark_sample_end();t->sample_active=0;g_m16_graph_profile_sample=0;sampling=0;}
 return rc;
}
