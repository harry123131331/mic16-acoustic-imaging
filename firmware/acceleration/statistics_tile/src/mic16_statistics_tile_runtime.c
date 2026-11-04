#include "mic16_statistics_tile_runtime.h"
#include "mic16_cooperate.h"
#define GUARD UINT64_C(0x48a2e7b39cf016d5)
/* 偏移逐项核对125MHz导出IP的xm16_statistics_tile_hw.h，不沿用旧WDAS寄存器。 */
enum {CTRL=0,RETURN=0x10,ARENA_LOW=0x18,ARENA_HIGH=0x1c,WORDS=0x24,
      OBS=0x2c,COEF=0x34,OUTPUT=0x3c,FRAMES=0x44,BINS=0x4c,CANDIDATES=0x54};
static int overlaps(const void *a,unsigned an,const void *b,unsigned bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return x<=y?y-x<an:x-y<bn;
}
int m16_tile_runtime_available(void *context){
 m16_tile_runtime *w=context;return w && w->initialized && !w->quarantined && !w->busy;
}
static int isolate(m16_tile_runtime *w,int rc){
 w->quarantined=1;w->report.quarantined=1;return rc;
}
int m16_tile_runtime_init(m16_tile_runtime *w,const m16_scanner_io *io,unsigned abi){
 unsigned ctrl,old_b,old_c,b,c,i;
 if(!w || !io || ((uintptr_t)w&63u) || overlaps(w,sizeof(*w),io,sizeof(*io)) ||
    !io->read || !io->write || !io->flush || !io->invalidate || !io->ticks || !io->barrier ||
    !io->timeout_ticks || io->arena_address<0x100040u || (io->arena_address&63u) ||
    io->arena_address>0x10000000u-(M16_TILE_WORDS*8u+64u) || w->busy || w->quarantined)return M16_TILE_INVALID;
 if(abi!=M16_TILE_BUILD_ABI)return M16_TILE_ABI;
 w->initialized=0;ctrl=io->read(io->context,CTRL);
 if(!(ctrl&4u) || (ctrl&0x81u))return M16_TILE_BUSY;
 old_b=io->read(io->context,BINS);old_c=io->read(io->context,CANDIDATES);
 io->write(io->context,BINS,7);io->write(io->context,CANDIDATES,3);io->barrier(io->context);
 b=io->read(io->context,BINS);c=io->read(io->context,CANDIDATES);
 io->write(io->context,BINS,old_b);io->write(io->context,CANDIDATES,old_c);io->barrier(io->context);
 if(b!=7 || c!=3)return M16_TILE_ABI;
 w->io=*io;w->report=(m16_tile_runtime_report){0};w->observations_clean=0;
 for(i=0;i<8;i++)w->before[i]=w->after[i]=GUARD;
 w->initialized=1;return 0;
}
static void cache(m16_tile_runtime *w,void (*fn)(void *,const void *,unsigned),const void *p,unsigned n){
 const unsigned char *bytes=p;unsigned offset=0;
 while(offset<n){unsigned size=n-offset;if(size>1024)size=1024;
  fn(w->io.context,bytes+offset,size);offset+=size;M16_COOPERATE();}
}
int m16_tile_runtime_submit(m16_tile_runtime *w,unsigned frames,unsigned bins,unsigned candidates,unsigned dirty){
 m16_scanner_io *io;unsigned i;uint64_t now;
 if(!m16_tile_runtime_available(w) || !frames || frames>4 || !bins || bins>8 ||
    !candidates || candidates>4 || dirty>1)return M16_TILE_INVALID;
 io=&w->io;w->report=(m16_tile_runtime_report){0};
 w->report.last_control=io->read(io->context,CTRL);
 if(!(w->report.last_control&4u) || (w->report.last_control&0x81u))return M16_TILE_BUSY;
 w->busy=1;w->done_seen=0;w->frames=frames;w->bins=bins;w->candidates=candidates;w->started=io->ticks(io->context);
 for(i=2048;i<M16_TILE_WORDS;i++)w->words[i]=GUARD;
 /* 输出及保护区先清洁；所有维护范围64字节对齐，单次最多1KiB。
  * 同一观测块跨候选组复用，只有第一次或观测变化时清洁8KiB观测。 */
 if(dirty || !w->observations_clean)cache(w,io->flush,w->words,8192);
 cache(w,io->flush,w->words+1024,8192);cache(w,io->flush,w->words+2048,2048);
 cache(w,io->flush,w->before,64);cache(w,io->flush,w->after,64);io->barrier(io->context);
 now=io->ticks(io->context);if(now<w->started)return isolate(w,M16_TILE_CLOCK);
 w->report.cache_ticks=now-w->started;
 io->write(io->context,4,0);io->write(io->context,8,0);
 io->write(io->context,ARENA_LOW,io->arena_address);io->write(io->context,ARENA_HIGH,0);
 io->write(io->context,WORDS,M16_TILE_WORDS);io->write(io->context,OBS,0);
 io->write(io->context,COEF,1024);io->write(io->context,OUTPUT,2048);
 io->write(io->context,FRAMES,frames);io->write(io->context,BINS,bins);io->write(io->context,CANDIDATES,candidates);
 io->barrier(io->context);now=io->ticks(io->context);
 if(now<w->started+w->report.cache_ticks)return isolate(w,M16_TILE_CLOCK);
 w->submitted=w->last_tick=now;io->write(io->context,CTRL,1);return 0;
}
int m16_tile_runtime_poll(m16_tile_runtime *w){
 m16_scanner_io *io;unsigned i;uint64_t now,done;
 if(!w || !w->initialized || !w->busy || w->quarantined)return M16_TILE_INVALID;
 io=&w->io;w->report.last_control=io->read(io->context,CTRL);now=io->ticks(io->context);
 if(now<w->last_tick)return isolate(w,M16_TILE_CLOCK);
 w->last_tick=now;
 /* ap_idle必须也为1，避免done置位但AXI仍可能在写入时归还所有权。 */
 if((w->report.last_control&6u)!=6u){
  /* ap_done是读清零；idle若稍晚出现，保留已看见的done状态。 */
  if(w->report.last_control&2u)w->done_seen=1;
  if(!(w->done_seen && (w->report.last_control&4u))){
   if(now-w->submitted>=io->timeout_ticks)return isolate(w,M16_TILE_TIMEOUT);
   return 1;
  }
 }
 done=now;w->report.device_ticks=done-w->submitted;
 w->report.kernel_return=(int32_t)io->read(io->context,RETURN);io->barrier(io->context);
 cache(w,io->invalidate,w->words+2048,2048);cache(w,io->invalidate,w->before,64);
 cache(w,io->invalidate,w->after,64);io->barrier(io->context);
 for(i=0;i<8;i++)if(w->before[i]!=GUARD || w->after[i]!=GUARD)return isolate(w,M16_TILE_GUARD);
 if(w->report.kernel_return)return isolate(w,M16_TILE_RETURN);
 for(i=0;i<256;i++){
  unsigned ctx=i>>1,c=ctx&3u,b=(ctx>>2)&7u,t=ctx>>5;
  uint64_t v=w->words[2048+i];
  if(t>=w->frames || b>=w->bins || c>=w->candidates){if(v!=GUARD)return isolate(w,M16_TILE_GUARD);}
  else{union{uint32_t u;float f;}re,im;re.u=(uint32_t)v;im.u=(uint32_t)(v>>32);
   if(!(re.f>=-33.f && re.f<=33.f && im.f>=-33.f && im.f<=33.f))return isolate(w,M16_TILE_GUARD);}
 }
 now=io->ticks(io->context);if(now<done)return isolate(w,M16_TILE_CLOCK);
 w->report.invalidate_ticks=now-done;w->report.total_ticks=now-w->started;
 w->report.batches=1;w->observations_clean=1;w->busy=0;return 0;
}
int m16_tile_runtime_execute(void *context,unsigned frames,unsigned bins,unsigned candidates,unsigned dirty){
 m16_tile_runtime *w=context;unsigned polls;int rc=m16_tile_runtime_submit(w,frames,bins,candidates,dirty);
 if(rc)return rc;
 for(polls=0;polls<100000000u;polls++){
  rc=m16_tile_runtime_poll(w);if(rc!=1)return rc;
  if((polls&255u)==0)M16_COOPERATE();
 }
 return isolate(w,M16_TILE_TIMEOUT);
}
int m16_tile_runtime_bind(m16_tile_runtime *w){
 m16_tile_batch_backend b={w,w?w->words:0,m16_tile_runtime_available,m16_tile_runtime_execute};
 return m16_tile_batch_bind(&b);
}
