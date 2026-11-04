#include "mic16_scanner_hls.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xtime_l.h"
#if XPAR_XMIC16_TF_ZONE_STREAMING_HLS_Q2_24_0_S_AXI_CONTROL_BASEADDR != 0x80010000u
#error scanner address does not match the retained bitstream
#endif
static uint32_t read_reg(void *c,unsigned o){(void)c;return Xil_In32(0x80010000u+o);}
static void write_reg(void *c,unsigned o,uint32_t v){(void)c;Xil_Out32(0x80010000u+o,v);}
static void flush(void *c,const void *p,unsigned n){(void)c;Xil_DCacheFlushRange((INTPTR)p,n);}
static void invalidate(void *c,const void *p,unsigned n){(void)c;Xil_DCacheInvalidateRange((INTPTR)p,n);}
static uint64_t ticks(void *c){XTime t;(void)c;XTime_GetTime(&t);return t;}
static void barrier(void *c){(void)c;__asm__ volatile("dmb sy" ::: "memory");}
int m16_scanner_platform_init(m16_scanner_workspace *w){
    m16_scanner_io io={0,read_reg,write_reg,flush,invalidate,ticks,barrier,0,0};
    if(!w)return M16_SCAN_INVALID;
    io.arena_address=(uint32_t)(UINTPTR)w->arena.words;
    io.timeout_ticks=60ULL*COUNTS_PER_SECOND;
    return m16_scanner_init(w,&io);
}
