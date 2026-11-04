#include "mic16_shared_spectrum_task.h"
#include <string.h>
#define MAGIC 0x4d535354u
#define GUARD UINT64_C(0x48a2e7b39cf016d5)
enum {CTRL=0,RETURN=0x10,ARENA=0x18,ARENA_HIGH=0x1c,INPUT=0x24,
 WORK=0x2c,SAMPLES=0x34,MODE=0x3c,ACTIVE=0x44};
static int valid(const m16_shared_spectrum_task *t){return t && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static uint32_t rd(m16_shared_spectrum_task *t,unsigned offset){return t->owner->io.read(t->owner->io.context,offset);}
static void wr(m16_shared_spectrum_task *t,unsigned offset,uint32_t value){t->owner->io.write(t->owner->io.context,offset,value);}
static int idle(unsigned ctrl){return (ctrl&4u) && !(ctrl&0x81u);}
static int fail(m16_shared_spectrum_task *t,int error,unsigned isolated){
 if(!t->error)t->error=error;
 t->phase=isolated?M16_SS_QUARANTINE:M16_SS_SAFE_ERROR;
 if(isolated)t->owner->quarantined=1;
 return t->error;
}
static int clock_ok(m16_shared_spectrum_task *t){
 uint64_t now=t->owner->io.ticks(t->owner->io.context);
 if(now<t->last_tick){fail(t,M16_SS_CLOCK,1);return 0;}
 t->last_tick=now;return 1;
}
int m16_shared_spectrum_task_init(m16_shared_spectrum_task *t,m16_shared_runtime *owner){
 if(!t || !owner || t->magic || (uintptr_t)t%_Alignof(m16_shared_spectrum_task) || (uintptr_t)owner%64u ||
  !apart(t,sizeof(*t),owner,sizeof(*owner)) || !owner->initialized || owner->busy || owner->quarantined ||
  !owner->io.read || !owner->io.write || !owner->io.ticks || !owner->io.flush || !owner->io.invalidate ||
  !owner->io.barrier || !owner->io.timeout_ticks)return M16_SS_INVALID;
 if(!idle(owner->io.read(owner->io.context,CTRL)))return M16_SS_BUSY;
 *t=(m16_shared_spectrum_task){0};t->magic=MAGIC;t->owner=owner;t->last_tick=owner->io.ticks(owner->io.context);return 0;
}
int m16_shared_spectrum_task_available(const m16_shared_spectrum_task *t){
 return valid(t) && t->phase==M16_SS_IDLE && t->owner && t->owner->initialized && !t->owner->busy && !t->owner->quarantined;
}
const uint64_t *m16_shared_spectrum_task_output(const m16_shared_spectrum_task *t,uint64_t epoch,uint64_t job){
 return valid(t) && t->phase==M16_SS_COMPLETE && t->epoch==epoch && t->job==job && t->owner->busy && !t->owner->quarantined ?
  t->owner->words+M16_SHARED_WORKSPACE_WORD:0;
}
int m16_shared_spectrum_task_acquire(m16_shared_spectrum_task *t,uint64_t epoch,uint64_t job,unsigned active){
 m16_shared_runtime *w;
 if(!valid(t) || !epoch || !job || !active || active>16)return M16_SS_INVALID;
 if(t->phase!=M16_SS_IDLE)return M16_SS_BUSY;
 if(epoch<t->last_epoch || (epoch==t->last_epoch && job<=t->last_job))return M16_SS_SEQUENCE;
 w=t->owner;if(w->busy || w->quarantined || !w->initialized)return M16_SS_BUSY;
 t->last_control=rd(t,CTRL);if(!idle(t->last_control))return M16_SS_BUSY;
 if(!clock_ok(t))return t->error;
 w->busy=1;t->epoch=epoch;t->job=job;t->last_epoch=epoch;t->last_job=job;t->active=active;
 t->output_bytes=(active*807u*8u+63u)&~63u;t->tail_word=M16_SHARED_WORKSPACE_WORD+t->output_bytes/8u;
 t->cursor=t->region=t->stop_requested=t->seen_done=t->started=0;t->error=t->kernel_return=0;
 t->steps=0;t->phase=M16_SS_CPU;return 0;
}
uint64_t *m16_shared_spectrum_task_arena(m16_shared_spectrum_task *t){
 return valid(t) && t->phase==M16_SS_CPU?t->owner->words:0;
}
int m16_shared_spectrum_task_submit(m16_shared_spectrum_task *t){
 if(!valid(t))return M16_SS_INVALID;
 if(t->phase!=M16_SS_CPU)return M16_SS_BUSY;
 if(!clock_ok(t))return t->error;
 t->cursor=0;t->phase=M16_SS_VALIDATE_INPUT;return 0;
}
/* 所有缓存区间64字节对齐；输出尾保护不与最后一条数据缓存线共用。 */
static void cache_step(m16_shared_spectrum_task *t,unsigned invalidate){
 m16_shared_runtime *w=t->owner;void *p;unsigned bytes,n;
 if(!invalidate){
  switch(t->region){
   case 0:p=w->words;bytes=t->active*8192u;break;
   case 1:p=w->words+M16_SHARED_WORKSPACE_WORD;bytes=t->output_bytes;break;
   case 2:p=w->words+t->tail_word;bytes=64;break;
   case 3:p=w->before;bytes=64;break;
   default:p=w->after;bytes=64;break;
  }
 }else{
  switch(t->region){
   case 0:p=w->words+M16_SHARED_WORKSPACE_WORD;bytes=t->output_bytes;break;
   case 1:p=w->words+t->tail_word;bytes=64;break;
   case 2:p=w->before;bytes=64;break;
   default:p=w->after;bytes=64;break;
  }
 }
 n=bytes-t->cursor;if(n>1024u)n=1024u;
 if(n>t->max_cache_bytes)t->max_cache_bytes=n;
 if(invalidate)w->io.invalidate(w->io.context,(unsigned char *)p+t->cursor,n);
 else w->io.flush(w->io.context,(unsigned char *)p+t->cursor,n);
 t->cursor+=n;
 if(t->cursor==bytes){
  t->cursor=0;t->region++;
  if(t->region==(invalidate?4u:5u)){
   w->io.barrier(w->io.context);t->region=0;t->phase=invalidate?M16_SS_CHECK:M16_SS_START;
  }
 }
}
int m16_shared_spectrum_task_poll(m16_shared_spectrum_task *t){
 unsigned i,end,ctrl;m16_shared_runtime *w;
 if(!valid(t))return M16_SS_INVALID;
 if(t->phase==M16_SS_IDLE)return M16_SS_INVALID;
 if(t->phase==M16_SS_QUARANTINE || t->phase==M16_SS_SAFE_ERROR)return t->error;
 if(t->phase==M16_SS_COMPLETE)return 0;
 if(t->phase==M16_SS_STOPPED)return 2;
 w=t->owner;t->steps++;
 if(!clock_ok(t))return t->error;
 switch(t->phase){
 case M16_SS_CPU:return 1;
 case M16_SS_VALIDATE_INPUT:
  end=t->cursor+64u;if(end>t->active*2048u)end=t->active*2048u;
  for(i=t->cursor;i<end;i++){
   uint64_t bits=w->words[i/2u];uint32_t v=(uint32_t)(i&1u?bits>>32:bits);
   if((v&0x7f800000u)==0x7f800000u || (v&0x7fffffffu)>0x40000000u)return fail(t,M16_SS_INPUT,0);
  }
  t->cursor=end;if(end==t->active*2048u){t->cursor=0;t->phase=M16_SS_GUARDS;}return 1;
 case M16_SS_GUARDS:
  for(i=0;i<8;i++)w->before[i]=w->after[i]=w->words[t->tail_word+i]=GUARD;
  t->cursor=t->region=0;t->phase=M16_SS_FLUSH;return 1;
 case M16_SS_FLUSH:cache_step(t,0);return 1;
 case M16_SS_START:
  ctrl=rd(t,CTRL);t->last_control=ctrl;
  if(!idle(ctrl))return fail(t,M16_SS_DEVICE_STATE,1);
  wr(t,4,0);wr(t,8,0);wr(t,ARENA,w->io.arena_address);wr(t,ARENA_HIGH,0);
  wr(t,INPUT,0);wr(t,WORK,M16_SHARED_WORKSPACE_WORD);wr(t,SAMPLES,2048);wr(t,MODE,4);wr(t,ACTIVE,t->active);
  w->io.barrier(w->io.context);t->device_tick=w->io.ticks(w->io.context);
  if(t->device_tick<t->last_tick)return fail(t,M16_SS_CLOCK,1);
  t->last_tick=t->device_tick;t->started=1;wr(t,CTRL,1);t->phase=M16_SS_DEVICE;return 1;
 case M16_SS_DEVICE:
  ctrl=rd(t,CTRL);t->last_control=ctrl;
  if(ctrl&0x80u)return fail(t,M16_SS_DEVICE_STATE,1);
  if(t->last_tick-t->device_tick>=w->io.timeout_ticks)return fail(t,M16_SS_TIMEOUT,1);
  if(ctrl&2u)t->seen_done=1;
  if(t->seen_done && idle(ctrl)){
   t->kernel_return=(int32_t)rd(t,RETURN);w->io.barrier(w->io.context);
   t->cursor=t->region=0;t->phase=M16_SS_INVALIDATE;return 1;
  }
  return 1;
 case M16_SS_INVALIDATE:cache_step(t,1);return 1;
 case M16_SS_CHECK:
  if(t->cursor==0){
   for(i=0;i<8;i++)if(w->before[i]!=GUARD || w->after[i]!=GUARD || w->words[t->tail_word+i]!=GUARD)return fail(t,M16_SS_GUARD,1);
   if(t->kernel_return)return fail(t,M16_SS_RETURN,0);
  }
  end=t->cursor+64u;if(end>t->active*807u)end=t->active*807u;
  for(i=t->cursor;i<end;i++){
   uint64_t bits=w->words[M16_SHARED_WORKSPACE_WORD+i];
   if(((uint32_t)bits&0x7f800000u)==0x7f800000u || ((uint32_t)(bits>>32)&0x7f800000u)==0x7f800000u)return fail(t,M16_SS_OUTPUT,0);
  }
  t->cursor=end;
  if(end==t->active*807u){t->phase=t->stop_requested?M16_SS_STOPPED:M16_SS_COMPLETE;return t->stop_requested?2:0;}
  return 1;
 default:return fail(t,M16_SS_INVALID,1);
 }
}
int m16_shared_spectrum_task_stop(m16_shared_spectrum_task *t){
 if(!valid(t))return M16_SS_INVALID;
 if(t->phase==M16_SS_IDLE)return 0;
 if(t->phase==M16_SS_QUARANTINE || t->phase==M16_SS_SAFE_ERROR)return t->error;
 t->stop_requested=1;
 if(!t->started || t->phase==M16_SS_COMPLETE){
  if(!idle(rd(t,CTRL)))return fail(t,M16_SS_DEVICE_STATE,1);
  t->phase=M16_SS_STOPPED;return 2;
 }
 return t->phase==M16_SS_STOPPED?2:1;
}
int m16_shared_spectrum_task_releasable(const m16_shared_spectrum_task *t){
 return valid(t) && (t->phase==M16_SS_COMPLETE || t->phase==M16_SS_STOPPED || t->phase==M16_SS_SAFE_ERROR);
}
int m16_shared_spectrum_task_retire(m16_shared_spectrum_task *t,uint64_t epoch,uint64_t job){
 if(!valid(t) || epoch!=t->epoch || job!=t->job)return M16_SS_SEQUENCE;
 if(!m16_shared_spectrum_task_releasable(t) || t->owner->quarantined)return M16_SS_BUSY;
 if(!idle(rd(t,CTRL)))return fail(t,M16_SS_DEVICE_STATE,1);
 t->owner->busy=0;t->phase=M16_SS_IDLE;return 0;
}
