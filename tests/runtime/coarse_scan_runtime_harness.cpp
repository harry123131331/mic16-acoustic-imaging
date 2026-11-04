/* 异步粗扫真实C驱动：CPU缓存、设备RAM和原HLS C模型各有独立数组。
 * 控制故障用明确替身；数值测试显式调用原核，不把替身零输出算数值通过。 */
extern "C" {
#include "mic16_coarse_scan_runtime.h"
}
#include "mic16_tf_zone_streaming_hls_q2_24.h"
#include <string.h>
#include <fenv.h>
static m16_coarse_scan_runtime runtime;
static m16_scanner_workspace reference __attribute__((aligned(64)));
static uint64_t device[M16_SCAN_PADDED_WORDS+16] __attribute__((aligned(64)));
static ap_uint<64> model[M16_SCAN_PADDED_WORDS+16],reference_model[M16_SCAN_PADDED_WORDS+16];
struct Context {
 unsigned regs[32],starts,flushes,invalidates,max_bytes,bad_cache,early_invalidate,read_only_changed,actual_calls,reference_calls;
 uint64_t tick,table_flush_bytes;
} static context;
static uint32_t read_reg(void *p,unsigned offset){Context *c=(Context *)p;unsigned value=c->regs[offset/4u];if(!offset)c->regs[0]&=~2u;return value;}
static void write_reg(void *p,unsigned at,uint32_t v){
 Context *c=(Context *)p;if(!at&&v==1){c->starts++;c->regs[0]=0;}else c->regs[at/4u]=v;
}
static void cache(void *p,const void *a,unsigned n,unsigned inv){
 Context *c=(Context *)p;uintptr_t start=(uintptr_t)runtime.arena.before,at=(uintptr_t)a;
 if(n>c->max_bytes)c->max_bytes=n;
 if((at&63u)||(n&63u)||!n||n>1024u||at<start||at-start>sizeof(device)-n){c->bad_cache++;return;}
 unsigned char *destination=(unsigned char *)device+(at-start);
 if(inv){c->invalidates++;if(!(c->regs[0]&4u))c->early_invalidate++;memcpy((void *)a,destination,n);}
 else{
  c->flushes++;memcpy(destination,a,n);
  uintptr_t first=(uintptr_t)(runtime.arena.words+M16_SCAN_PHASE_OFFSET),last=(uintptr_t)(runtime.arena.words+M16_SCAN_OUTPUT_OFFSET);
  if(at>=first&&at+n<=last)c->table_flush_bytes+=n;
 }
}
static void flush(void *c,const void *a,unsigned n){cache(c,a,n,0);}
static void invalidate(void *c,const void *a,unsigned n){cache(c,a,n,1);}
static uint64_t ticks(void *p){return ++((Context *)p)->tick;}
static void barrier(void *){}
static uint32_t reference_read(void *,unsigned){return 4;}
static void reference_write(void *,unsigned,uint32_t){}
static void reference_cache(void *,const void *,unsigned){}
static uint64_t reference_tick(void *){return 1;}
extern "C" {
int cs_reset(unsigned control){
 memset(&runtime,0,sizeof(runtime));memset(&context,0,sizeof(context));memset(device,0x5a,sizeof(device));
 context.regs[0]=control;context.tick=100;
 m16_scanner_io io={&context,read_reg,write_reg,flush,invalidate,ticks,barrier,0x02000000u,100000u};
 return m16_coarse_scan_init(&runtime,&io);
}
int cs_poll(void){return m16_coarse_scan_poll(&runtime);}
void *cs_runtime_bridge(void){return &runtime;}
int cs_submit(const float *p,unsigned nv,float *maps,unsigned nm,uint64_t epoch,uint64_t frame){
 return m16_coarse_scan_submit(&runtime,p,nv,maps,nm,epoch,frame);
}
int cs_retire(void){return m16_coarse_scan_retire(&runtime);}
int cs_cancel(void){return m16_coarse_scan_cancel(&runtime);}
int cs_hardware(unsigned actual){
 if(runtime.phase!=M16_CS_DEVICE||context.regs[0]||context.regs[0x10/4]!=0x02000000u||context.regs[0x14/4]||
    context.regs[0x1c/4]||context.regs[0x24/4]!=M16_SCAN_PHASE_OFFSET||context.regs[0x2c/4]!=M16_SCAN_STEP_OFFSET||context.regs[0x34/4]!=M16_SCAN_OUTPUT_OFFSET)return -999;
 uint64_t before[M16_SCAN_PADDED_WORDS+16];memcpy(before,device,sizeof(device));
 if(actual){
  for(unsigned i=0;i<M16_SCAN_PADDED_WORDS+16;i++)model[i]=device[i];
  mic16_tf_zone_streaming_hls_q2_24(model+8,0,M16_SCAN_PHASE_OFFSET,M16_SCAN_STEP_OFFSET,M16_SCAN_OUTPUT_OFFSET);
  for(unsigned i=0;i<M16_SCAN_PADDED_WORDS+16;i++)device[i]=(uint64_t)model[i];
  context.actual_calls++;
 }else{
  for(unsigned i=0;i<3900u;i++)device[8+M16_SCAN_OUTPUT_OFFSET+i]=0;
 }
 for(unsigned i=0;i<M16_SCAN_PADDED_WORDS+16;i++)
  if((i<8u+M16_SCAN_OUTPUT_OFFSET||i>=8u+M16_SCAN_WORDS)&&device[i]!=before[i])context.read_only_changed++;
 context.regs[0]=6;return 0;
}
int cs_reference(const float *phat,float *maps){
 memset(&reference,0,sizeof(reference));
 m16_scanner_io io={0,reference_read,reference_write,reference_cache,reference_cache,reference_tick,barrier,0x03000000u,100000u};
 int rc=m16_scanner_init(&reference,&io);if(rc)return rc;
 for(unsigned i=0;i<M16_SCAN_OBS_WORDS;i++){
  int32_t a,b;if(m16_scanner_quantize(phat[2u*i],&a)||m16_scanner_quantize(phat[2u*i+1u],&b))return -99;
  reference.arena.words[i]=m16_scanner_pack(a,b);
 }
 for(unsigned i=0;i<M16_SCAN_PADDED_WORDS+16;i++)reference_model[i]=((uint64_t *)reference.arena.before)[i];
 mic16_tf_zone_streaming_hls_q2_24(reference_model+8,0,M16_SCAN_PHASE_OFFSET,M16_SCAN_STEP_OFFSET,M16_SCAN_OUTPUT_OFFSET);
 for(unsigned i=0;i<24u*325u;i++){
  uint64_t word=(uint64_t)reference_model[8+M16_SCAN_OUTPUT_OFFSET+i/2];
  int32_t raw=(int32_t)(uint32_t)(i%2?word>>32:word);maps[i]=(float)raw*(1.0f/16777216.0f);
 }
 context.reference_calls++;return 0;
}
int cs_tables_equal(void){
 return memcmp(runtime.arena.words+M16_SCAN_PHASE_OFFSET,reference.arena.words+M16_SCAN_PHASE_OFFSET,
      (M16_SCAN_OUTPUT_OFFSET-M16_SCAN_PHASE_OFFSET)*8u)==0;
}
void cs_control(unsigned value){context.regs[0]=value;}
void cs_clock(uint64_t value){context.tick=value;}
void cs_corrupt_guard(void){device[M16_SCAN_PADDED_WORDS+8]=0;}
void cs_bad_output(void){device[8+M16_SCAN_OUTPUT_OFFSET]=UINT64_C(0x7fffffff);}
int cs_round_down(unsigned active){return fesetround(active?FE_DOWNWARD:FE_TONEAREST);}
uint64_t cs_metric(unsigned i){
 switch(i){case 0:return runtime.phase;case 1:return m16_coarse_scan_available(&runtime);case 2:return context.starts;
 case 3:return context.flushes;case 4:return context.invalidates;case 5:return context.max_bytes;case 6:return context.bad_cache;
 case 7:return context.early_invalidate;case 8:return context.read_only_changed;case 9:return context.table_flush_bytes;
 case 10:return context.regs[0];case 11:return runtime.done_seen;case 12:return runtime.last_epoch;case 13:return runtime.last_frame;
 case 14:return sizeof(runtime);case 15:return context.actual_calls;case 16:return context.reference_calls;
 case 17:return runtime.report.error;default:return 0;}
}
}
