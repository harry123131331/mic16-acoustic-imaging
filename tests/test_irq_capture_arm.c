/* 执行真实ARM适配层C逻辑，仅用模拟SDK替换硬件调用和三条汇编边界。
 * 不将虚拟中断处理时间当ARM测量，也不声称已验证真实cache/GIC。 */
#include "mic16_irq_capture_arm.h"
#include "mic16_network.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
extern void *__stdcall VirtualAlloc(void *,size_t,unsigned long,unsigned long);
extern int __stdcall VirtualFree(void *,size_t,unsigned long);
static unsigned checks,errors,scenarios;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("IRQ_ARM_ERROR line=%u scenario=%u\n",(unsigned)__LINE__,scenarios);}}while(0)
static mic16_irq_capture_arm c __attribute__((aligned(64)));
static unsigned char *raw;
static XAxiDma_Config config;
static XScuGic_Config gic={0x100,0x200};
volatile m16_network_stats g_m16_network_stats;
static uint64_t timer;
static unsigned sr,actual,pl,mask,source,transfers,flushes,invalidates,gic_registers,reset_hang,submit_fail,inflight;
static UINTPTR destination;
uint64_t m16_irq_test_ticks(void){return ++timer;}
unsigned m16_irq_test_lock(void){unsigned old=mask;mask|=128u;return old;}
void m16_irq_test_unlock(unsigned old){mask=old;}
void m16_irq_test_barrier(void){}
void XTime_GetTime(XTime *t){*t=m16_irq_test_ticks();}
XAxiDma_Config *XAxiDma_LookupConfig(unsigned i){CHECK(i==0);return &config;}
XScuGic_Config *XScuGic_LookupConfig(unsigned i){CHECK(i==0);return &gic;}
int XAxiDma_CfgInitialize(XAxiDma *d,XAxiDma_Config *cfg){CHECK(d==&c.dma && cfg==&config);return 0;}
int XAxiDma_HasSg(XAxiDma *d){(void)d;return 0;}
void XAxiDma_IntrDisable(XAxiDma *d,unsigned m,int dir){(void)d;(void)m;CHECK(dir==1);}
void XAxiDma_IntrEnable(XAxiDma *d,unsigned m,int dir){(void)d;CHECK(m==0x5000 && dir==1);}
void XAxiDma_IntrAckIrq(XAxiDma *d,unsigned m,int dir){(void)d;(void)m;CHECK(dir==1);}
unsigned XAxiDma_IntrGetIrq(XAxiDma *d,int dir){(void)d;CHECK(dir==1);return sr&0x7000u;}
void XAxiDma_Reset(XAxiDma *d){(void)d;if(!reset_hang)inflight=0;}
int XAxiDma_ResetIsDone(XAxiDma *d){(void)d;return !reset_hang;}
int XAxiDma_SimpleTransfer(XAxiDma *d,UINTPTR p,unsigned n,int dir){
 (void)d;CHECK(!inflight && n==8192 && dir==1 && p%64==0);destination=p;transfers++;
 if(submit_fail)return -1;inflight=1;return 0;
}
unsigned XAxiDma_ReadReg(UINTPTR base,unsigned offset){CHECK(base==0x40400030);return offset==4?sr:actual;}
void XScuGic_DisableIntr(u32 b,unsigned id){CHECK(b==0x200 && id==61);}
void XScuGic_EnableIntr(u32 b,unsigned id){CHECK(b==0x200 && id==61);}
void XScuGic_RegisterHandler(u32 b,int id,Xil_InterruptHandler h,void *p){CHECK(b==0x100 && id==61 && h==mic16_irq_capture_arm_handler && p==&c);gic_registers++;}
void XScuGic_SetPriTrigTypeByDistAddr(u32 b,unsigned id,unsigned priority,unsigned trigger){CHECK(b==0x200 && id==61 && priority==0x80 && trigger==1);}
void Xil_DCacheFlushRange(INTPTR p,u32 n){CHECK((UINTPTR)p%64==0 && n==sizeof(mic16_irq_dma_buffer));flushes++;}
void Xil_DCacheInvalidateRange(INTPTR p,u32 n){CHECK((UINTPTR)p%64==0 && (n==64 || n==1024));invalidates++;}
unsigned Xil_In32(UINTPTR p){CHECK(p==0x41220000);return (pl<<2)|3u;}
void Xil_Out32(UINTPTR p,unsigned n){if(p==0x41200000)source=n;}
int m16_av_audio_dma_destination(uint32_t p,uint32_t bytes){(void)p;CHECK(bytes==8192);return 1;}
static void init(void){
 scenarios++;memset(&c,0,sizeof(c));memset(raw,0x55,3u*2560u*1024u);timer=0;
 sr=0x1002;actual=1024;pl=2;mask=0;source=transfers=flushes=invalidates=gic_registers=reset_hang=submit_fail=inflight=0;
 g_m16_network_stats.initialized=1;g_m16_network_stats.failed=0;
 CHECK(!mic16_irq_capture_arm_open(&c,raw,3u*2560u*1024u,4));CHECK(source==1 && inflight && transfers==1 && gic_registers==1 && flushes==2 && !mask);
 c.warmup=0;
}
static void packet(unsigned char tag){
 CHECK(inflight);inflight=0;memset((void *)destination,tag,1024);mic16_irq_capture_arm_handler(&c);
}
int main(void){
 unsigned i;mic16_irq_lease l;const unsigned char *p;uint64_t completed;
 raw=VirtualAlloc((void *)0x02000000,3u*2560u*1024u,0x3000,4);
 if(raw!=(void *)0x02000000){printf("IRQ_ARM_LOW_ADDRESS_ALLOCATION_FAILED\n");return 2;}
#if defined(MIC16_IRQ_CAPTURE_GATE_TEST)
 g_m16_network_stats.initialized=1;
 CHECK(mic16_irq_capture_arm_open(&c,raw,3u*2560u*1024u,4)==M16_IRQ_NOT_WIRED);
 CHECK(!source && !transfers && !flushes && !gic_registers && !inflight);
 CHECK(VirtualFree(raw,0,0x8000));
 printf("IRQ_GATE_%s errors=%u DEVICE_ACCESS=0 ACTUAL_PRODUCTION_SITE_HEADER\n",errors?"FAIL":"PASS",errors);
 return errors?1:0;
#endif
 init();c.warmup=2;packet(11);packet(12);CHECK(!c.ring.packets && raw[0]==0x55);
 packet(13);CHECK(c.ring.packets==1 && raw[0]==13 && c.bounce==1);
 for(i=1;i<2560;i++)packet((unsigned char)i);
 CHECK(!mic16_irq_capture_arm_take(&c,&l) && l.window==0);
 p=mic16_irq_capture_arm_packet(&c,&l,0);CHECK(p && p[0]==13);
 for(i=0;i<2560;i++)packet(72);
 CHECK(p[0]==13 && c.ring.completed==2 && c.ring.slots[l.slot].state==M16_IRQ_READING);
 CHECK(mic16_irq_capture_arm_stop(&c)==1 && c.stopped && !source && !inflight);
 CHECK(c.ring.slots[l.slot].state==M16_IRQ_READING && p[0]==13);
 CHECK(!mic16_irq_capture_arm_release(&c,&l));CHECK(!mic16_irq_capture_arm_packet(&c,&l,0));
 init();actual=8192;packet(7);CHECK(c.ring.failure==M16_IRQ_LENGTH && raw[0]==0x55 && !source && transfers==1);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();sr=0x5012;packet(7);CHECK(c.ring.failure==M16_IRQ_DMA && raw[0]==0x55 && transfers==1);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();pl=3;packet(7);CHECK(c.ring.failure==M16_IRQ_PL_OVERFLOW && c.first_bad_pl==3 && raw[0]==0x55);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();pl=0;packet(7);CHECK(c.ring.failure==M16_IRQ_CLOCK);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();c.buffers[0].after[4]=0;packet(7);CHECK(c.ring.failure==M16_IRQ_GUARD && raw[0]==0x55);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();submit_fail=1;packet(7);CHECK(c.ring.failure==M16_IRQ_DMA && c.ring.packets==1 && !source);CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();mask=128;CHECK(mic16_irq_capture_arm_take(&c,&l)==M16_IRQ_EMPTY && mask==128);
 reset_hang=1;CHECK(mic16_irq_capture_arm_stop(&c)==0 && c.ring.quarantined && c.ring.slots[0].state==M16_IRQ_FILLING);
 timer+=200000;CHECK(mic16_irq_capture_arm_stop(&c)==0 && c.reset_failed && !c.stopped);
 reset_hang=0;CHECK(mic16_irq_capture_arm_stop(&c)==1 && c.stopped && c.ring.slots[0].state==M16_IRQ_FREE);
 init();for(i=0;i<3u*2560u;i++)packet((unsigned char)i);
 CHECK(c.ring.failure==M16_IRQ_OVERRUN && c.ring.completed==3 && !source && !inflight);
 completed=c.ring.completed;mic16_irq_capture_arm_handler(&c);CHECK(c.ring.completed==completed);
 CHECK(mic16_irq_capture_arm_stop(&c)==1);
 init();timer+=200000;CHECK(mic16_irq_capture_arm_take(&c,&l)==M16_IRQ_TIMEOUT && !source);
 CHECK(mic16_irq_capture_arm_stop(&c)==1);
 CHECK(VirtualFree(raw,0,0x8000));
 printf("IRQ_ARM_%s scenarios=%u errors=%u checks=%u ACTUAL_ADAPTER_C_WITH_MOCK_SDK_NOT_BOARD\n",errors?"FAIL":"PASS",scenarios,errors,checks);
 return errors?1:0;
}
