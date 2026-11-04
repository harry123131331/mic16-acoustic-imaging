/* 有效载荷用窗口标签模拟；验证采集先于CPU服务、借用期间不覆盖及异常优先级。 */
#include "mic16_irq_ring.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static unsigned checks,errors,scenarios;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("IRQ_RING_ERROR line=%u scenario=%u\n",(unsigned)__LINE__,scenarios);}}while(0)
static mic16_irq_ring ring;
static unsigned payload[3][2560];
static uint64_t tick;
static void init(unsigned limit){scenarios++;memset(&ring,0,sizeof(ring));tick=0;CHECK(!mic16_irq_ring_init(&ring,limit,0));}
static int complete_window(unsigned window){
 unsigned i,s,p;int rc=0;
 for(i=0;i<2560;i++){
  CHECK(!mic16_irq_ring_destination(&ring,&s,&p));CHECK(p==i);
  CHECK(ring.slots[s].state==M16_IRQ_FILLING && ring.slots[s].window==window);
  payload[s][p]=window*2560+p;
  rc=mic16_irq_ring_complete(&ring,0x1002,1024,2,++tick);
  if(i<2559)CHECK(!rc);
 }
 return rc;
}
static void check_payload(const mic16_irq_lease *l){unsigned p;for(p=0;p<2560;p++)CHECK(payload[l->slot][p]==l->window*2560+p);}
int main(void){
 mic16_irq_lease a,b,c,old;unsigned i,s,p;int rc;
 init(2000);
 for(i=0;i<2000;i++){
  rc=complete_window(i);CHECK(rc==(i==1999?M16_IRQ_DONE:0));
  CHECK(!mic16_irq_ring_take(&ring,&a) && a.window==i);check_payload(&a);CHECK(!mic16_irq_ring_release(&ring,&a));
 }
 CHECK(ring.completed==2000 && ring.packets==5120000 && !ring.overrun_count && ring.peak_used<=2);
 CHECK(mic16_irq_ring_take(&ring,&a)==M16_IRQ_DONE);CHECK(!mic16_irq_ring_quiesced(&ring,1));
 init(0);CHECK(!complete_window(0));CHECK(!mic16_irq_ring_take(&ring,&a));old=a;
 CHECK(!complete_window(1));CHECK(!mic16_irq_ring_take(&ring,&b));
 CHECK(complete_window(2)==M16_IRQ_OVERRUN);check_payload(&a);check_payload(&b);
 CHECK(ring.slots[a.slot].state==M16_IRQ_READING && ring.slots[b.slot].state==M16_IRQ_READING);
 CHECK(mic16_irq_ring_take(&ring,&c)==M16_IRQ_OVERRUN);
 CHECK(!mic16_irq_ring_quiesced(&ring,1));check_payload(&a);CHECK(!mic16_irq_ring_release(&ring,&a));CHECK(!mic16_irq_ring_release(&ring,&b));
 CHECK(mic16_irq_ring_release(&ring,&old)==M16_IRQ_LEASE);
 init(5);CHECK(!complete_window(0));CHECK(!mic16_irq_ring_take(&ring,&a));CHECK(!mic16_irq_ring_release(&ring,&a));old=a;
 CHECK(!complete_window(1));CHECK(!mic16_irq_ring_take(&ring,&b));CHECK(!mic16_irq_ring_release(&ring,&b));
 CHECK(!complete_window(2));CHECK(!mic16_irq_ring_take(&ring,&c));CHECK(mic16_irq_ring_release(&ring,&old)==M16_IRQ_LEASE);CHECK(!mic16_irq_ring_release(&ring,&c));
 scenarios++;
 CHECK(mic16_irq_packet_valid(0x5012,1024,2)==M16_IRQ_DMA);
 CHECK(mic16_irq_packet_valid(0x1002,0,2)==M16_IRQ_LENGTH);
 CHECK(mic16_irq_packet_valid(0x1002,8192,2)==M16_IRQ_LENGTH);
 CHECK(mic16_irq_packet_valid(0x1002,1024,3)==M16_IRQ_PL_OVERFLOW);
 CHECK(mic16_irq_packet_valid(0x1002,1024,0)==M16_IRQ_CLOCK);
 CHECK(mic16_irq_packet_valid(0,1024,2)==M16_IRQ_SPURIOUS);
 CHECK(mic16_irq_packet_valid(0x1000,1024,2)==M16_IRQ_DMA);
 init(0);CHECK(mic16_irq_ring_complete(&ring,0,0,2,10)==M16_IRQ_SPURIOUS && !ring.packet);
 CHECK(mic16_irq_ring_complete(&ring,0x1002,1024,2,9)==M16_IRQ_CLOCK);
 CHECK(mic16_irq_ring_quiesced(&ring,0)==M16_IRQ_STOPPED && ring.quarantined && ring.slots[0].state==M16_IRQ_FILLING);
 CHECK(mic16_irq_ring_destination(&ring,&s,&p)==M16_IRQ_CLOCK);
 CHECK(!mic16_irq_ring_quiesced(&ring,1) && ring.slots[0].state==M16_IRQ_FREE);
 init(0);CHECK(mic16_irq_ring_complete(&ring,0x1002,1024,3,1)==M16_IRQ_PL_OVERFLOW);
 CHECK(mic16_irq_ring_abort(&ring,M16_IRQ_DMA)==M16_IRQ_PL_OVERFLOW);
 CHECK(mic16_irq_ring_complete(&ring,0x1002,1024,2,2)==M16_IRQ_PL_OVERFLOW && !ring.packets);
 init(0);CHECK(mic16_irq_ring_abort(&ring,M16_IRQ_GUARD)==M16_IRQ_GUARD);
 CHECK(mic16_irq_ring_abort(&ring,0)==M16_IRQ_INVALID);
 printf("IRQ_RING_%s scenarios=%u errors=%u windows_fast=2000 checks=%u metadata=%u VIRTUAL_DMA_NOT_BOARD\n",
  errors?"FAIL":"PASS",scenarios,errors,checks,(unsigned)sizeof(ring));return errors?1:0;
}
