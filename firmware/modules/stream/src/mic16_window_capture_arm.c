#include "mic16_window_capture_arm.h"
#include "mic16_network.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xtime_l.h"
#if !defined(__arm__)
#error This platform port is ARM-only; native verification uses an explicitly separate DMA model
#endif
#if XPAR_AXI_DMA_0_INCLUDE_SG || XPAR_AXI_DMA_0_INCLUDE_S2MM_DRE
#error This port requires the checked simple S2MM design without DRE
#endif
static uint32_t read_reg(void *p,unsigned off){(void)p;return Xil_In32(XPAR_AXI_DMA_0_BASEADDR+off);}
static void write_reg(void *p,unsigned off,uint32_t n){(void)p;Xil_Out32(XPAR_AXI_DMA_0_BASEADDR+off,n);}
static uint32_t frontend(void *p){(void)p;return (Xil_In32(XPAR_AXI_GPIO_2_BASEADDR)>>2)&3u;}
static void source(void *p,unsigned on){(void)p;Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,on?1u:0u);}
static uint64_t ticks(void *p){XTime now;(void)p;XTime_GetTime(&now);return (uint64_t)now;}
static void clean(void *p,void *a,unsigned n){(void)p;Xil_DCacheFlushRange((INTPTR)a,n);}
static void invalidate(void *p,void *a,unsigned n){(void)p;Xil_DCacheInvalidateRange((INTPTR)a,n);}
static uintptr_t lock(void *p){unsigned state;(void)p;__asm__ volatile("mrs %0,cpsr\n\tcpsid i\n\tdmb sy":"=r"(state)::"memory");return state;}
static void unlock(void *p,uintptr_t state){(void)p;__asm__ volatile("dmb sy":::"memory");if(!(state&128u))__asm__ volatile("cpsie i":::"memory");}
static void barrier(void *p){(void)p;__asm__ volatile("dsb sy":::"memory");}
static int apart(const void *a,size_t an,const void *b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
void m16_window_capture_arm_handler(void *context){
 m16_window_capture_arm *p=context;
 if(p && p->attached && p->runtime)m16_window_capture_irq(p->runtime);
}
int m16_window_capture_arm_attach(m16_window_capture_arm *p,m16_window_capture *c,const m16_window_capture_config *cfg){
 XScuGic_Config *gic;m16_window_capture_io io;uintptr_t key;unsigned i;int rc;
 if(!p || !c || !cfg || (uintptr_t)p%_Alignof(m16_window_capture_arm) || p->attached ||
  !apart(p,sizeof(*p),c,sizeof(*c)) || !apart(p,sizeof(*p),cfg,sizeof(*cfg)) ||
  !g_m16_network_stats.initialized || g_m16_network_stats.failed)return M16_WC_INVALID;
 for(i=0;i<3;i++)if(cfg->physical[i]!=(uint32_t)(uintptr_t)cfg->storage[i] ||
  !apart(p,sizeof(*p),cfg->storage[i],M16_WC_STORAGE))return M16_WC_INVALID;
 gic=XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);if(!gic)return M16_WC_INVALID;
 io=(m16_window_capture_io){p,read_reg,write_reg,frontend,source,ticks,clean,invalidate,lock,unlock,barrier};
 rc=m16_window_capture_init(c,&io,cfg);if(rc)return rc;
 key=lock(p);p->runtime=c;
 Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR+4,0);Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR+4,0);
 Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR,20);Xil_Out32(XPAR_AXI_GPIO_2_BASEADDR+4,15);
 XScuGic_DisableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
 XScuGic_RegisterHandler(gic->CpuBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,
  (Xil_InterruptHandler)m16_window_capture_arm_handler,p);
 XScuGic_SetPriTrigTypeByDistAddr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,0x80,1);
 write_reg(p,0x34,0x7000);p->attached=1;
 XScuGic_EnableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
 unlock(p,key);return 0;
}
int m16_window_capture_arm_detach(m16_window_capture_arm *p){
 XScuGic_Config *gic;uintptr_t key;
 if(!p || !p->attached || !p->runtime)return M16_WC_INVALID;
 if(!m16_window_capture_releasable(p->runtime))return M16_WC_BUSY;
 gic=XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);if(!gic)return M16_WC_INVALID;
 key=lock(p);XScuGic_DisableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
 write_reg(p,0x34,0x7000);p->attached=0;unlock(p,key);return 0;
}
