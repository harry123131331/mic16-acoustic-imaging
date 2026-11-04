#include "mic16_irq_capture_arm.h"
#if defined(MIC16_IRQ_CAPTURE_ENABLED) && MIC16_IRQ_CAPTURE_ENABLED
#include "mic16_irq_capture_site.h"
#include "mic16_network.h"
#include "mic16_av_policy.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xscugic_hw.h"
#include "xaxidma_hw.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xtime_l.h"
#include <string.h>
#define GUARD 0x35e271a9u
#define RAW_BYTES (3u*M16_IRQ_PACKETS*M16_IRQ_BYTES)
#if XPAR_AXI_DMA_0_INCLUDE_SG || XPAR_AXI_DMA_0_INCLUDE_S2MM_DRE
#error Continuous capture requires the checked simple S2MM/no-DRE design
#endif
static volatile unsigned wiring_approved=M16_IRQ_STATUS_WIRING_APPROVED;
#if defined(MIC16_IRQ_CAPTURE_HOST_TEST)
#if defined(__arm__)
#error Host mocks must never be enabled in an ARM binary
#endif
extern uint64_t m16_irq_test_ticks(void);
extern unsigned m16_irq_test_lock(void);
extern void m16_irq_test_unlock(unsigned);
extern void m16_irq_test_barrier(void);
static uint64_t ticks(void){return m16_irq_test_ticks();}
static unsigned lock(void){return m16_irq_test_lock();}
static void unlock(unsigned x){m16_irq_test_unlock(x);}
static void barrier(void){m16_irq_test_barrier();}
#else
static uint64_t ticks(void){XTime t;XTime_GetTime(&t);return t;}
static unsigned lock(void){unsigned x;__asm__ volatile("mrs %0,cpsr\n\tcpsid i\n\tdmb sy":"=r"(x)::"memory");return x;}
static void unlock(unsigned x){__asm__ volatile("dmb sy":::"memory");if(!(x&128u))__asm__ volatile("cpsie i":::"memory");}
static void barrier(void){__asm__ volatile("dmb sy":::"memory");}
#endif
static void source_stop(void){Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,0);barrier();}
static void fast_stop(mic16_irq_capture_arm *c,int reason){
 (void)mic16_irq_ring_abort(&c->ring,reason);c->first_bad_pl=c->last_pl;
 source_stop();XAxiDma_IntrDisable(&c->dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
 /* 只暂停生产者和本设备IRQ，不在ISR中等待reset，更不调用网络/printf。 */
}
static int arm_buffer(mic16_irq_capture_arm *c){
 UINTPTR address=(UINTPTR)c->buffers[c->bounce].payload;
 if(!m16_av_audio_dma_destination((uint32_t)address,8192u))return -1;
 return XAxiDma_SimpleTransfer(&c->dma,address,8192u,XAXIDMA_DEVICE_TO_DMA)==XST_SUCCESS?0:-1;
}
static int guards(const mic16_irq_dma_buffer *b){unsigned i;for(i=0;i<16;i++)if(b->before[i]!=GUARD || b->after[i]!=GUARD)return 0;return 1;}
void mic16_irq_capture_arm_handler(void *context){
 mic16_irq_capture_arm *c=context;UINTPTR base=XPAR_AXI_DMA_0_BASEADDR+XAXIDMA_RX_OFFSET;
 uint64_t begin=ticks(),end;uint32_t irq;unsigned slot,packet;int rc;mic16_irq_dma_buffer *b;
 if(!c || !c->registered)return;
 irq=XAxiDma_IntrGetIrq(&c->dma,XAXIDMA_DEVICE_TO_DMA);
 c->last_sr=XAxiDma_ReadReg(base,XAXIDMA_SR_OFFSET);
 c->last_actual=XAxiDma_ReadReg(base,XAXIDMA_BUFFLEN_OFFSET);
 c->last_pl=(Xil_In32(XPAR_AXI_GPIO_2_BASEADDR)>>2)&3u;
 XAxiDma_IntrAckIrq(&c->dma,irq,XAXIDMA_DEVICE_TO_DMA);
 c->irq_count++;
 if(c->last_irq_tick && begin-c->last_irq_tick>c->max_irq_gap_ticks)c->max_irq_gap_ticks=begin-c->last_irq_tick;
 c->last_irq_tick=begin;
 if(c->ring.failure || c->ring.done)goto done;
 rc=mic16_irq_packet_valid(c->last_sr,c->last_actual,c->last_pl);
 if(rc==M16_IRQ_SPURIOUS){c->ring.spurious++;goto done;}
 if(rc){fast_stop(c,rc);goto done;}
 b=c->buffers+c->bounce;
 /* 所有区间64字节对齐，不误刷新其它所有者。8192字节DMA容量与旧诊断一致，
  * 正常只失效已完成的1024字节及前后保护区，不在ISR清扫整窗。 */
 Xil_DCacheInvalidateRange((INTPTR)b->before,64);
 Xil_DCacheInvalidateRange((INTPTR)b->after,64);
 Xil_DCacheInvalidateRange((INTPTR)b->payload,1024);
 if(!guards(b)){fast_stop(c,M16_IRQ_GUARD);goto done;}
 if(c->warmup)c->warmup--;
 else{
  rc=mic16_irq_ring_destination(&c->ring,&slot,&packet);
  if(rc){fast_stop(c,rc);goto done;}
  memcpy(c->raw+((size_t)slot*M16_IRQ_PACKETS+packet)*1024u,b->payload,1024u);
  barrier();
  rc=mic16_irq_ring_complete(&c->ring,c->last_sr,c->last_actual,c->last_pl,begin);
  if(rc==M16_IRQ_DONE){source_stop();XAxiDma_IntrDisable(&c->dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);goto done;}
  if(rc){fast_stop(c,rc);goto done;}
 }
 c->bounce^=1u;
 if(arm_buffer(c))fast_stop(c,M16_IRQ_DMA);
done:
 end=ticks();if(end>=begin && end-begin>c->max_handler_ticks)c->max_handler_ticks=end-begin;
}
int mic16_irq_capture_arm_open(mic16_irq_capture_arm *c,unsigned char *raw,unsigned bytes,uint64_t windows){
 XAxiDma_Config *cfg;XScuGic_Config *gic;unsigned state,i,j;uint64_t begin;
 if(!c || !raw || (UINTPTR)c%64u || (UINTPTR)raw%64u || bytes!=RAW_BYTES ||
  c->ring.magic || !g_m16_network_stats.initialized || g_m16_network_stats.failed)return M16_IRQ_INVALID;
 if(!wiring_approved)return M16_IRQ_NOT_WIRED;
 /* 仅核验地址范围；raw由CPU使用，不能与本设备或相机保留区重叠。 */
 if((UINTPTR)raw>0x10000000u-RAW_BYTES ||
    !((UINTPTR)raw+RAW_BYTES<=(UINTPTR)c || (UINTPTR)c+sizeof(*c)<=(UINTPTR)raw))return M16_IRQ_INVALID;
 source_stop();Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR+4,0);Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR+4,0);
 Xil_Out32(XPAR_AXI_GPIO_1_BASEADDR,20);
 Xil_Out32(XPAR_AXI_GPIO_2_BASEADDR+4,15); /* 低两位按钮保留，高两位只读音频状态。 */
 cfg=XAxiDma_LookupConfig(XPAR_AXI_DMA_0_DEVICE_ID);gic=XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
 if(!cfg || !gic || XAxiDma_CfgInitialize(&c->dma,cfg)!=XST_SUCCESS || XAxiDma_HasSg(&c->dma))return M16_IRQ_DMA;
 XAxiDma_IntrDisable(&c->dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);XAxiDma_Reset(&c->dma);begin=ticks();
 while(!XAxiDma_ResetIsDone(&c->dma))if(ticks()-begin>COUNTS_PER_SECOND/10u){c->reset_failed=1;return M16_IRQ_DMA;}
 c->raw=raw;c->warmup=M16_IRQ_WARMUP_PACKETS;
 for(i=0;i<2;i++){
  for(j=0;j<16;j++)c->buffers[i].before[j]=c->buffers[i].after[j]=GUARD;
  memset(c->buffers[i].payload,0xa5,8192);Xil_DCacheFlushRange((INTPTR)&c->buffers[i],sizeof(c->buffers[i]));
 }
 if(mic16_irq_ring_init(&c->ring,windows,ticks()))return M16_IRQ_INVALID;
 state=lock();
 /* 不调用DeviceInitialize/CfgInitialize重置GIC，保留GEM及已有中断处理表。 */
 XScuGic_DisableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
 XScuGic_RegisterHandler(gic->CpuBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,
  (Xil_InterruptHandler)mic16_irq_capture_arm_handler,c);
 XScuGic_SetPriTrigTypeByDistAddr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,0x80,1);
 c->registered=1;
 XAxiDma_IntrAckIrq(&c->dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
 XAxiDma_IntrEnable(&c->dma,(XAXIDMA_IRQ_IOC_MASK|XAXIDMA_IRQ_ERROR_MASK),XAXIDMA_DEVICE_TO_DMA);
 XScuGic_EnableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
 Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR,1);
 if(arm_buffer(c)){fast_stop(c,M16_IRQ_DMA);unlock(state);return M16_IRQ_DMA;}
 unlock(state);return 0;
}
/* 只检查健康状态，不取走就绪窗，供计算中的安全检查点使用。 */
int mic16_irq_capture_arm_status(mic16_irq_capture_arm *c){
 unsigned state;uint64_t n,last;int rc;
 if(!c || !c->ring.magic)return M16_IRQ_INVALID;
 state=lock();
 if(c->registered && !c->ring.failure && !c->ring.done && !c->reset_started){
  n=ticks();last=c->last_irq_tick?c->last_irq_tick:c->ring.last_tick;
  if(n<last)fast_stop(c,M16_IRQ_CLOCK);
  else if(n-last>COUNTS_PER_SECOND/10u)fast_stop(c,M16_IRQ_TIMEOUT);
 }
 rc=c->ring.failure;unlock(state);return rc;
}
int mic16_irq_capture_arm_take(mic16_irq_capture_arm *c,mic16_irq_lease *lease){
 int rc;unsigned state;if(!c || !lease)return M16_IRQ_INVALID;
 rc=mic16_irq_capture_arm_status(c);if(rc)return rc;
 state=lock();rc=mic16_irq_ring_take(&c->ring,lease);unlock(state);return rc;
}
const unsigned char *mic16_irq_capture_arm_packet(mic16_irq_capture_arm *c,const mic16_irq_lease *lease,unsigned packet){
 mic16_irq_slot *s;unsigned state;const unsigned char *p=0;
 if(!c || !lease || lease->slot>=3 || packet>=2560)return 0;
 state=lock();s=c->ring.slots+lease->slot;
 if(s->state==M16_IRQ_READING && s->generation==lease->generation && s->window==lease->window)
  p=c->raw+((size_t)lease->slot*2560+packet)*1024;
 unlock(state);return p; /* 租约释放前ISR不会再写此槽。 */
}
int mic16_irq_capture_arm_release(mic16_irq_capture_arm *c,const mic16_irq_lease *lease){
 int rc;unsigned state;if(!c || !lease)return M16_IRQ_INVALID;
 state=lock();rc=mic16_irq_ring_release(&c->ring,lease);unlock(state);return rc;
}
int mic16_irq_capture_arm_stop(mic16_irq_capture_arm *c){
 unsigned state;XScuGic_Config *gic;
 if(!c)return 0;
 if(c->stopped)return 1;
 if(!c->ring.magic)return !c->reset_failed;
 if(!c->reset_started){
  state=lock();source_stop();XAxiDma_IntrDisable(&c->dma,XAXIDMA_IRQ_ALL_MASK,XAXIDMA_DEVICE_TO_DMA);
  gic=XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
  if(gic)XScuGic_DisableIntr(gic->DistBaseAddress,XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);
  XAxiDma_Reset(&c->dma);c->reset_tick=ticks();c->reset_started=1;unlock(state);
 }
 if(!XAxiDma_ResetIsDone(&c->dma)){
  if(ticks()-c->reset_tick>COUNTS_PER_SECOND/10u)c->reset_failed=1;
  (void)mic16_irq_ring_quiesced(&c->ring,0);return 0;
 }
 state=lock();(void)mic16_irq_ring_quiesced(&c->ring,1);c->stopped=1;unlock(state);return 1;
}
#endif
