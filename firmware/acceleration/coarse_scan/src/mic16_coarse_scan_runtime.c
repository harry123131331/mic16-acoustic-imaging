#include "mic16_coarse_scan_runtime.h"
#include <string.h>
#include <math.h>
#include <stddef.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d435352u
#define GUARD UINT64_C(0x73b5ac19e0624df8)
static int valid(const m16_coarse_scan_runtime *r){return r&&r->magic==MAGIC;}
static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int float_environment(void){
#if defined(__arm__)
 uint32_t fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr));return !(fpscr&((3u<<22)|(1u<<24)));
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&((3u<<13)|(1u<<15)|(1u<<6)))return 0;
#endif
 return 1;
#endif
}
static int isolate(m16_coarse_scan_runtime *r,int rc){r->report.error=(unsigned)-rc;r->phase=M16_CS_QUARANTINE;return rc;}
static int safe_error(m16_coarse_scan_runtime *r,int rc){
 r->report.error=(unsigned)-rc;r->phase=r->initialized?M16_CS_SAFE_ERROR:M16_CS_QUARANTINE;return rc;
}
static int clock_read(m16_coarse_scan_runtime *r,uint64_t *now){
 *now=r->io.ticks(r->io.context);if(*now<r->last_tick)return isolate(r,M16_SCAN_CLOCK);r->last_tick=*now;return 0;
}
static int idle(m16_coarse_scan_runtime *r){
 unsigned value=r->io.read(r->io.context,0);r->report.last_control=value;return (value&4u)&&!(value&0x81u);
}
static int guards(const m16_coarse_scan_runtime *r){
 unsigned i;for(i=0;i<8;i++)if(r->arena.before[i]!=GUARD||r->arena.after[i]!=GUARD)return 0;
 for(i=M16_SCAN_WORDS;i<M16_SCAN_PADDED_WORDS;i++)if(r->arena.words[i]!=GUARD)return 0;
 return 1;
}
int m16_coarse_scan_init(m16_coarse_scan_runtime *r,const m16_scanner_io *io){
 unsigned i;uint32_t control;
 if(!r||!io||((uintptr_t)r&63u)||r->magic||!io->read||!io->write||!io->ticks||!io->flush||!io->invalidate||!io->barrier||
    !io->timeout_ticks||io->arena_address<0x100040u||(io->arena_address&63u)||
    io->arena_address>0x10000000u-(M16_SCAN_PADDED_WORDS*8u+64u)||overlap(r,sizeof(*r),io,sizeof(*io)))return M16_SCAN_INVALID;
 if(!float_environment())return M16_SCAN_INVALID;
 control=io->read(io->context,0);if(!(control&4u)||(control&0x81u))return M16_SCAN_BUSY;
 /* 不批量清空大arena：输入/输出在每帧显式填写，启动前只准备固定相位表。 */
 memset((unsigned char *)r+offsetof(m16_coarse_scan_runtime,io),0,sizeof(*r)-offsetof(m16_coarse_scan_runtime,io));
 r->io=*io;r->magic=MAGIC;r->phase=M16_CS_TABLES;
 r->last_tick=io->ticks(io->context);
 for(i=0;i<8;i++)r->arena.before[i]=r->arena.after[i]=GUARD;
 for(i=M16_SCAN_WORDS;i<M16_SCAN_PADDED_WORDS;i++)r->arena.words[i]=GUARD;
 return 0;
}
int m16_coarse_scan_available(const m16_coarse_scan_runtime *r){return valid(r)&&r->initialized&&r->phase==M16_CS_IDLE;}
static int cache_step(m16_coarse_scan_runtime *r,unsigned boot,unsigned collecting){
 const void *address;unsigned bytes,n;
 if(r->cache_stage>=((boot||collecting)?3u:4u))return 0;
 if(boot){
  if(r->cache_stage==0){address=r->arena.words+M16_SCAN_PHASE_OFFSET;bytes=(M16_SCAN_OUTPUT_OFFSET-M16_SCAN_PHASE_OFFSET)*8u;}
  else if(r->cache_stage==1){address=r->arena.before;bytes=64;}
  else{address=r->arena.after;bytes=64;}
 }else if(collecting){
  if(r->cache_stage==0){address=r->arena.words+M16_SCAN_OUTPUT_OFFSET;bytes=(M16_SCAN_PADDED_WORDS-M16_SCAN_OUTPUT_OFFSET)*8u;}
  else if(r->cache_stage==1){address=r->arena.before;bytes=64;}
  else{address=r->arena.after;bytes=64;}
 }else{
  if(r->cache_stage==0){address=r->arena.words;bytes=M16_SCAN_OBS_WORDS*8u;}
  else if(r->cache_stage==1){address=r->arena.words+M16_SCAN_OUTPUT_OFFSET;bytes=(M16_SCAN_PADDED_WORDS-M16_SCAN_OUTPUT_OFFSET)*8u;}
  else if(r->cache_stage==2){address=r->arena.before;bytes=64;}
  else{address=r->arena.after;bytes=64;}
 }
 n=bytes-r->cache_cursor;if(n>1024u)n=1024u;
 address=(const unsigned char *)address+r->cache_cursor;
 if(collecting){r->io.invalidate(r->io.context,address,n);r->report.invalidate_calls++;}
 else{r->io.flush(r->io.context,address,n);r->report.flush_calls++;}
 r->cache_cursor+=n;if(r->cache_cursor==bytes){r->cache_stage++;r->cache_cursor=0;}return 1;
}
int m16_coarse_scan_submit(m16_coarse_scan_runtime *r,const float *x,unsigned nx,float *maps,unsigned nm,uint64_t epoch,uint64_t frame){
 uint64_t now;
 if(!m16_coarse_scan_available(r))return M16_SCAN_BUSY;
 if(!x||!maps||((uintptr_t)x%_Alignof(float))||((uintptr_t)maps%_Alignof(float))||nx<807u*32u*2u||nm<24u*325u||!epoch||
    epoch<r->last_epoch||(epoch==r->last_epoch&&frame<=r->last_frame)||(epoch>r->last_epoch&&frame!=0)||
    overlap(x,807u*32u*2u*4u,r,sizeof(*r))||overlap(maps,24u*325u*4u,r,sizeof(*r))||
    overlap(x,807u*32u*2u*4u,maps,24u*325u*4u))return M16_SCAN_INVALID;
 if(!float_environment())return M16_SCAN_INVALID;
 if(!idle(r))return M16_SCAN_BUSY;
 if(!guards(r))return isolate(r,M16_SCAN_GUARD);
 if(clock_read(r,&now))return M16_SCAN_CLOCK;
 r->input=x;r->maps=maps;r->epoch=epoch;r->frame=frame;r->last_epoch=epoch;r->last_frame=frame;
 r->begin_tick=now;r->cursor=r->cache_stage=r->cache_cursor=r->done_seen=r->started=r->stop_requested=0;
 r->report=(m16_coarse_scan_report){0};r->phase=M16_CS_PACK;return 0;
}
int m16_coarse_scan_poll(m16_coarse_scan_runtime *r){
 unsigned end,i,control;uint64_t now;
 if(!valid(r)||r->phase==M16_CS_IDLE)return M16_SCAN_INVALID;
 if(r->phase==M16_CS_COMPLETE)return 0;
 if(r->phase==M16_CS_STOPPED)return 2;
 if(r->phase==M16_CS_SAFE_ERROR||r->phase==M16_CS_QUARANTINE)return -(int)r->report.error;
 if(clock_read(r,&now))return M16_SCAN_CLOCK;
 if(r->phase==M16_CS_TABLES){
  const float *lags=mic16_spatial_lags();if(!float_environment())return safe_error(r,M16_SCAN_INVALID);
  end=r->cursor+16u;if(end>325u*32u)end=325u*32u;
  for(i=r->cursor;i<end;i++){
   int32_t a,b;double step=6.283185307179586476925286766559*(double)lags[i]/4096.;
   if(m16_scanner_quantize((float)cos(32.*step),&a)||m16_scanner_quantize((float)sin(32.*step),&b))return safe_error(r,M16_SCAN_INVALID);
   r->arena.words[M16_SCAN_PHASE_OFFSET+i]=m16_scanner_pack(a,b);
   if(m16_scanner_quantize((float)cos(step),&a)||m16_scanner_quantize((float)sin(step),&b))return safe_error(r,M16_SCAN_INVALID);
   r->arena.words[M16_SCAN_STEP_OFFSET+i]=m16_scanner_pack(a,b);
  }
  r->cursor=end;if(end==325u*32u){r->phase=M16_CS_TABLE_FLUSH;r->cache_stage=r->cache_cursor=0;}return 1;
 }
 if(r->phase==M16_CS_TABLE_FLUSH){
  if(cache_step(r,1,0))return 1;
  r->io.barrier(r->io.context);r->initialized=1;r->phase=M16_CS_IDLE;r->report=(m16_coarse_scan_report){0};return 0;
 }
 if(r->phase==M16_CS_PACK){
  end=r->cursor+64u;if(end>M16_SCAN_OBS_WORDS)end=M16_SCAN_OBS_WORDS;
  for(i=r->cursor;i<end;i++){
   int32_t re,im;
   if(m16_scanner_quantize(r->input[2u*i],&re)||m16_scanner_quantize(r->input[2u*i+1u],&im))return safe_error(r,M16_SCAN_INVALID);
   r->arena.words[i]=m16_scanner_pack(re,im);
  }
  r->cursor=end;if(end==M16_SCAN_OBS_WORDS){r->phase=M16_CS_FILL;r->cursor=M16_SCAN_OUTPUT_OFFSET;}return 1;
 }
 if(r->phase==M16_CS_FILL){
  end=r->cursor+64u;if(end>M16_SCAN_PADDED_WORDS)end=M16_SCAN_PADDED_WORDS;
  for(i=r->cursor;i<end;i++)r->arena.words[i]=GUARD;
  r->cursor=end;if(end==M16_SCAN_PADDED_WORDS){r->phase=M16_CS_FLUSH;r->cache_stage=r->cache_cursor=0;}return 1;
 }
 if(r->phase==M16_CS_FLUSH){if(cache_step(r,0,0))return 1;r->phase=M16_CS_START;return 1;}
 if(r->phase==M16_CS_START){
  if(!idle(r)){
   if(now-r->begin_tick>=r->io.timeout_ticks)return isolate(r,M16_SCAN_TIMEOUT);
   return 1;
  }
  r->io.barrier(r->io.context);r->io.write(r->io.context,4,0);r->io.write(r->io.context,8,0);
  r->io.write(r->io.context,0x10,r->io.arena_address);r->io.write(r->io.context,0x14,0);
  r->io.write(r->io.context,0x1c,0);r->io.write(r->io.context,0x24,M16_SCAN_PHASE_OFFSET);
  r->io.write(r->io.context,0x2c,M16_SCAN_STEP_OFFSET);r->io.write(r->io.context,0x34,M16_SCAN_OUTPUT_OFFSET);
  r->io.barrier(r->io.context);if(clock_read(r,&now))return M16_SCAN_CLOCK;
  r->submitted_tick=now;r->report.prepare_ticks=now-r->begin_tick;r->started=1;r->io.write(r->io.context,0,1);r->phase=M16_CS_DEVICE;return 1;
 }
 if(r->phase==M16_CS_DEVICE){
  control=r->io.read(r->io.context,0);r->report.last_control=control;if(control&2u)r->done_seen=1;
  if(!(r->done_seen&&(control&4u)&&!(control&0x81u))){
   if(now-r->submitted_tick>=r->io.timeout_ticks)return isolate(r,M16_SCAN_TIMEOUT);
   return 1;
  }
  r->report.device_ticks=now-r->submitted_tick;r->collect_tick=now;r->io.barrier(r->io.context);
  r->phase=M16_CS_INVALIDATE;r->cache_stage=r->cache_cursor=0;return 1;
 }
 if(r->phase==M16_CS_INVALIDATE){
  if(cache_step(r,0,1))return 1;
  r->io.barrier(r->io.context);if(!guards(r))return isolate(r,M16_SCAN_GUARD);
  if(r->stop_requested){r->phase=M16_CS_STOPPED;return 2;}
  r->phase=M16_CS_DECODE;r->cursor=0;return 1;
 }
 if(r->phase!=M16_CS_DECODE)return isolate(r,M16_SCAN_INVALID);
 if(r->stop_requested){r->phase=M16_CS_STOPPED;return 2;}
 if(!float_environment())return isolate(r,M16_SCAN_INVALID);
 end=r->cursor+64u;if(end>24u*325u)end=24u*325u;
 for(i=r->cursor;i<end;i++){
  uint64_t word=r->arena.words[M16_SCAN_OUTPUT_OFFSET+i/2u];int32_t raw=(int32_t)(uint32_t)(i%2u?word>>32:word);
  if(raw< -33554432||raw>33554431)return isolate(r,M16_SCAN_GUARD);
  r->maps[i]=(float)raw*0x1p-24f;
 }
 r->cursor=end;if(end==24u*325u){
  if(clock_read(r,&now))return M16_SCAN_CLOCK;
  r->report.collect_ticks=now-r->collect_tick;r->report.total_ticks=now-r->begin_tick;r->phase=M16_CS_COMPLETE;return 0;
 }
 return 1;
}
int m16_coarse_scan_retire(m16_coarse_scan_runtime *r){
 if(!valid(r)||!r->initialized||(r->phase!=M16_CS_COMPLETE&&r->phase!=M16_CS_STOPPED&&r->phase!=M16_CS_SAFE_ERROR))return M16_SCAN_BUSY;
 if(!idle(r))return isolate(r,M16_SCAN_BUSY);
 r->phase=M16_CS_IDLE;return 0;
}
int m16_coarse_scan_cancel(m16_coarse_scan_runtime *r){
 if(!valid(r)||r->phase==M16_CS_QUARANTINE)return M16_SCAN_INVALID;
 if(r->phase==M16_CS_IDLE)return 1;
 if(r->phase==M16_CS_TABLES||r->phase==M16_CS_TABLE_FLUSH)return M16_SCAN_BUSY;
 r->stop_requested=1;
 if(r->phase==M16_CS_DEVICE||r->phase==M16_CS_INVALIDATE||r->phase==M16_CS_DECODE)return 0;
 if(r->phase==M16_CS_COMPLETE||r->phase==M16_CS_STOPPED)return m16_coarse_scan_retire(r)?M16_SCAN_BUSY:1;
 if(!idle(r))return isolate(r,M16_SCAN_BUSY);
 r->phase=M16_CS_IDLE;return 1;
}
