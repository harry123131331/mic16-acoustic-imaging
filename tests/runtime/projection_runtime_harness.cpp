/* 独立虚拟设备：真实C驱动+真实HLS C模型，CPU缓存与设备RAM分开。
 * 不模拟真实AXI时延，不构成RTL/ARM计时或板卡验收。 */
#include "mic16_projection_runtime.h"
#include "shared_projection_engine.h"
#include <stdlib.h>
#include <string.h>
struct Context {
    void *cpu_allocation,*device_allocation;
    uint64_t *cpu,*device,tick;
    unsigned words,registers[16],starts,flushes,invalidates,max_bytes,bad_cache,early_invalidate,writes;
    uint64_t flush_bytes,invalidate_bytes;
} static context;
static m16_projection_runtime runtime;
static uint32_t read_reg(void *p,unsigned offset){
    Context *c=(Context *)p;unsigned value=c->registers[offset/4];
    if(!offset)c->registers[0]&=~2u;
    return value;
}
static void write_reg(void *p,unsigned offset,uint32_t value){
    Context *c=(Context *)p;c->writes++;
    if(!offset&&value==1){c->starts++;c->registers[0]=0;}else c->registers[offset/4]=value;
}
static void cache(void *p,const void *address,unsigned bytes,unsigned invalidate){
    Context *c=(Context *)p;uintptr_t first=(uintptr_t)c->cpu,at=(uintptr_t)address;
    if(bytes>c->max_bytes)c->max_bytes=bytes;
    if((at&63u)||(bytes&63u)||!bytes||bytes>1024||bytes>(uintptr_t)c->words*8u||
       at<first||at-first>(uintptr_t)c->words*8u-bytes){c->bad_cache=1;return;}
    unsigned char *device=(unsigned char *)c->device+(at-first);
    if(invalidate){
        c->invalidates++;c->invalidate_bytes+=bytes;if(!(c->registers[0]&4u))c->early_invalidate=1;
        memcpy((void *)address,device,bytes);
    }else{c->flushes++;c->flush_bytes+=bytes;memcpy(device,address,bytes);}
    c->tick++;
}
static void flush(void *p,const void *a,unsigned n){cache(p,a,n,0);}
static void invalidate(void *p,const void *a,unsigned n){cache(p,a,n,1);}
static uint64_t ticks(void *p){return ++((Context *)p)->tick;}
static void barrier(void *p){(void)p;}
extern "C" {
int projection_harness_reset(const uint64_t *source,unsigned words,unsigned abi,unsigned control){
    free(context.cpu_allocation);free(context.device_allocation);memset(&context,0,sizeof(context));
    memset(&runtime,0,sizeof(runtime));
    context.cpu_allocation=malloc((size_t)words*8u+63u);context.device_allocation=malloc((size_t)words*8u+63u);
    if(!context.cpu_allocation||!context.device_allocation)return -999;
    context.cpu=(uint64_t *)(((uintptr_t)context.cpu_allocation+63u)&~(uintptr_t)63u);
    context.device=(uint64_t *)(((uintptr_t)context.device_allocation+63u)&~(uintptr_t)63u);
    context.words=words;context.registers[0]=control;context.tick=100;
    memcpy(context.cpu,source,(size_t)words*8u);memset(context.device,0xa5,(size_t)words*8u);
    m16_projection_io io={&context,read_reg,write_reg,flush,invalidate,ticks,barrier,0x02000000u,100000u};
    return m16_projection_runtime_init(&runtime,&io,context.cpu,words,abi);
}
int projection_harness_submit(unsigned desc,unsigned jobs){return m16_projection_runtime_submit(&runtime,desc,jobs);}
int projection_harness_poll(void){return m16_projection_runtime_poll(&runtime);}
int projection_harness_hardware(void){
    if(runtime.phase!=M16_PR_DEVICE||context.registers[0x18/4]!=0x02000000u||context.registers[0x1c/4]||
       context.registers[0x24/4]!=context.words)return -999;
    int rc=m16_shared_projection_engine(context.device,context.words,context.registers[0x2c/4],context.registers[0x34/4]);
    context.registers[0x10/4]=(uint32_t)rc;context.registers[0]=6;return rc;
}
void projection_harness_control(unsigned value){context.registers[0]=value;}
void projection_harness_clock(uint64_t tick){context.tick=tick;}
void projection_harness_word(unsigned at,uint64_t word,unsigned device){if(at<context.words)(device?context.device:context.cpu)[at]=word;}
uint64_t projection_harness_get(unsigned at,unsigned device){return at<context.words?(device?context.device:context.cpu)[at]:0;}
int projection_harness_copy(uint64_t *out,unsigned words){if(words<context.words)return -1;memcpy(out,context.cpu,(size_t)context.words*8u);return 0;}
int projection_harness_retire(void){return m16_projection_runtime_retire(&runtime);}
int projection_harness_cancel(void){return m16_projection_runtime_cancel(&runtime);}
uint64_t projection_harness_metric(unsigned i){
    switch(i){case 0:return runtime.phase;case 1:return context.flushes;case 2:return context.invalidates;
        case 3:return context.max_bytes;case 4:return context.bad_cache;case 5:return context.starts;
        case 6:return context.early_invalidate;case 7:return context.writes;
        case 8:return context.flush_bytes;case 9:return context.invalidate_bytes;
        case 10:return m16_projection_runtime_available(&runtime);case 11:return runtime.tag_high;
        case 12:return runtime.report.total_ticks;default:return 0;}
}
}
