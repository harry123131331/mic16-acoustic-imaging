/* 旧FFT寄存器合同的控制模型：CPU/设备RAM分离；写入的已知复数不是FFT计算。 */
#include "mic16_shared_spectrum_task.h"
#include <string.h>
static m16_shared_runtime owner __attribute__((aligned(64)));
static m16_shared_spectrum_task task;
static struct {uint64_t before[8],words[M16_SHARED_ARENA_WORDS],after[8];} device __attribute__((aligned(64)));
static unsigned ctrl,regs[32],hardware_active,starts,cache_errors,early_invalidates,maximum_cache,legacy;
static uint64_t now;
static uint32_t read_reg(void *context,unsigned off){
 unsigned value;(void)context;
 if(legacy){off&=63u;if(off==0)return ctrl;if(off==4)return regs[1]&1u;return 0;}
 if(off==0){value=ctrl;ctrl&=~2u;return value;}
 return regs[off/4];
}
static void write_reg(void *context,unsigned off,uint32_t n){
 (void)context;
 if(legacy){off&=63u;if(off==0){if(n&1u)starts++;}else if(off==4)regs[1]=n&1u;return;}
 regs[off/4]=n;
 if(off==0 && (n&1u)){
  if(hardware_active || regs[0x18/4]!=0x04000000u || regs[0x1c/4] || regs[0x24/4] ||
   regs[0x2c/4]!=16384 || regs[0x34/4]!=2048 || regs[0x3c/4]!=4 || regs[0x44/4]!=task.active)cache_errors++;
  hardware_active=1;ctrl=0;starts++;
 }
}
static void cache(void *context,const void *p,unsigned n,unsigned inv){
 uintptr_t at=(uintptr_t)p,base=(uintptr_t)&owner;unsigned char *d;uintptr_t offset;(void)context;
 if(!n || n>1024 || at%64 || n%64){cache_errors++;return;}
 if(n>maximum_cache)maximum_cache=n;
 if(at>=(uintptr_t)owner.before && at+n<=(uintptr_t)owner.after+64u){
  offset=at-base;d=(unsigned char *)&device+offset;
 }else{cache_errors++;return;}
 if(inv){if(hardware_active || !(ctrl&4u)){early_invalidates++;return;}memcpy((void *)p,d,n);}
 else {if(hardware_active){cache_errors++;return;}memcpy(d,p,n);}
}
static void flush(void *p,const void *a,unsigned n){cache(p,a,n,0);}
static void invalidate(void *p,const void *a,unsigned n){cache(p,a,n,1);}
static uint64_t ticks(void *p){(void)p;return now;}
static void barrier(void *p){(void)p;}
int ss_reset(unsigned use_legacy){
 m16_scanner_io io;int rc;
 memset(&owner,0,sizeof(owner));memset(&task,0,sizeof(task));memset(&device,0x5a,sizeof(device));memset(regs,0,sizeof(regs));
 ctrl=4;legacy=use_legacy;regs[1]=1;hardware_active=starts=cache_errors=early_invalidates=maximum_cache=0;now=100;
 io=(m16_scanner_io){0,read_reg,write_reg,flush,invalidate,ticks,barrier,0x04000000u,1000};
 rc=m16_shared_runtime_init(&owner,&io);if(rc)return rc;
 return m16_shared_spectrum_task_init(&task,&owner);
}
int ss_acquire(uint64_t epoch,uint64_t job,unsigned active){return m16_shared_spectrum_task_acquire(&task,epoch,job,active);}
int ss_fill(void){
 unsigned i;uint64_t *p=m16_shared_spectrum_task_arena(&task);if(!p)return -999;
 for(i=0;i<task.active*1024u;i++)p[i]=(uint64_t)(0x3d800000u+(i&255u))|((uint64_t)(0xbd000000u+(i&127u))<<32);
 return 0;
}
int ss_bad_input(unsigned index,unsigned bits){
 uint64_t *p=m16_shared_spectrum_task_arena(&task),v;
 if(!p || index>=task.active*2048u)return -999;
 v=p[index/2u];p[index/2u]=index&1u?(v&0xffffffffu)|((uint64_t)bits<<32):(v&UINT64_C(0xffffffff00000000))|bits;
 return 0;
}
int ss_submit(void){return m16_shared_spectrum_task_submit(&task);}
int ss_poll(void){return m16_shared_spectrum_task_poll(&task);}
int ss_stop(void){return m16_shared_spectrum_task_stop(&task);}
int ss_retire(uint64_t epoch,uint64_t job){return m16_shared_spectrum_task_retire(&task,epoch,job);}
int ss_releasable(void){return m16_shared_spectrum_task_releasable(&task);}
int ss_reinitialize(void){m16_scanner_io io=owner.io;return m16_shared_runtime_init(&owner,&io);}
int ss_sync_try(void){m16_shared_report r;return m16_shared_runtime_execute(&owner,task.active,&r);}
void ss_complete(unsigned kind,unsigned done_idle){
 unsigned i;
 for(i=0;i<task.active*807u;i++)device.words[16384u+i]=(uint64_t)(0x3d800000u+(i&255u))|((uint64_t)(0xbf000000u+(i&127u))<<32);
 if(kind==1)device.before[0]^=1u;
 if(kind==2)device.words[task.tail_word]^=1u;
 if(kind==3)device.after[7]^=1u;
 if(kind==4)device.words[16384u+17u]=UINT64_C(0x3f0000007fc00000);
 regs[0x10/4]=kind==5?0xffffffffu:0;ctrl=done_idle?6u:2u;hardware_active=done_idle?0u:1u;
}
void ss_control(unsigned value){ctrl=value;if((value&4u) && !(value&1u))hardware_active=0;}
void ss_clock(uint64_t value){now=value;}
void ss_advance(uint64_t value){now+=value;}
int ss_copy(uint64_t *out){
 if(!out || task.phase!=M16_SS_COMPLETE)return -999;
 memcpy(out,owner.words+16384u,task.active*807u*8u);return 0;
}
int ss_read(unsigned slot,double *re,double *im){return m16_shared_read_bins(owner.words,M16_SHARED_ARENA_WORDS,slot,re,im);}
m16_shared_spectrum_task *ss_task_bridge(void){return &task;}
m16_shared_runtime *ss_runtime_bridge(void){return &owner;}
int ss_device_input_copy(uint64_t *out,unsigned words){
 if(!out || words>16384u)return -999;
 memcpy(out,device.words,words*8u);return 0;
}
int ss_output_access(uint64_t epoch,uint64_t job){return m16_shared_spectrum_task_output(&task,epoch,job)!=0;}
uint64_t ss_metric(unsigned n){
 switch(n){
 case 0:return task.phase;case 1:return owner.busy;case 2:return owner.quarantined;case 3:return starts;
 case 4:return maximum_cache;case 5:return cache_errors;case 6:return early_invalidates;
 case 7:return task.seen_done;case 8:return task.last_control;case 9:return task.steps;
 case 10:return task.epoch;case 11:return task.job;case 12:return now;
 case 13:return m16_shared_runtime_available(&owner);
 case 14:return m16_shared_spectrum_task_arena(&task)!=0;
 default:return UINT64_MAX;
 }
}
