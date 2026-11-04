#include "mic16_frontend_runtime.h"
#include "mic16_cooperate.h"
#define GUARD UINT64_C(0x48a2e7b39cf016d5)
static int overlap(const void *a,unsigned na,const void *b,unsigned nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
static int quantize(float x,int32_t *out){
 double scaled,rem;int64_t q;
 if(!(x>=-1.f && x<=1.f))return M16_FRONT_INVALID;
 scaled=(double)x*2147483648.;q=(int64_t)scaled;rem=scaled-(double)q;
 if(rem>.5 || (rem==.5 && q%2))q++;
 else if(rem<-.5 || (rem==-.5 && q%2))q--;
 if(q<INT32_MIN || q>INT32_MAX)return M16_FRONT_RANGE;
 *out=(int32_t)q;return 0;
}
static int isolate(m16_front_runtime *w,m16_front_report *r,int code){w->quarantined=1;r->quarantined=1;return code;}
int m16_front_runtime_init(m16_front_runtime *w,const m16_scanner_io *io){
 unsigned i;
 if(!w || !io || ((uintptr_t)w&63u) || !io->read || !io->write || !io->ticks || !io->flush || !io->invalidate || !io->barrier ||
  !io->timeout_ticks || io->arena_address<0x100000u || (io->arena_address&63u) || io->arena_address>0x10000000u-M16_FRONT_ARENA_WORDS*8u ||
  overlap(w,sizeof(*w),io,sizeof(*io)) || w->quarantined)return M16_FRONT_INVALID;
 w->io=*io;w->initialized=0;
 for(i=0;i<8;i++)w->before[i]=w->after[i]=GUARD;
 w->initialized=1;return 0;
}
int m16_front_runtime_frame(m16_front_runtime *w,const float *x,mic16_spectral_frame *out,m16_front_report *r){
 unsigned i,polls;int32_t q,a,b;int rc;uint64_t begin,start,now,mark;
 m16_scanner_io *io;
 if(!w || !x || !out || !r || !w->initialized || w->quarantined ||
  overlap(w,sizeof(*w),x,32768u*4) || overlap(w,sizeof(*w),out,sizeof(*out)) || overlap(x,32768u*4,out,sizeof(*out)) ||
  overlap(r,sizeof(*r),w,sizeof(*w)) || overlap(r,sizeof(*r),x,32768u*4) || overlap(r,sizeof(*r),out,sizeof(*out)))return M16_FRONT_INVALID;
 /* 全部量化可行性检查在任何输出/暂存区写入之前完成。 */
 for(i=0;i<32768;i++){rc=quantize(x[i],&q);if(rc)return rc;}
 io=&w->io;
 *r=(m16_front_report){0,0,0,0,0,0,0,0};r->input_scale=31;start=io->ticks(io->context);begin=start;
 r->last_control=io->read(io->context,0);
 if(!(r->last_control&4u) || (r->last_control&0x81u))return M16_FRONT_BUSY;
 for(i=0;i<16384;i++){
  quantize(x[i*2],&a);quantize(x[i*2+1],&b);
  w->words[i]=(uint64_t)(uint32_t)a|((uint64_t)(uint32_t)b<<32);
 }
 /* 包含旧核会写的64560字全部工作区；首12912字才是本路径读取的频谱。 */
 for(i=16384;i<M16_FRONT_ARENA_WORDS;i++)w->words[i]=GUARD;
 io->flush(io->context,w->before,8u*8u);io->flush(io->context,w->words,sizeof(w->words));io->flush(io->context,w->after,8u*8u);io->barrier(io->context);
 now=io->ticks(io->context);if(now<begin)return isolate(w,r,M16_FRONT_CLOCK);r->packing_ticks=now-begin;
 io->write(io->context,4,0);io->write(io->context,8,0);
 io->write(io->context,0x10,io->arena_address);io->write(io->context,0x14,0);
 io->write(io->context,0x1c,0);io->write(io->context,0x24,16384);io->write(io->context,0x2c,2048);
 io->barrier(io->context);begin=io->ticks(io->context);if(begin<now)return isolate(w,r,M16_FRONT_CLOCK);io->write(io->context,0,1);
 for(polls=0;;polls++){
  if((polls&255u)==0)M16_COOPERATE();
  r->last_control=io->read(io->context,0);now=io->ticks(io->context);
  if(now<begin)return isolate(w,r,M16_FRONT_CLOCK);
  if(r->last_control&2u)break;
  if(now-begin>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_FRONT_TIMEOUT);
 }
 mark=now;
 for(polls=0;!(r->last_control&4u);polls++){
  if((polls&255u)==0)M16_COOPERATE();
  r->last_control=io->read(io->context,0);now=io->ticks(io->context);
  if(now<mark)return isolate(w,r,M16_FRONT_CLOCK);
  if(now-begin>=io->timeout_ticks || polls>=100000000u)return isolate(w,r,M16_FRONT_TIMEOUT);
  mark=now;
 }
 r->device_ticks=now-begin;begin=now;
 io->barrier(io->context);io->invalidate(io->context,w->before,8u*8u);io->invalidate(io->context,w->words,sizeof(w->words));io->invalidate(io->context,w->after,8u*8u);io->barrier(io->context);
 for(i=0;i<8;i++)if(w->before[i]!=GUARD || w->after[i]!=GUARD)return isolate(w,r,M16_FRONT_GUARD);
 rc=m16_frontend_import_f32_spectrum(w->words+16384,12912,31,out);
 if(rc)return isolate(w,r,M16_FRONT_GUARD);
 now=io->ticks(io->context);if(now<begin)return isolate(w,r,M16_FRONT_CLOCK);
 r->decoding_ticks=now-begin;r->total_ticks=now-start;r->completed_frames=1;return 0;
}
