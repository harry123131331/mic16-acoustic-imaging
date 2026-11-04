#include "mic16_shared_spectrum_runtime.h"
#include "mic16_cooperate.h"
#define GUARD UINT64_C(0x48a2e7b39cf016d5)
enum {CTRL=0,RETURN=0x10,ARENA_LOW=0x18,ARENA_HIGH=0x1c,INPUT_WORD=0x24,
 WORK_WORD=0x2c,FRAME_SAMPLES=0x34,MODE=0x3c,ACTIVE=0x44,CACHE_CHUNK=1024};
static int overlap(const void *a,unsigned an,const void *b,unsigned bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
int m16_shared_runtime_available(const m16_shared_runtime *w){return w && w->initialized && !w->quarantined && !w->busy;}
int m16_shared_runtime_init(m16_shared_runtime *w,const m16_scanner_io *io){
 unsigned i,ctrl,old_mode,old_active,mode,active;
 if(!w || !io || ((uintptr_t)w&63u) || !io->read || !io->write || !io->ticks || !io->flush || !io->invalidate || !io->barrier ||
 !io->timeout_ticks || io->arena_address<0x100040u || (io->arena_address&63u) ||
 io->arena_address>0x10000000u-(M16_SHARED_ARENA_WORDS*8u+64u) || overlap(w,sizeof(*w),io,sizeof(*io)) || w->quarantined || w->busy)return M16_SHARED_INVALID;
 w->initialized=0;
 ctrl=io->read(io->context,CTRL);if(!(ctrl&4u) || (ctrl&0x81u))return M16_SHARED_BUSY;
 old_mode=io->read(io->context,MODE);old_active=io->read(io->context,ACTIVE);
 io->write(io->context,MODE,4);io->write(io->context,ACTIVE,16);io->barrier(io->context);
 mode=io->read(io->context,MODE);active=io->read(io->context,ACTIVE);
 io->write(io->context,MODE,old_mode);io->write(io->context,ACTIVE,old_active);io->barrier(io->context);
 if(mode!=4 || active!=16)return M16_SHARED_ABI;
 w->io=*io;
 for(i=0;i<8;i++)w->before[i]=w->after[i]=GUARD;
 w->initialized=1;return 0;
}
static void cache(m16_shared_runtime *w,void (*f)(void *,const void *,unsigned),const void *p,unsigned bytes){
 unsigned offset=0;const unsigned char *v=p;
 for(;offset<bytes;){unsigned n=bytes-offset;if(n>CACHE_CHUNK)n=CACHE_CHUNK;
  f(w->io.context,v+offset,n);offset+=n;M16_COOPERATE();}
}
static int isolate(m16_shared_runtime *w,m16_shared_report *r,int error){w->quarantined=1;r->quarantined=1;return error;}
int m16_shared_runtime_execute(m16_shared_runtime *w,unsigned active,m16_shared_report *r){
 unsigned i,polls,out_bytes,tail_word;uint64_t start,mark,now,device_start;int rc;m16_scanner_io *io;
 if(!m16_shared_runtime_available(w) || !r || !active || active>16 || overlap(r,sizeof(*r),w,sizeof(*w)))return M16_SHARED_INVALID;
 *r=(m16_shared_report){0};r->active_channels=active;io=&w->io;start=io->ticks(io->context);
 r->last_control=io->read(io->context,CTRL);
 if(!(r->last_control&4u) || (r->last_control&0x81u))return M16_SHARED_BUSY;
 w->busy=1;
 out_bytes=(active*807u*8u+31u)&~31u;tail_word=16384u+out_bytes/8u;
 for(i=0;i<8;i++)w->words[tail_word+i]=GUARD;
 /* 先清洁输出及尾保护线，避免InvalidateRange在非整线边界写回旧脏数据。
  * 每次cache调用最多1KiB，避免整块维护长期屏蔽采集IRQ；实际代价仍需实测。 */
 cache(w,io->flush,w->words,active*2048u*4u);
 cache(w,io->flush,w->words+16384,out_bytes);cache(w,io->flush,w->words+tail_word,64);
 cache(w,io->flush,w->before,64);cache(w,io->flush,w->after,64);io->barrier(io->context);
 now=io->ticks(io->context);if(now<start)return isolate(w,r,M16_SHARED_CLOCK);r->cache_ticks=now-start;
 io->write(io->context,4,0);io->write(io->context,8,0);
 io->write(io->context,ARENA_LOW,io->arena_address);io->write(io->context,ARENA_HIGH,0);
 io->write(io->context,INPUT_WORD,0);io->write(io->context,WORK_WORD,16384);
 io->write(io->context,FRAME_SAMPLES,2048);io->write(io->context,MODE,4);io->write(io->context,ACTIVE,active);io->barrier(io->context);
 device_start=io->ticks(io->context);if(device_start<now)return isolate(w,r,M16_SHARED_CLOCK);
 io->write(io->context,CTRL,1);
 mark=device_start;
 for(polls=0;;polls++){
  if((polls&255u)==0)M16_COOPERATE();
  r->last_control=io->read(io->context,CTRL);now=io->ticks(io->context);
  if(now<mark)return isolate(w,r,M16_SHARED_CLOCK);
  mark=now;
  if(r->last_control&2u)break;
  if(now-device_start>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_SHARED_TIMEOUT);
 }
 mark=now;
 for(polls=0;!(r->last_control&4u);polls++){
  if((polls&255u)==0)M16_COOPERATE();
  r->last_control=io->read(io->context,CTRL);now=io->ticks(io->context);
  if(now<mark)return isolate(w,r,M16_SHARED_CLOCK);
  if(now-device_start>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_SHARED_TIMEOUT);
  mark=now;
 }
 r->device_ticks=now-device_start;
 r->kernel_return=(int32_t)io->read(io->context,RETURN);io->barrier(io->context);
 cache(w,io->invalidate,w->words+16384,out_bytes);cache(w,io->invalidate,w->words+tail_word,64);
 cache(w,io->invalidate,w->before,64);cache(w,io->invalidate,w->after,64);io->barrier(io->context);
 for(i=0;i<8;i++)if(w->before[i]!=GUARD || w->after[i]!=GUARD || w->words[tail_word+i]!=GUARD)return isolate(w,r,M16_SHARED_GUARD);
 rc=r->kernel_return;if(rc)return isolate(w,r,M16_SHARED_RETURN);
 mark=io->ticks(io->context);if(mark<now)return isolate(w,r,M16_SHARED_CLOCK);
 r->invalidate_ticks=mark-now;r->total_ticks=mark-start;r->completed_batches=1;w->busy=0;return 0;
}
