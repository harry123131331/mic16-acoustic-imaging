/* 真实节点、异步驱动和HLS C模型的离线连接。CPU缓存与设备RAM分别分配，
 * 不模拟真实AXI时延，测试时钟不可换算ARM耗时。 */
#include "mic16_physical_projection_task.h"
#include "shared_projection_engine.h"
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <xmmintrin.h>
#ifdef M16_GRID_TASK_TEST
#include "mic16_grid_refinement_task.h"
extern "C" int m16_scanner_refine_accumulators(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
static m16_grid_refinement_task refinement;
#endif
struct Context {
    void *ca,*da;uint64_t *cpu,*device,tick;
    unsigned regs[16],starts,flushes,invalidates,max_bytes,bad_cache,early_invalidate,read_only_changed;
} static context;
static m16_projection_runtime runtime;
static m16_physical_projection_task task;
static uint32_t read_reg(void *p,unsigned at){Context *c=(Context *)p;unsigned v=c->regs[at/4];if(!at)c->regs[0]&=~2u;return v;}
static void write_reg(void *p,unsigned at,uint32_t v){
    Context *c=(Context *)p;if(!at&&v==1u){c->starts++;c->regs[0]=0;}else c->regs[at/4]=v;
}
static void cache(void *p,const void *address,unsigned bytes,unsigned inv){
    Context *c=(Context *)p;uintptr_t at=(uintptr_t)address,first=(uintptr_t)c->cpu;
    if(bytes>c->max_bytes)c->max_bytes=bytes;
    if((at&63u)||(bytes&63u)||!bytes||bytes>1024u||at<first||at-first>M16_PP_ARENA_WORDS*8u-bytes){c->bad_cache++;return;}
    unsigned char *d=(unsigned char *)c->device+(at-first);
    if(inv){c->invalidates++;if(!(c->regs[0]&4u))c->early_invalidate++;memcpy((void *)address,d,bytes);}
    else{c->flushes++;memcpy(d,address,bytes);}
}
static void flush(void *p,const void *a,unsigned n){cache(p,a,n,0);}
static void invalidate(void *p,const void *a,unsigned n){cache(p,a,n,1);}
static uint64_t ticks(void *p){return ++((Context *)p)->tick;}
static void barrier(void *){}
extern "C" {
static int reset_context(void){
    free(context.ca);free(context.da);memset(&context,0,sizeof(context));memset(&runtime,0,sizeof(runtime));memset(&task,0,sizeof(task));
    context.ca=malloc(M16_PP_ARENA_WORDS*8u+63u);context.da=malloc(M16_PP_ARENA_WORDS*8u+63u);
    if(!context.ca||!context.da)return -999;
    context.cpu=(uint64_t *)(((uintptr_t)context.ca+63u)&~(uintptr_t)63u);
    context.device=(uint64_t *)(((uintptr_t)context.da+63u)&~(uintptr_t)63u);
    memset(context.cpu,0xa5,M16_PP_ARENA_WORDS*8u);memset(context.device,0x5a,M16_PP_ARENA_WORDS*8u);
    context.regs[0]=4;context.tick=100;
    m16_projection_io io={&context,read_reg,write_reg,flush,invalidate,ticks,barrier,0x02000000u,100000u};
    int rc=m16_projection_runtime_init(&runtime,&io,context.cpu,M16_PP_ARENA_WORDS,M16_PROJECTION_BUILD_ABI);
    if(!rc)rc=m16_physical_projection_init(&task,&runtime);
    return rc;
}
int pp_reset(const float *phat,const double *directions,unsigned count,float *out,
             unsigned nv,unsigned nf,uint64_t epoch,uint64_t first_tag){
    int rc=reset_context();
    if(!rc)rc=m16_physical_projection_begin(&task,phat,nv,directions,count,out,nf,epoch,first_tag);
    return rc;
}
int pp_reset_grids(const float *phat,const unsigned *grids,unsigned count,unsigned zone,float *out,
                   unsigned nv,unsigned nf,uint64_t epoch,uint64_t first_tag){
    int rc=reset_context();
    if(!rc)rc=m16_physical_projection_begin_grids(&task,phat,nv,grids,count,zone,out,nf,epoch,first_tag);
    return rc;
}
int pp_begin(const float *p,const double *d,unsigned c,float *o,unsigned nv,unsigned nf,uint64_t epoch,uint64_t tag){
    return m16_physical_projection_begin(&task,p,nv,d,c,o,nf,epoch,tag);
}
void *pp_runtime_bridge(unsigned address){
    if(address<0x100040u||(address&63u)||address>0x10000000u-M16_PP_ARENA_WORDS*8u||reset_context())return 0;
    /* 联合测试只在空闲、未提交状态指定独立模拟物理区域。 */
    runtime.io.arena_address=address;return &runtime;
}
int pp_step(void){return m16_physical_projection_step(&task);}
int pp_stop(void){return m16_physical_projection_stop(&task);}
int pp_hardware(void){
    if(runtime.phase!=M16_PR_DEVICE||context.regs[0]||context.regs[0x18/4]!=runtime.io.arena_address||
       context.regs[0x24/4]!=M16_PP_ARENA_WORDS||context.regs[0x2c/4]!=8||context.regs[0x34/4]!=7)return -999;
    uint64_t *before=(uint64_t *)malloc(M16_PP_ARENA_WORDS*8u);
    unsigned char *mutable_words=(unsigned char *)calloc(M16_PP_ARENA_WORDS,1);
    if(!before||!mutable_words){free(before);free(mutable_words);return -999;}
    memcpy(before,context.device,M16_PP_ARENA_WORDS*8u);
    for(unsigned j=0;j<7;j++){
        const m16_projection_job &job=runtime.plan[j];
        memset(mutable_words+job.out,1,job.nr);memset(mutable_words+job.report,1,2);
    }
    int rc=m16_shared_projection_engine(context.device,M16_PP_ARENA_WORDS,8,7);
    for(unsigned i=0;i<M16_PP_ARENA_WORDS;i++)if(!mutable_words[i]&&before[i]!=context.device[i])context.read_only_changed++;
    free(before);free(mutable_words);context.regs[0x10/4]=(uint32_t)rc;context.regs[0]=6;return rc;
}
void pp_control(unsigned value){context.regs[0]=value;}
void pp_clock(uint64_t value){context.tick=value;}
void pp_word(unsigned at,uint64_t value,unsigned device){if(at<M16_PP_ARENA_WORDS)(device?context.device:context.cpu)[at]=value;}
uint64_t pp_get(unsigned at,unsigned device){return at<M16_PP_ARENA_WORDS?(device?context.device:context.cpu)[at]:0;}
void pp_bad_lease(void){runtime.phase=M16_PR_PREPARE;}
unsigned pp_float_mode(unsigned value){unsigned old=_mm_getcsr();_mm_setcsr(value);return old;}
int pp_round_down(unsigned active){return fesetround(active?FE_DOWNWARD:FE_TONEAREST);}
uint64_t pp_metric(unsigned i){
    switch(i){case 0:return task.phase;case 1:return runtime.phase;case 2:return context.starts;
    case 3:return context.flushes;case 4:return context.invalidates;case 5:return context.max_bytes;
    case 6:return context.bad_cache;case 7:return context.early_invalidate;case 8:return task.last_step_items;
    case 9:return task.max_step_items;case 10:return task.transactions;case 11:return task.results;
    case 12:return task.zone;case 13:return task.bins;case 14:return task.used_words;
    case 15:return task.epoch;case 16:return task.reserved_tag_high;case 17:return runtime.tag_high;
    case 18:return task.steps;case 19:return context.read_only_changed;case 20:return task.receipts;
    case 21:return task.out;case 22:return task.coef;case 23:return task.obs;
    case 24:return m16_projection_runtime_available(&runtime);case 25:return (uint64_t)(int64_t)task.error;
    case 26:return context.regs[0];
    default:return 0;}
}
#ifdef M16_GRID_TASK_TEST
int rg_reset(const float *phat,float *maps,unsigned nv,unsigned nm,uint64_t epoch,uint64_t tag){
    int rc=reset_context();memset(&refinement,0,sizeof(refinement));
    if(!rc)rc=m16_grid_refinement_init(&refinement,&runtime);
    if(!rc)rc=m16_grid_refinement_begin(&refinement,phat,nv,maps,nm,epoch,tag);
    return rc;
}
int rg_begin(const float *phat,float *maps,unsigned nv,unsigned nm,uint64_t epoch,uint64_t tag){
    return m16_grid_refinement_begin(&refinement,phat,nv,maps,nm,epoch,tag);
}
int rg_step(void){return m16_grid_refinement_step(&refinement);}
void rg_other_owner_overwrote_observations(void){
    /* 模拟已归还arena被另一事务使用；新提交标签使缓存失效，不改输入PHAT。 */
    runtime.tag_high++;
    for(unsigned i=0;i<20u*32u*refinement.projection.bins;i++)context.cpu[refinement.projection.obs+i]=0;
}
int rg_stop(void){return m16_grid_refinement_stop(&refinement);}
int rg_reference(const float *phat,float *maps,unsigned *out){
    mic16_spatial_workspace w;m16_scanner_refinement report;
    int rc=m16_scanner_refine_accumulators(phat,maps,&w,&report);
    out[0]=report.refined_grids;out[1]=report.full_zones;out[2]=report.zero_input;return rc;
}
uint64_t rg_metric(unsigned i){
    switch(i){case 0:return refinement.phase;case 1:return refinement.zone;case 2:return refinement.frame;
    case 3:return refinement.batches;case 4:return refinement.report.refined_grids;
    case 5:return refinement.report.full_zones;case 6:return refinement.report.zero_input;
    case 7:return refinement.last_items;case 8:return refinement.max_items;case 9:return refinement.epoch;
    case 10:return refinement.reserved_tag_high;case 11:return refinement.steps;
    case 12:return refinement.projection.phase;
    case 13:return refinement.projection.observation_words;
    case 14:return refinement.projection.observation_reuses;
    case 15:return refinement.projection.reuse_active;default:return 0;}
}
#endif
}
