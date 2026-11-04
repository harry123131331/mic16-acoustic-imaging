/* 缓存/设备内存分离的控制台：模拟DMA寄存器，不冒充真实DDR或ARM计时。 */
#include "mic16_window_capture.h"
#include "mic16_live_input.h"
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fenv.h>
static m16_window_capture capture;
static m16_window_capture_lease leases[3];
static unsigned char *cpu[3],*device[3];
static uint32_t regs[64];
static uint64_t now,maximum_cache,cache_errors,early_invalidates,irq_cache,starts;
static unsigned status,enabled,active,in_irq,locked,source_before_dma;
static m16_window_capture_io ops;
static m16_window_capture_config cfg;
static uint32_t rd(void *unused,unsigned off){(void)unused;return regs[off/4];}
static void wr(void *unused,unsigned off,uint32_t value){
 unsigned i;(void)unused;
 if(off==0x4c)cache_errors++;
 if(off==0x34){regs[off/4]&=~(value&0x7000u);return;}
 regs[off/4]=value;
 if(off==0x30 && value==4)return;
 if(off==0x30 && value==0x5001u)regs[0x34/4]&=~1u;
 if(off!=0x58)return;
 for(i=0;i<3;i++)if(regs[0x48/4]==cfg.physical[i]+64u)break;
 if(i==3 || active!=3 || value!=M16_WC_BYTES || capture.slots[i].state!=M16_WC_DMA)cache_errors++;
 active=i;starts++;regs[0x34/4]=0;
}
static uint32_t frontend(void *unused){(void)unused;return status;}
static void source(void *unused,unsigned on){
 (void)unused;enabled=on;
 if(on && active==3)source_before_dma++;
 status=on?2u:0u;
}
static uint64_t ticks(void *unused){(void)unused;return now;}
static uintptr_t lock(void *unused){(void)unused;locked++;return 0;}
static void unlock(void *unused,uintptr_t key){(void)unused;(void)key;locked--;}
static void barrier(void *unused){(void)unused;}
static void cache(void *p,unsigned n,int invalidate){
 unsigned i;uintptr_t offset=0;
 if(n>maximum_cache)maximum_cache=n;
 if(in_irq)irq_cache++;
 for(i=0;i<3;i++)if((uintptr_t)p>=(uintptr_t)cpu[i] && (uintptr_t)p<(uintptr_t)cpu[i]+M16_WC_STORAGE){offset=(uintptr_t)p-(uintptr_t)cpu[i];break;}
 if(i==3 || (uintptr_t)p%64 || n%64 || n>1024 || offset+n>M16_WC_STORAGE){cache_errors++;return;}
 if(active==i){early_invalidates++;return;}
 if(invalidate)memcpy(cpu[i]+offset,device[i]+offset,n);
 else memcpy(device[i]+offset,cpu[i]+offset,n);
}
static void clean(void *unused,void *p,unsigned n){(void)unused;cache(p,n,0);}
static void invalidate(void *unused,void *p,unsigned n){(void)unused;cache(p,n,1);}
int wc_reset(unsigned scale,unsigned permutation,unsigned warmup,unsigned bad){
 unsigned i;int rc;
 for(i=0;i<3;i++){
  if(!cpu[i])cpu[i]=_aligned_malloc(M16_WC_STORAGE,64);
  if(!device[i])device[i]=_aligned_malloc(M16_WC_STORAGE,64);
  if(!cpu[i] || !device[i])return -999;
  memset(cpu[i],0xa5,M16_WC_STORAGE);memset(device[i],0x5a,M16_WC_STORAGE);
 }
 memset(&capture,0,sizeof(capture));memset(leases,0,sizeof(leases));memset(regs,0,sizeof(regs));
 regs[0x34/4]=1;now=1000;maximum_cache=cache_errors=early_invalidates=irq_cache=starts=0;
 active=3;enabled=0;status=0;in_irq=locked=source_before_dma=0;
 ops=(m16_window_capture_io){0,rd,wr,frontend,source,ticks,clean,invalidate,lock,unlock,barrier};
 memset(&cfg,0,sizeof(cfg));
 for(i=0;i<3;i++){cfg.storage[i]=cpu[i];cfg.physical[i]=0x02000000u+i*0x00400000u;}
 for(i=0;i<16;i++)cfg.logical_to_wire[i]=(unsigned char)((i+permutation)%16);
 cfg.scale_exponent=scale;cfg.warmup_windows=warmup;cfg.transfer_timeout_ticks=2000000;cfg.reset_timeout_ticks=100;
 cfg.packet_samples=40960;cfg.dma_length_bits=23;cfg.fifo_streaming=1;cfg.local_fifo_reset=1;
 if(bad==1)cfg.physical[1]=cfg.physical[0]+64;
 if(bad==2)cfg.storage[1]=cpu[0];
 if(bad==3)cfg.dma_length_bits=14;
 if(bad==4)cfg.logical_to_wire[1]=cfg.logical_to_wire[0];
 if(bad==5)regs[0x34/4]=0;
 if(bad==6)cfg.local_fifo_reset=0;
 if(bad==7)cfg.fifo_streaming=0;
 rc=m16_window_capture_init(&capture,&ops,&cfg);return rc;
}
int wc_step(void){return m16_window_capture_step(&capture);}
int wc_start(unsigned epoch){return m16_window_capture_start(&capture,epoch);}
static int32_t sample(unsigned i,unsigned seed,unsigned exponent){
 uint32_t n=1664525u*(i+seed*1597u)+1013904223u;
 if(exponent==31)return (int32_t)n;
 if(i%37u==0)return -(int32_t)(1u<<exponent);
 if(i%41u==0)return (int32_t)(1u<<exponent);
 return (int32_t)(n%((1u<<exponent)+1u))*(i&1u?1:-1);
}
int wc_complete(unsigned seed,unsigned actual_bytes,unsigned sr,unsigned extra){
 unsigned i,slot=active;int32_t x;
 if(slot>=3)return -998;
 for(i=0;i<M16_WC_VALUES;i++){x=sample(i,seed,cfg.scale_exponent);memcpy(device[slot]+64+4u*i,&x,4);}
 if(extra==1)device[slot][0]^=1;
 if(extra==2){x=(int32_t)((1u<<cfg.scale_exponent)+1u);memcpy(device[slot]+64+400u,&x,4);}
 active=3;regs[0x58/4]=actual_bytes;regs[0x34/4]=sr;
 in_irq=1;m16_window_capture_irq(&capture);in_irq=0;return 0;
}
int wc_take(unsigned id){if(id>=3)return -999;return m16_window_capture_take(&capture,leases+id);}
int wc_release(unsigned id){if(id>=3)return -999;return m16_window_capture_release(&capture,leases+id);}
int wc_stale_release(unsigned id){m16_window_capture_lease s=leases[id];s.generation++;return m16_window_capture_release(&capture,&s);}
int wc_copy(unsigned id,float *out){if(id>=3 || !out || !leases[id].pcm)return -999;memcpy(out,leases[id].pcm,M16_WC_BYTES);return 0;}
int wc_reference(unsigned seed,float *out){
 mic16_live_input live={0};mic16_live_config config;unsigned i;int rc;
 float *reference=calloc(8192u*16u,sizeof(float));unsigned char zero[1024]={0},packet[1024];
 if(!reference)return -999;
 memset(&config,0,sizeof(config));config.scale_exponent=cfg.scale_exponent;config.context_id=1;
 memcpy(config.logical_to_wire,cfg.logical_to_wire,16);
 rc=mic16_live_init(&live,&config,reference,8192u*16u,out,M16_WC_VALUES);if(rc)goto done;
 rc=mic16_live_begin_reference(&live);if(rc)goto done;
 for(i=0;i<512;i++)rc=mic16_live_push(&live,zero,1024,i,1);
 if(rc!=M16_LIVE_REFERENCE_READY)goto done;
 rc=mic16_live_begin_audio(&live);if(rc)goto done;
 for(i=0;i<2560;i++){
  unsigned j;for(j=0;j<256;j++){int32_t x=sample(i*256+j,seed,cfg.scale_exponent);memcpy(packet+4*j,&x,4);}
  rc=mic16_live_push(&live,packet,1024,i,1);
 }
 if(rc==M16_LIVE_WINDOW_READY)rc=0;
done:free(reference);return rc;
}
int wc_stop(void){return m16_window_capture_stop(&capture);}
int wc_restart(void){return m16_window_capture_restart(&capture);}
int wc_releasable(void){return m16_window_capture_releasable(&capture);}
void wc_reset_done(void){regs[0x30/4]=0;regs[0x34/4]=1;active=3;}
void wc_advance(uint64_t ticks_value){now+=ticks_value;}
void wc_clock(uint64_t value){now=value;}
void wc_status(unsigned value){status=value;}
void wc_rounding(int bad){(void)fesetround(bad?FE_DOWNWARD:FE_TONEAREST);}
void wc_spurious(void){in_irq=1;m16_window_capture_irq(&capture);in_irq=0;}
uint64_t wc_metric(unsigned which){
 switch(which){
 case 0:return capture.phase;case 1:return (uint64_t)(int64_t)capture.failure;
 case 2:return capture.active;case 3:return maximum_cache;case 4:return cache_errors;
 case 5:return early_invalidates;case 6:return irq_cache;case 7:return starts;
 case 8:return capture.completed;case 9:return capture.warmup_completed;case 10:return capture.delivered;
 case 11:return capture.next_sample;case 12:return capture.spurious;case 13:return enabled;
 case 14:return capture.epoch;case 15:return source_before_dma;case 16:return locked;
 case 17:return capture.generation;case 18:return now;
 default:if(which>=20 && which<23)return capture.slots[which-20].state;
 if(which>=30 && which<33)return leases[which-30].first_sample;
 if(which>=40 && which<43)return leases[which-40].received_tick;
 return UINT64_MAX;
 }
}
#if defined(M16_CAPTURE_STREAM_TEST)
#include "mic16_capture_stream.h"
static m16_capture_stream feed;
static mic16_stream_scheduler scheduler;
static m16_capture_stream_view views[2];
static mic16_stream_ticket outputs[2];
int wcs_reset(unsigned warmup,uint64_t wait){
 mic16_stream_config config={3,3,1000000,0,20000000,7,9};int rc;
 rc=wc_reset(24,3,warmup,0);if(rc)return rc;
 memset(&feed,0,sizeof(feed));memset(&scheduler,0,sizeof(scheduler));memset(views,0,sizeof(views));memset(outputs,0,sizeof(outputs));
 config.max_input_wait_ticks=wait;rc=mic16_stream_init(&scheduler,&config,9,now);if(rc)return rc;
 return m16_capture_stream_init(&feed,&capture,&scheduler);
}
int wcs_step(void){return m16_capture_stream_step(&feed);}
int wcs_start(void){return m16_capture_stream_start(&feed);}
int wcs_take(unsigned id){if(id>1)return -999;return m16_capture_stream_take(&feed,views+id);}
int wcs_reserve(unsigned id){if(id>1)return -999;return mic16_stream_output_reserve(&scheduler,&views[id].ticket,now,outputs+id);}
int wcs_release(unsigned id){if(id>1)return -999;return m16_capture_stream_release(&feed,views+id);}
int wcs_stale_release(unsigned id){m16_capture_stream_view v=views[id];v.ticket.generation++;return m16_capture_stream_release(&feed,&v);}
int wcs_finish(unsigned id){
 mic16_stream_ticket send;mic16_stream_window window;int rc;
 if(id>1)return -999;
 rc=mic16_stream_output_commit(&scheduler,outputs+id,now);if(rc)return rc;
 rc=mic16_stream_output_acquire(&scheduler,now,&send,&window);if(rc)return rc;
 return mic16_stream_output_finish(&scheduler,&send,now,1);
}
int wcs_cancel(unsigned id){return mic16_stream_output_cancel(&scheduler,outputs+id);}
int wcs_stop(void){return m16_capture_stream_stop(&feed);}
int wcs_restart(unsigned epoch,uint64_t ref){return m16_capture_stream_restart(&feed,epoch,7,ref);}
int wcs_releasable(void){return m16_capture_stream_releasable(&feed);}
int wcs_copy(unsigned id,float *out){if(id>1 || !views[id].pcm)return -999;memcpy(out,views[id].pcm,M16_WC_BYTES);return 0;}
int wcs_receipt_future(void){
 mic16_stream_ticket ticket;int rc=mic16_stream_input_reserve(&scheduler,0,now,&ticket);if(rc)return rc;
 return mic16_stream_input_commit_received(&scheduler,&ticket,now+1,now);
}
uint64_t wcs_metric(unsigned which){
 switch(which){
 case 0:return scheduler.stats.inputs_committed;case 1:return scheduler.stats.inputs_acquired;
 case 2:return (uint64_t)(int64_t)scheduler.failure;case 3:return feed.stopping;
 case 4:return scheduler.stats.outputs_sent;case 5:return scheduler.reference_invalid;
 case 6:return feed.bindings[0].lease.slot;case 7:return scheduler.epoch;
 case 10:return views[0].window.first_sample;case 11:return views[0].window.received_tick;
 case 12:return views[1].window.first_sample;case 13:return views[1].window.received_tick;
 case 14:return views[0].ticket.slot;
 default:return UINT64_MAX;
 }
}
#endif
