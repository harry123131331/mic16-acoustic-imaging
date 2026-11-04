#include "mic16_statistics_tile_runtime.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xtime_l.h"
/* 仅用于statistics_sources候选：HP2、125MHz、控制地址0x80020000。
 * 必须先核对候选BIT/XSA摘要，不可加载原WDAS硬件后调用本驱动。 */
#define TILE_BASE 0x80020000u
static uint32_t rd(void *c,unsigned o){return Xil_In32((UINTPTR)c+o);}
static void wr(void *c,unsigned o,uint32_t v){Xil_Out32((UINTPTR)c+o,v);}
static void flush(void *c,const void *p,unsigned n){(void)c;Xil_DCacheFlushRange((INTPTR)p,n);}
static void invalidate(void *c,const void *p,unsigned n){(void)c;Xil_DCacheInvalidateRange((INTPTR)p,n);}
static uint64_t ticks(void *c){XTime t;(void)c;XTime_GetTime(&t);return t;}
static void barrier(void *c){(void)c;__asm__ volatile("dmb sy" ::: "memory");}
int m16_tile_runtime_platform_init(m16_tile_runtime *w,unsigned abi){
 m16_scanner_io io={(void *)(UINTPTR)TILE_BASE,rd,wr,flush,invalidate,ticks,barrier,0,COUNTS_PER_SECOND};
 if(!w)return M16_TILE_INVALID;
 io.arena_address=(uint32_t)(UINTPTR)w->words;return m16_tile_runtime_init(w,&io,abi);
}
