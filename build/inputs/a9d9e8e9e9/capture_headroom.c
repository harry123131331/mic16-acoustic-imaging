#include "mic16_window_capture.h"
#include <string.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d574344u
#define NONE 3u
#define CR 0x30u
#define SR 0x34u
#define DEST 0x48u
#define LENGTH 0x58u
#define RESET 4u
#define ERRORS 0x4770u
#define IOC 0x1000u
#define IRQ_BITS 0x7000u
#define CONTROL 0x5001u
#define GUARD 0x43e17a95u
volatile uint64_t m16_capture_nominal_exceedances;
volatile uint32_t m16_capture_peak_abs,m16_capture_nominal_channel_mask;
static int valid(const m16_window_capture *c){return c && c->magic==MAGIC;}
/* 每个对象的包长在init后固定；小帧和整窗使用同一所有权/IRQ实现。 */
static unsigned packet_samples(const m16_window_capture *c){return c->config.packet_samples;}
static unsigned packet_bytes(const m16_window_capture *c){return packet_samples(c)*64u;}
static unsigned storage_bytes(const m16_window_capture *c){return packet_bytes(c)+128u;}
static int apart(const void *a,size_t an,const void *b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int fenv_ok(void){
#if defined(__arm__)
 unsigned s;__asm__ volatile("vmrs %0,fpscr":"=r"(s));return !(s&0x01c00000u);
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&0x8040u)return 0;
#endif
 return 1;
#endif
}
static unsigned char *storage(m16_window_capture *c,unsigned i){return c->config.storage[i];}
static unsigned char *payload(m16_window_capture *c,unsigned i){return storage(c,i)+64;}
static uint32_t read32(const unsigned char *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void put32(unsigned char *b,uint32_t n){b[0]=(unsigned char)n;b[1]=(unsigned char)(n>>8);b[2]=(unsigned char)(n>>16);b[3]=(unsigned char)(n>>24);}
static uint32_t rd(m16_window_capture *c,unsigned off){return c->io.read(c->io.context,off);}
static void wr(m16_window_capture *c,unsigned off,uint32_t n){c->io.write(c->io.context,off,n);}
/* 调用方已持有IRQ锁，或本身就是IRQ；复位期间仍保留DMA槽所有权。 */
static void stop_locked(m16_window_capture *c,int reason,uint64_t now){
 if(reason && !c->failure){
  c->failure=reason;c->fault_sr=rd(c,SR);c->fault_cr=rd(c,CR);
  c->fault_length=rd(c,LENGTH);c->fault_frontend=c->io.frontend_status(c->io.context);
 }
 if(c->phase==M16_WC_DRAINING || c->phase==M16_WC_QUARANTINE || c->phase==M16_WC_STOPPED)return;
 c->io.source_enable(c->io.context,0);c->io.barrier(c->io.context);
 wr(c,CR,RESET);c->reset_started=1;c->reset_tick=now;c->phase=M16_WC_DRAINING;
}
static int clock_locked(m16_window_capture *c,uint64_t now){
 if(now<c->last_tick){stop_locked(c,M16_WC_CLOCK,c->last_tick);return 0;}
 c->last_tick=now;return 1;
}
static int arm_locked(m16_window_capture *c,unsigned i,uint64_t now){
 m16_window_capture_slot *s=c->slots+i;
 if(c->generation==UINT64_MAX || (!c->warmup_left && c->next_sample>UINT64_MAX-packet_samples(c))){
  stop_locked(c,M16_WC_COUNTER,now);return M16_WC_COUNTER;
 }
 if(s->state!=M16_WC_ARMED || c->active!=NONE || (rd(c,CR)&RESET) || !(rd(c,SR)&3u)){
  stop_locked(c,M16_WC_DMA_ERROR,now);return M16_WC_DMA_ERROR;
 }
 s->generation=++c->generation;s->discard=c->warmup_left!=0;s->first_sample=c->next_sample;
 if(!s->discard)c->next_sample+=packet_samples(c);
 s->received_tick=0;s->state=M16_WC_DMA;c->active=i;c->next_slot=(i+1u)%3u;c->transfer_tick=now;
 /* 此BD是32位地址，不写仅在更宽地址配置中实现的DA_MSB寄存器。 */
 wr(c,SR,IRQ_BITS);wr(c,CR,CONTROL);wr(c,DEST,c->config.physical[i]+64u);
 c->io.barrier(c->io.context);wr(c,LENGTH,packet_bytes(c));return 0;
}
int m16_window_capture_init(m16_window_capture *c,const m16_window_capture_io *io,const m16_window_capture_config *cfg){
 unsigned i,j,mask=0,cfg_storage;uint64_t now;
 if(!c || !io || !cfg || c->magic || (uintptr_t)c%_Alignof(m16_window_capture) ||
  (uintptr_t)io%_Alignof(m16_window_capture_io) || (uintptr_t)cfg%_Alignof(m16_window_capture_config) ||
  !apart(c,sizeof(*c),io,sizeof(*io)) || !apart(c,sizeof(*c),cfg,sizeof(*cfg)) ||
  !io->read || !io->write || !io->frontend_status || !io->source_enable || !io->ticks ||
  !io->clean || !io->invalidate || !io->lock || !io->unlock || !io->barrier ||
  cfg->scale_exponent>31u || !cfg->transfer_timeout_ticks || !cfg->reset_timeout_ticks ||
  (cfg->packet_samples!=M16_WC_SAMPLES&&cfg->packet_samples!=M16_WC_FRAME_SAMPLES) || cfg->dma_length_bits<22 || cfg->dma_length_bits>26 ||
  cfg->fifo_streaming!=1 || cfg->local_fifo_reset!=1)return M16_WC_INVALID;
 cfg_storage=cfg->packet_samples*64u+128u;
 for(i=0;i<16;i++){
  unsigned wire=cfg->logical_to_wire[i];if(wire>=16 || (mask&(1u<<wire)))return M16_WC_INVALID;mask|=1u<<wire;
 }
 for(i=0;i<3;i++){
  if(!cfg->storage[i] || (uintptr_t)cfg->storage[i]%64 || cfg->physical[i]%64 || cfg->physical[i]<0x100000u ||
   cfg->physical[i]>0x10000000u-cfg_storage || !apart(c,sizeof(*c),cfg->storage[i],cfg_storage) ||
   !apart(io,sizeof(*io),cfg->storage[i],cfg_storage) || !apart(cfg,sizeof(*cfg),cfg->storage[i],cfg_storage))return M16_WC_INVALID;
  for(j=0;j<i;j++)if(!apart(cfg->storage[i],cfg_storage,cfg->storage[j],cfg_storage) ||
   !((uint64_t)cfg->physical[i]+cfg_storage<=cfg->physical[j] ||
      (uint64_t)cfg->physical[j]+cfg_storage<=cfg->physical[i]))return M16_WC_INVALID;
 }
 if((io->read(io->context,CR)&5u) || !(io->read(io->context,SR)&1u) || (io->read(io->context,SR)&ERRORS))return M16_WC_BUSY;
 now=io->ticks(io->context);*c=(m16_window_capture){0};c->io=*io;c->config=*cfg;
 c->magic=MAGIC;c->active=NONE;c->phase=M16_WC_PREPARING;c->last_tick=now;
 io->source_enable(io->context,0);io->barrier(io->context);return 0;
}
/* 主循环最多推进一个CPU片段，不在中断清扫整窗。IRQ锁保护槽状态及64位序号。 */
int m16_window_capture_step(m16_window_capture *c){
 uintptr_t key;unsigned i,j,n;uint64_t now;int rc=1;
 if(!valid(c))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);now=c->io.ticks(c->io.context);
 if(c->phase==M16_WC_QUARANTINE){rc=c->failure;goto done;}
 if(!clock_locked(c,now) && c->phase!=M16_WC_DRAINING){rc=c->failure;goto done;}
 if(c->phase==M16_WC_DRAINING){
  if(!(rd(c,CR)&RESET) && (rd(c,SR)&1u)){
   /* reset已完成且halt；此时才可丢弃未借用的旧窗和部分DMA窗。 */
   for(i=0;i<3;i++)if(c->slots[i].state!=M16_WC_READING)c->slots[i].state=M16_WC_FREE;
   c->active=NONE;c->phase=M16_WC_STOPPED;rc=c->failure?c->failure:2;
  }else if(now<c->reset_tick || now-c->reset_tick>c->config.reset_timeout_ticks){
   if(!c->failure)c->failure=M16_WC_TIMEOUT;
   c->phase=M16_WC_QUARANTINE;rc=c->failure;
  }
  goto done;
 }
 if(c->phase==M16_WC_STOPPED){rc=c->failure?c->failure:2;goto done;}
 if(c->phase==M16_WC_RUNNING){
  uint32_t status=c->io.frontend_status(c->io.context);
  if(status&1u){stop_locked(c,M16_WC_OVERFLOW,now);goto done;}
  if(!(status&2u)){stop_locked(c,M16_WC_CLOCK,now);goto done;}
  if(c->active==NONE){stop_locked(c,M16_WC_DMA_ERROR,now);goto done;}
  if(now-c->transfer_tick>c->config.transfer_timeout_ticks){stop_locked(c,M16_WC_TIMEOUT,now);goto done;}
 }
 /* 优先使已完成窗对CPU可读，再维护已经归还的槽。 */
 {
  unsigned oldest=NONE;
  for(i=0;i<3;i++)if((c->slots[i].state==M16_WC_DONE || c->slots[i].state==M16_WC_INVALIDATE || c->slots[i].state==M16_WC_NORMALIZE) &&
   (oldest==NONE || c->slots[i].generation<c->slots[oldest].generation))oldest=i;
  i=oldest;
 }
 if(i<3){
  m16_window_capture_slot *s=c->slots+i;
  if(s->state==M16_WC_DONE){s->state=M16_WC_INVALIDATE;s->cursor=0;goto done;}
  if(s->state==M16_WC_INVALIDATE){
   n=storage_bytes(c)-s->cursor;if(n>1024)n=1024;
   c->io.invalidate(c->io.context,storage(c,i)+s->cursor,n);s->cursor+=n;
   if(s->cursor==storage_bytes(c)){
    for(j=0;j<16;j++)if(read32(storage(c,i)+4*j)!=GUARD || read32(storage(c,i)+64+packet_bytes(c)+4*j)!=GUARD){stop_locked(c,M16_WC_GUARD,now);goto done;}
    s->cursor=0;s->state=s->discard?M16_WC_FREE:M16_WC_NORMALIZE;
   }
   goto done;
  }
  if(!fenv_ok()){stop_locked(c,M16_WC_FENV,now);goto done;}
  /* 每个采样组先读齐16路，再按通道排列原地覆盖，防止交换通道时读到float位型。 */
  for(n=0;n<4 && s->cursor<packet_samples(c);n++,s->cursor++){
   double values[16],divisor=(double)((uint64_t)1u<<c->config.scale_exponent);float output[16];
   unsigned char *base=payload(c,i)+s->cursor*64u;
   for(j=0;j<16;j++){
    uint32_t raw=read32(base+4*j);values[j]=raw<=0x7fffffffu?(double)raw:(double)raw-4294967296.0;
    /* 已核对的DC阻断和补偿滤波界小于4倍标称CIC增益；不改除数或截断样本。 */
    if(values[j]>divisor || values[j]<-divisor){
     uint32_t magnitude=(uint32_t)(values[j]<0?-values[j]:values[j]);
     m16_capture_nominal_exceedances++;m16_capture_nominal_channel_mask|=1u<<j;
     if(magnitude>m16_capture_peak_abs)m16_capture_peak_abs=magnitude;
    }
    if(values[j]>4.*divisor || values[j]<-4.*divisor){stop_locked(c,M16_WC_RANGE,now);goto done;}
   }
   for(j=0;j<16;j++)output[j]=(float)(values[c->config.logical_to_wire[j]]/divisor);
   memcpy(base,output,sizeof(output));
  }
  if(s->cursor==packet_samples(c))s->state=M16_WC_PCM;
  goto done;
 }
 /* 正常收尾只处理已完成窗，不为已停的输入清扫新的空槽。 */
 if(c->phase==M16_WC_FINISHING)goto done;
 for(i=0;i<3;i++)if(c->slots[i].state==M16_WC_FREE || c->slots[i].state==M16_WC_CLEAN)break;
 if(i<3){
  m16_window_capture_slot *s=c->slots+i;
  if(s->state==M16_WC_FREE){
   for(j=0;j<16;j++){put32(storage(c,i)+4*j,GUARD);put32(storage(c,i)+64+packet_bytes(c)+4*j,GUARD);}
   s->state=M16_WC_CLEAN;s->cursor=0;goto done;
  }
  n=storage_bytes(c)-s->cursor;if(n>1024)n=1024;
  c->io.clean(c->io.context,storage(c,i)+s->cursor,n);s->cursor+=n;
  if(s->cursor==storage_bytes(c)){c->io.barrier(c->io.context);s->state=M16_WC_ARMED;}
  goto done;
 }
 if(c->phase==M16_WC_PREPARING){
  for(i=0;i<3;i++)if(c->slots[i].state!=M16_WC_ARMED)goto done;
  c->phase=M16_WC_READY;
 }
 if(c->phase==M16_WC_READY)rc=0;
done:c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_start(m16_window_capture *c,unsigned epoch){
 uintptr_t key;uint64_t now;int rc;
 if(!valid(c) || !epoch || epoch<=c->epoch)return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_READY){rc=M16_WC_BUSY;goto done;}
 now=c->io.ticks(c->io.context);if(!clock_locked(c,now)){rc=c->failure;goto done;}
 c->epoch=epoch;c->warmup_left=c->config.warmup_windows;c->next_sample=0;c->next_take_sample=0;c->finish_requested=0;c->finish_sample=0;c->phase=M16_WC_RUNNING;
 rc=arm_locked(c,0,now);
 if(!rc){c->io.source_enable(c->io.context,1);c->io.barrier(c->io.context);}
done:c->io.unlock(c->io.context,key);return rc;
}
void m16_window_capture_irq(m16_window_capture *c){
 uint32_t sr,status;uint64_t now;unsigned i,j;
 if(!valid(c))return;
 /* 中断入口已由CPU/GIC串行化；前台所有状态访问须经同一IRQ锁。 */
 sr=rd(c,SR);c->irqs++;now=c->io.ticks(c->io.context);
 if(c->phase!=M16_WC_RUNNING){wr(c,SR,sr&IRQ_BITS);return;}
 if(!clock_locked(c,now))return;
 if(now-c->transfer_tick>c->config.transfer_timeout_ticks){stop_locked(c,M16_WC_TIMEOUT,now);return;}
 status=c->io.frontend_status(c->io.context);
 if(status&1u){stop_locked(c,M16_WC_OVERFLOW,now);return;}
 if(!(status&2u)){stop_locked(c,M16_WC_CLOCK,now);return;}
 if(sr&ERRORS){stop_locked(c,M16_WC_DMA_ERROR,now);return;}
 if(!(sr&IOC)){c->spurious++;wr(c,SR,sr&IRQ_BITS);return;}
 if(c->active==NONE || !(sr&2u)){stop_locked(c,M16_WC_DMA_ERROR,now);return;}
 if(rd(c,LENGTH)!=packet_bytes(c)){stop_locked(c,M16_WC_LENGTH,now);return;}
 i=c->active;c->active=NONE;c->slots[i].received_tick=now;c->slots[i].state=M16_WC_DONE;c->completed++;
 if(c->slots[i].discard){c->warmup_left--;c->warmup_completed++;}
 if(c->finish_requested||(!c->slots[i].discard&&c->finish_sample&&c->slots[i].first_sample+packet_samples(c)==c->finish_sample)){
  /* IOC及IDLE已验证；不再写下一次BTT，保留完成槽直到前台实际消费。 */
  wr(c,SR,sr&IRQ_BITS);c->io.source_enable(c->io.context,0);c->io.barrier(c->io.context);
  c->phase=M16_WC_FINISHING;return;
 }
 /* 仅续接已经完成缓存准备的独立槽；绝不覆盖PCM/计算租约或在此复制整窗。 */
 for(j=0;j<3;j++){i=(c->next_slot+j)%3u;if(c->slots[i].state==M16_WC_ARMED)break;}
 if(j==3){stop_locked(c,M16_WC_OVERFLOW,now);return;}
 (void)arm_locked(c,i,now);
}
int m16_window_capture_take(m16_window_capture *c,m16_window_capture_lease *out){
 uintptr_t key;unsigned i,index=NONE;int rc=M16_WC_EMPTY;
 if(!valid(c) || !out || (uintptr_t)out%_Alignof(m16_window_capture_lease) || !apart(c,sizeof(*c),out,sizeof(*out)))return M16_WC_INVALID;
 for(i=0;i<3;i++)if(!apart(out,sizeof(*out),storage(c,i),storage_bytes(c)))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_RUNNING && c->phase!=M16_WC_FINISHING){rc=c->failure?c->failure:M16_WC_STOPPING;goto done;}
 for(i=0;i<3;i++)if(c->slots[i].state==M16_WC_PCM && c->slots[i].first_sample==c->next_take_sample)index=i;
 if(index!=NONE){
  m16_window_capture_slot *s=c->slots+index;s->state=M16_WC_READING;
  *out=(m16_window_capture_lease){index,c->epoch,s->generation,s->first_sample,s->received_tick,(const float *)payload(c,index)};
  c->next_take_sample+=packet_samples(c);c->delivered++;rc=0;
 }
done:c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_release(m16_window_capture *c,const m16_window_capture_lease *lease){
 uintptr_t key;int rc=M16_WC_LEASE;m16_window_capture_slot *s;
 if(!valid(c) || !lease || (uintptr_t)lease%_Alignof(m16_window_capture_lease) || !apart(c,sizeof(*c),lease,sizeof(*lease)))return M16_WC_INVALID;
 if(lease->slot>=3)return M16_WC_LEASE;
 key=c->io.lock(c->io.context);s=c->slots+lease->slot;
 if(s->state==M16_WC_READING && lease->epoch==c->epoch && lease->generation==s->generation &&
  lease->first_sample==s->first_sample && lease->received_tick==s->received_tick && lease->pcm==(const float *)payload(c,lease->slot)){
  s->state=M16_WC_FREE;rc=0;
 }
 c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_request_finish_aligned(m16_window_capture *c,uint64_t context,uint64_t *end){
 uintptr_t key;uint64_t units,target;int rc=0;
 if(!valid(c)||!end||!context||context%packet_samples(c)||!apart(c,sizeof(*c),end,sizeof(*end)))return M16_WC_INVALID;
 for(unsigned i=0;i<3;i++)if(!apart(end,sizeof(*end),storage(c,i),storage_bytes(c)))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_RUNNING||c->finish_requested||c->finish_sample)rc=M16_WC_BUSY;
 else{
  units=c->next_sample/context+(c->next_sample%context!=0);if(!units)units=1;
  if(units>UINT64_MAX/context)rc=M16_WC_COUNTER;
  else{target=units*context;c->finish_sample=target;*end=target;}
 }
 c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_request_finish_at(m16_window_capture *c,uint64_t end){
 uintptr_t key;int rc=0;
 if(!valid(c)||!end||end%packet_samples(c))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_RUNNING||c->finish_requested||c->finish_sample)rc=M16_WC_BUSY;
 else if(end<c->next_sample)rc=M16_WC_COUNTER;
 else c->finish_sample=end;
 c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_request_finish(m16_window_capture *c){
 uintptr_t key;int rc=0;uint64_t now;
 if(!valid(c))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_RUNNING && c->phase!=M16_WC_FINISHING){rc=M16_WC_BUSY;goto done;}
 now=c->io.ticks(c->io.context);if(!clock_locked(c,now)){rc=c->failure;goto done;}
 c->finish_requested=1;c->io.barrier(c->io.context);
done:c->io.unlock(c->io.context,key);return rc;
}
int m16_window_capture_input_complete(const m16_window_capture *c){
 uintptr_t key;unsigned i;int complete=0;
 if(!valid(c))return 0;
 key=c->io.lock(c->io.context);
 if(c->phase!=M16_WC_FINISHING || c->active!=NONE || c->failure || c->next_take_sample!=c->next_sample)goto done;
 for(i=0;i<3;i++)if(c->slots[i].state==M16_WC_DONE || c->slots[i].state==M16_WC_INVALIDATE ||
  c->slots[i].state==M16_WC_NORMALIZE || c->slots[i].state==M16_WC_PCM || c->slots[i].state==M16_WC_DMA)goto done;
 complete=1;
done:c->io.unlock(c->io.context,key);return complete;
}
int m16_window_capture_stop(m16_window_capture *c){
 uintptr_t key;uint64_t now;if(!valid(c))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);now=c->io.ticks(c->io.context);
 if(clock_locked(c,now))stop_locked(c,0,now);
 c->io.unlock(c->io.context,key);return 0;
}
int m16_window_capture_releasable(const m16_window_capture *c){
 unsigned i;if(!valid(c) || c->phase!=M16_WC_STOPPED || c->active!=NONE)return 0;
 for(i=0;i<3;i++)if(c->slots[i].state==M16_WC_READING || c->slots[i].state==M16_WC_DMA)return 0;
 return 1;
}
int m16_window_capture_restart(m16_window_capture *c){
 uintptr_t key;unsigned i;int rc=M16_WC_BUSY;
 if(!valid(c))return M16_WC_INVALID;
 key=c->io.lock(c->io.context);
 if(m16_window_capture_releasable(c) && !(rd(c,CR)&5u) && (rd(c,SR)&1u)){
  for(i=0;i<3;i++)c->slots[i]=(m16_window_capture_slot){0};
  c->failure=0;c->reset_started=0;c->finish_requested=0;c->finish_sample=0;c->next_slot=0;c->phase=M16_WC_PREPARING;rc=0;
 }
 c->io.unlock(c->io.context,key);return rc;
}
