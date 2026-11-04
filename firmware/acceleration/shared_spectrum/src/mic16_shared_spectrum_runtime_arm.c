#include "mic16_shared_spectrum_runtime.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xtime_l.h"
/* 与本工程BD前端地址0x80000000一致；参数偏移另按已核对的新RTL合同。
 * 本文件不能使旧BIT兼容新IP，平台init的非启动探测只拒绝已知旧前端。 */
#define SHARED_BASE 0x80000000u
static uint32_t rd(void *c,unsigned o){return Xil_In32((UINTPTR)c+o);}
static void wr(void *c,unsigned o,uint32_t v){Xil_Out32((UINTPTR)c+o,v);}
static void flush(void *c,const void *p,unsigned n){(void)c;Xil_DCacheFlushRange((INTPTR)p,n);}
static void invalidate(void *c,const void *p,unsigned n){(void)c;Xil_DCacheInvalidateRange((INTPTR)p,n);}
static uint64_t ticks(void *c){XTime t;(void)c;XTime_GetTime(&t);return t;}
static void barrier(void *c){(void)c;__asm__ volatile("dmb sy" ::: "memory");}
int m16_shared_runtime_platform_init(m16_shared_runtime *w){
 m16_scanner_io io={(void *)(UINTPTR)SHARED_BASE,rd,wr,flush,invalidate,ticks,barrier,0,COUNTS_PER_SECOND};
 if(!w)return M16_SHARED_INVALID;
 io.arena_address=(uint32_t)(UINTPTR)w->words;return m16_shared_runtime_init(w,&io);
}
