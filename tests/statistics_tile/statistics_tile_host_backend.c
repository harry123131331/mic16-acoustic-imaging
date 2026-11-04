/* 宿主寄存器/缓存替身，计算执行真实HLS C模型；不能当作实板或时序测试。 */
#include "mic16_statistics_tile_runtime.h"
#include "statistics_tile.h"
#include <string.h>
static m16_tile_runtime state __attribute__((aligned(64)));
static unsigned regs[32];
static uint64_t now;
unsigned m16_tile_host_mode,m16_tile_host_flush,m16_tile_host_invalidates,m16_tile_host_cache_errors;
unsigned m16_tile_host_flush_obs,m16_tile_host_starts,m16_tile_host_reused;
static uint32_t read_reg(void *c,unsigned offset){
 unsigned value;(void)c;value=regs[offset/4];
 if(!offset && (value&2u))regs[0]=m16_tile_host_mode==6?4u:value&~2u;
 return value;
}
static void write_reg(void *c,unsigned offset,uint32_t value){
 (void)c;if(m16_tile_host_mode==8 && offset==0x54)return;
 regs[offset/4]=value;
 if(!offset && (value&1u)){
  int rc;m16_tile_host_starts++;
  if(m16_tile_host_mode==1){regs[0]=0;return;}
  rc=m16_statistics_tile(state.words,regs[0x24/4],regs[0x2c/4],regs[0x34/4],regs[0x3c/4],
                         regs[0x44/4],regs[0x4c/4],regs[0x54/4]);
  regs[0x10/4]=(uint32_t)(m16_tile_host_mode==2?-2:rc);
  if(m16_tile_host_mode==3)state.after[0]^=1;
  if(m16_tile_host_mode==4)state.words[2303]^=1;
  if(m16_tile_host_mode==5)state.words[2048]=UINT64_C(0x7fc000007fc00000);
  regs[0]=m16_tile_host_mode==6?2u:6u;
 }
}
static void flush(void *c,const void *p,unsigned n){
 uintptr_t x=(uintptr_t)p,begin=(uintptr_t)state.words;(void)c;
 m16_tile_host_flush+=n;
 if((x&63u) || (n&63u) || n>1024)m16_tile_host_cache_errors++;
 if(x>=begin && x<begin+8192)m16_tile_host_flush_obs+=n;
}
static void invalidate(void *c,const void *p,unsigned n){
 (void)c;m16_tile_host_invalidates+=n;
 if(((uintptr_t)p&63u) || (n&63u) || n>1024)m16_tile_host_cache_errors++;
}
static uint64_t ticks(void *c){(void)c;return ++now;}
static void barrier(void *c){(void)c;}
m16_tile_runtime *m16_tile_host_state(void){return &state;}
int m16_tile_host_reset(unsigned mode){
 m16_scanner_io io={0,read_reg,write_reg,flush,invalidate,ticks,barrier,0x200040,100};
 memset(&state,0,sizeof(state));memset(regs,0,sizeof(regs));regs[0]=4;now=0;
 m16_tile_host_mode=mode;m16_tile_host_flush=m16_tile_host_invalidates=m16_tile_host_cache_errors=0;
 m16_tile_host_flush_obs=m16_tile_host_starts=m16_tile_host_reused=0;
 return m16_tile_runtime_init(&state,&io,M16_TILE_BUILD_ABI);
}
int m16_tile_host_bind(void){int rc=m16_tile_host_reset(0);return rc?rc:m16_tile_runtime_bind(&state);}
