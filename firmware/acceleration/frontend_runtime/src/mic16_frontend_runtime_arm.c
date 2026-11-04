#include "mic16_frontend_runtime.h"
#include "xparameters.h"
#include "xm16_tf_front_q24_hw.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xtime_l.h"
#if XPAR_XM16_TF_FRONT_Q24_0_S_AXI_CONTROL_BASEADDR != 0x80000000u
#error frontend_address_does_not_match_retained_bitstream
#endif
/* 寄存器布局直接核对同一XSA生成的官方驱动头，不猜地址。 */
_Static_assert(XM16_TF_FRONT_Q24_CONTROL_ADDR_DDR_ARENA_DATA==0x10,"DDR寄存器不匹配");
_Static_assert(XM16_TF_FRONT_Q24_CONTROL_ADDR_WORKSPACE_WORD_OFFSET_DATA==0x24,"工作区寄存器不匹配");
_Static_assert(XM16_TF_FRONT_Q24_CONTROL_ADDR_FRAME_SAMPLES_DATA==0x2c,"样本数寄存器不匹配");
static uint32_t read_reg(void *c,unsigned o){(void)c;return Xil_In32(0x80000000u+o);}
static void write_reg(void *c,unsigned o,uint32_t v){(void)c;Xil_Out32(0x80000000u+o,v);}
static void flush(void *c,const void *p,unsigned n){(void)c;Xil_DCacheFlushRange((INTPTR)p,n);}
static void invalidate(void *c,const void *p,unsigned n){(void)c;Xil_DCacheInvalidateRange((INTPTR)p,n);}
static uint64_t ticks(void *c){XTime t;(void)c;XTime_GetTime(&t);return t;}
static void barrier(void *c){(void)c;__asm__ volatile("dmb sy" ::: "memory");}
int m16_front_runtime_platform_init(m16_front_runtime *w){
 m16_scanner_io io={0,read_reg,write_reg,flush,invalidate,ticks,barrier,0,0};
 if(!w)return M16_FRONT_INVALID;
 io.arena_address=(uint32_t)(UINTPTR)w->words;io.timeout_ticks=COUNTS_PER_SECOND;
 return m16_front_runtime_init(w,&io);
}
