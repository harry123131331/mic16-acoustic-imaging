/* 实际原始窗环+实际适配层的虚拟生产者测试，不接硬件。 */
#include "mic16_irq_stream.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static unsigned checks,errors,scenarios;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("IRQ_STREAM_ERROR scenario=%u line=%u\n",scenarios,(unsigned)__LINE__);}}while(0)
typedef struct {
 mic16_irq_ring ring;mic16_irq_stream bridge;
 unsigned open_fail,done,corrupt,stop_wait,stop_calls,release_calls;
 uint64_t tick;
 unsigned char raw[3][2560][1024];
} fixture;
static fixture f;
static int open_(void *p){fixture *a=p;CHECK(!mic16_irq_ring_init(&a->ring,0,++a->tick));return a->open_fail?-901:0;}
static int status_(void *p){return ((fixture *)p)->ring.failure;}
static int take_(void *p,mic16_irq_lease *l){fixture *a=p;int rc;if(a->done)return M16_IRQ_DONE;rc=mic16_irq_ring_take(&a->ring,l);if(!rc&&a->corrupt)l->generation++;return rc;}
static const unsigned char *packet_(void *p,const mic16_irq_lease *l,unsigned k){fixture *a=p;mic16_irq_slot *s=a->ring.slots+l->slot;if(l->slot>=3 || s->state!=M16_IRQ_READING || l->generation!=s->generation)return 0;return a->raw[l->slot][k];}
static int release_(void *p,const mic16_irq_lease *l){fixture *a=p;a->release_calls++;return mic16_irq_ring_release(&a->ring,l);}
static int stop_(void *p){fixture *a=p;a->stop_calls++;if(a->stop_wait){a->stop_wait--;mic16_irq_ring_quiesced(&a->ring,0);return 0;}mic16_irq_ring_quiesced(&a->ring,1);return 1;}
static void init(void){mic16_irq_stream_ops o={&f,open_,status_,take_,packet_,release_,stop_};scenarios++;memset(&f,0,sizeof(f));CHECK(!mic16_irq_stream_init(&f.bridge,&o));}
static int begin(unsigned slot,uint64_t n){mic16_stream_window w={n*40960,(n+1)*40960,0,1,1};return mic16_irq_stream_begin(&f.bridge,slot,&w);}
static int produce(void){unsigned k,s,p;int rc=0;for(k=0;k<2560;k++){
 rc=mic16_irq_ring_destination(&f.ring,&s,&p);if(rc)break;
 f.raw[s][p][0]=(unsigned char)f.ring.completed;f.raw[s][p][1]=(unsigned char)p;
 rc=mic16_irq_ring_complete(&f.ring,0x1002,1024,2,++f.tick);if(rc)break;
 }return rc;}
static void normal(void){unsigned n;init();for(n=0;n<7;n++){
 const unsigned char *p;CHECK(!begin(n%3,n));CHECK(mic16_irq_stream_poll(&f.bridge)==0);CHECK(!produce());
 CHECK(mic16_irq_stream_poll(&f.bridge)==1);p=mic16_irq_stream_packet(&f.bridge,n%3,n,13);CHECK(p&&p[0]==n);
 CHECK(!mic16_irq_stream_release(&f.bridge,n%3));CHECK(!mic16_irq_stream_status(&f.bridge));
 }CHECK(mic16_irq_stream_close(&f.bridge)==1);CHECK(f.ring.taken==7 && f.release_calls==7);}
static void independent(void){const unsigned char *p;init();CHECK(!begin(0,0));CHECK(!produce());CHECK(mic16_irq_stream_poll(&f.bridge)==1);p=mic16_irq_stream_packet(&f.bridge,0,0,0);CHECK(p&&p[0]==0);
 CHECK(!begin(1,1));CHECK(!produce());CHECK(p&&p[0]==0);CHECK(f.ring.slots[f.bridge.leases[0].slot].state==M16_IRQ_READING);
 CHECK(mic16_irq_stream_poll(&f.bridge)==1);CHECK(!mic16_irq_stream_release(&f.bridge,0));
 CHECK(!begin(2,2));CHECK(!produce());CHECK(mic16_irq_stream_poll(&f.bridge)==1);p=mic16_irq_stream_packet(&f.bridge,2,2,0);CHECK(p&&p[0]==2);
 CHECK(mic16_irq_stream_close(&f.bridge)==1);CHECK(f.release_calls==3);}
static void slow(void){init();CHECK(!begin(0,0));CHECK(!produce());CHECK(mic16_irq_stream_poll(&f.bridge)==1);CHECK(!begin(1,1));CHECK(!produce());CHECK(produce()==M16_IRQ_OVERRUN);
 CHECK(mic16_irq_stream_status(&f.bridge)==M16_IRQ_OVERRUN);CHECK(mic16_irq_stream_stop(&f.bridge)==1);CHECK(f.bridge.held[0] && f.release_calls==0);
 CHECK(mic16_irq_stream_close(&f.bridge)==1);CHECK(!f.bridge.held[0]);}
static void quiescing(void){init();CHECK(!begin(0,0));CHECK(!produce());CHECK(mic16_irq_stream_poll(&f.bridge)==1);f.stop_wait=1;
 CHECK(mic16_irq_stream_close(&f.bridge)==0);CHECK(f.bridge.held[0] && !f.release_calls);CHECK(mic16_irq_stream_close(&f.bridge)==1);CHECK(f.release_calls==1);}
static void open_fail(void){init();f.open_fail=1;CHECK(begin(0,0)==-901);CHECK(f.bridge.opened);CHECK(mic16_irq_stream_close(&f.bridge)==1);CHECK(f.stop_calls==1);}
static void fault(void){init();CHECK(!begin(0,0));mic16_irq_ring_abort(&f.ring,M16_IRQ_PL_OVERFLOW);CHECK(mic16_irq_stream_poll(&f.bridge)==M16_IRQ_PL_OVERFLOW);CHECK(mic16_irq_stream_close(&f.bridge)==1);}
static void bad_lease(void){init();CHECK(!begin(0,0));CHECK(!produce());f.corrupt=1;CHECK(mic16_irq_stream_poll(&f.bridge)==1);CHECK(!mic16_irq_stream_packet(&f.bridge,0,0,0));CHECK(mic16_irq_stream_release(&f.bridge,0)==M16_IRQ_LEASE);CHECK(f.bridge.held[0]);CHECK(mic16_irq_stream_close(&f.bridge)==0);}
static void eof(void){init();CHECK(!begin(0,0));f.done=1;CHECK(mic16_irq_stream_poll(&f.bridge)==M16_IRQ_COUNTER);CHECK(mic16_irq_stream_close(&f.bridge)==1);}
static void counter(void){mic16_stream_window w={1,40961,0,1,1};init();CHECK(mic16_irq_stream_begin(&f.bridge,0,&w)==M16_IRQ_COUNTER);CHECK(!f.bridge.opened);CHECK(mic16_irq_stream_close(&f.bridge)==1);}
static void misuse(void){init();CHECK(!begin(0,0));CHECK(begin(1,0)==M16_IRQ_INVALID);CHECK(!produce());CHECK(mic16_irq_stream_poll(&f.bridge)==1);CHECK(begin(0,1)==M16_IRQ_INVALID);CHECK(!mic16_irq_stream_packet(&f.bridge,0,1,0));CHECK(!mic16_irq_stream_packet(&f.bridge,0,0,2560));CHECK(!mic16_irq_stream_release(&f.bridge,0));CHECK(mic16_irq_stream_release(&f.bridge,0)==M16_IRQ_LEASE);CHECK(mic16_irq_stream_close(&f.bridge)==1);}
int main(void){normal();independent();slow();quiescing();open_fail();fault();bad_lease();eof();counter();misuse();printf("IRQ_STREAM_%s scenarios=%u checks=%u errors=%u metadata=%u VIRTUAL_PRODUCER_NOT_BOARD\n",errors?"FAIL":"PASS",scenarios,checks,errors,(unsigned)sizeof(mic16_irq_stream));return errors?1:0;}
