#include "mic16_graph_benchmark.h"
#include <string.h>
#define MAGIC 0x47423631u
static m16_graph_benchmark *active;
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
static uint64_t enter(m16_graph_benchmark *t){return row(t)->instrumented?tick(t):0;}
static void leave(m16_graph_benchmark *t,unsigned id,uint64_t start){if(row(t)->instrumented)add(t,id,tick(t)-start);}
void m16_graph_benchmark_external(unsigned id,uint64_t duration){if(active&&active->phase!=M16_GB_DONE&&active->phase!=M16_GB_FAILED)add(active,id,duration);}
void m16_graph_benchmark_projection_complete(const m16_projection_runtime *r){
 m16_graph_benchmark *t=active;unsigned mode;
 if(!t||r!=t->pipeline->detection->runtime||t->run>=M16_GB_RUNS||r->phase!=M16_PR_COMPLETE)return;
 mode=r->plan[0].mode;if(mode>=6u)mode=0;
 for(unsigned i=1;i<r->jobs;i++)if(r->plan[i].mode!=mode)mode=0;
 m16_graph_benchmark_mode *m=&row(t)->mode[mode];
 m->transactions++;m->jobs+=r->jobs;m->prepare+=r->report.prepare_ticks;
 m->observed_device+=r->report.device_ticks;m->collect+=r->report.collect_ticks;
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
 report->magic=MAGIC;report->version=1;report->timer_hz=io->timer_hz;
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
static unsigned pipeline_bin(const m16_graph_benchmark *t){
 const m16_audio_pipeline_task *p=t->pipeline;
 if(p->phase==M16_AP_DETECT)return 8u+(p->detection->phase<13u?p->detection->phase:12u);
 if(p->source_audio->phase==M16_SA_PREDICT)return 23u;
 if(p->source_audio->phase==M16_SA_REBUILD||p->source_audio->phase==M16_SA_FINISH)return 24u;
 return 22u;
}
int m16_graph_benchmark_step(m16_graph_benchmark *t){
 m16_graph_benchmark_run *v;uint64_t a;int rc;unsigned bin;
 if(!t||t->magic!=MAGIC)return M16_GB_INVALID;
 if(t->phase==M16_GB_DONE)return 0;
 if(t->phase==M16_GB_FAILED)return t->error;
 if(t!=active)return M16_GB_INVALID;
 t->report->phase=t->phase;t->report->current=t->run;v=row(t);v->steps++;
 a=enter(t);rc=m16_shared_compute_graph_service(t->backend);leave(t,0,a);
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
  v->instrumented=t->run==2u||t->run==3u;v->crc=UINT32_MAX;v->started=tick(t);
  t->FFT_before=t->backend->fft.task.transactions;t->FIR_before=t->backend->fir.transactions;t->fallback_before=t->backend->fft.task.range_fallbacks;
  t->frame=t->feeding=0;t->phase=M16_GB_FRONT;return 1;
 }
 if(t->phase==M16_GB_FRONT){
  if(!t->feeding){
   rc=m16_front_ring_begin(t->ring,t->input+t->frame*32768u,32768u,t->run+1u,t->frame,t->frame*2048u,tick(t));if(rc)return fail(t,rc);
   t->feeding=1;
  }
  bin=t->ring->phase==M16_FR_COPY?1u:t->ring->phase==M16_FR_FRONT?2u:3u;
  a=enter(t);rc=m16_front_ring_step(t->ring,tick(t));leave(t,bin,a);if(rc<0)return fail(t,rc);
  if(!rc){t->feeding=0;v->input_frames++;if(++t->frame==20u){v->front_ticks=tick(t)-v->started;
   rc=m16_front_context_begin(t->front_context);if(rc)return fail(t,rc);t->phase=M16_GB_SNAPSHOT;}}
  return 1;
 }
 if(t->phase==M16_GB_SNAPSHOT){
  a=enter(t);rc=m16_front_context_step(t->front_context);leave(t,4,a);if(rc<0)return fail(t,rc);
  if(!rc){v->snapshot_ticks=tick(t)-v->started-v->front_ticks;
   rc=m16_audio_pipeline_begin_prepared(t->pipeline,t->front_context,t->reference);if(rc)return fail(t,rc);t->phase=M16_GB_PIPELINE;}
  return 1;
 }
 if(t->phase==M16_GB_PIPELINE||t->phase==M16_GB_FLUSH){
  a=enter(t);rc=consume(t);leave(t,5,a);if(rc)return fail(t,rc);
  bin=pipeline_bin(t);a=enter(t);rc=m16_audio_pipeline_step(t->pipeline);leave(t,bin,a);
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
  v->FFT_jobs=t->backend->fft.task.transactions-t->FFT_before;v->FIR_jobs=t->backend->fir.transactions-t->FIR_before;
  v->range_fallbacks=t->backend->fft.task.range_fallbacks-t->fallback_before;
  if(v->input_frames!=20u||v->locations!=1u||!v->FFT_jobs||!v->FIR_jobs)return fail(t,M16_GB_SEQUENCE);
  if((t->io.expected_crc&&v->crc!=t->io.expected_crc)||(t->run&&v->crc!=t->report->run[0].crc))return fail(t,M16_GB_CRC);
  rc=observe(t,M16_GB_EVENT_RUN,0,0,0,0);if(rc)return fail(t,rc);
  t->report->runs=t->run+1u;if(++t->run==M16_GB_RUNS){t->report->done=t->report->owners_released=1;t->phase=M16_GB_DONE;t->report->phase=t->phase;active=0;return 0;}
  t->phase=M16_GB_START;return 1;
 }
 return fail(t,M16_GB_SEQUENCE);
}
