#include "mic16_irq_ring.h"
#include <stddef.h>
#define MAGIC 0x4d495251u
static int valid(const mic16_irq_ring *r){return r && r->magic==MAGIC;}
static int separate(const void *a,size_t an,const void *b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static void peak(mic16_irq_ring *r){unsigned i,n=0;for(i=0;i<3;i++)n+=r->slots[i].state!=M16_IRQ_FREE;if(n>r->peak_used)r->peak_used=n;}
int mic16_irq_ring_abort(mic16_irq_ring *r,int reason){
 if(!valid(r) || reason>M16_IRQ_OVERRUN || reason<M16_IRQ_TIMEOUT || reason==M16_IRQ_LEASE)return M16_IRQ_INVALID;
 if(!r->failure)r->failure=reason;
 return r->failure;
}
int mic16_irq_ring_init(mic16_irq_ring *r,uint64_t limit,uint64_t now){
 if(!r || (uintptr_t)r%_Alignof(mic16_irq_ring) || r->magic || limit>UINT64_MAX/M16_IRQ_WINDOW)return M16_IRQ_INVALID;
 *r=(mic16_irq_ring){0};r->magic=MAGIC;r->limit=limit;r->last_tick=now;r->active=1;r->generation=1;
 r->slots[0]=(mic16_irq_slot){M16_IRQ_FILLING,1,0,0};peak(r);return 0;
}
int mic16_irq_ring_destination(const mic16_irq_ring *r,unsigned *slot,unsigned *packet){
 if(!valid(r) || !separate(r,sizeof(*r),slot,sizeof(*slot)) || !separate(r,sizeof(*r),packet,sizeof(*packet)) ||
  !separate(slot,sizeof(*slot),packet,sizeof(*packet)))return M16_IRQ_INVALID;
 if(r->failure)return r->failure;
 if(!r->active)return r->done?M16_IRQ_DONE:M16_IRQ_STOPPED;
 *slot=r->slot;*packet=r->packet;return 0;
}
int mic16_irq_packet_valid(uint32_t sr,unsigned bytes,uint32_t status){
 if(status&1u)return M16_IRQ_PL_OVERFLOW;
 if(!(status&2u))return M16_IRQ_CLOCK;
 if(sr&0x4770u)return M16_IRQ_DMA;
 if(!(sr&0x1000u))return M16_IRQ_SPURIOUS;
 if(!(sr&2u))return M16_IRQ_DMA;
 if(bytes!=1024u)return M16_IRQ_LENGTH;
 return 0;
}
int mic16_irq_ring_complete(mic16_irq_ring *r,uint32_t sr,unsigned bytes,uint32_t status,uint64_t now){
 unsigned i;int rc;
 if(!valid(r))return M16_IRQ_INVALID;
 if(r->failure)return r->failure;
 if(!r->active)return r->done?M16_IRQ_DONE:M16_IRQ_STOPPED;
 if(now<r->last_tick)return mic16_irq_ring_abort(r,M16_IRQ_CLOCK);
 r->last_tick=now;rc=mic16_irq_packet_valid(sr,bytes,status);
 if(rc==M16_IRQ_SPURIOUS){r->spurious++;return rc;}
 if(rc)return mic16_irq_ring_abort(r,rc);
 r->packets++;r->packet++;
 if(r->packet<M16_IRQ_PACKETS)return 0;
 r->slots[r->slot].received_tick=now;r->slots[r->slot].state=M16_IRQ_READY;r->active=0;r->completed++;
 if(r->limit && r->completed==r->limit){r->done=1;return M16_IRQ_DONE;}
 if(r->completed>UINT64_MAX/M16_IRQ_WINDOW || r->generation==UINT64_MAX)return mic16_irq_ring_abort(r,M16_IRQ_COUNTER);
 for(i=0;i<3;i++)if(r->slots[i].state==M16_IRQ_FREE)break;
 if(i==3){r->overrun_count++;return mic16_irq_ring_abort(r,M16_IRQ_OVERRUN);}
 r->slot=i;r->packet=0;r->generation++;r->active=1;
 r->slots[i]=(mic16_irq_slot){M16_IRQ_FILLING,r->generation,r->completed,0};peak(r);return 0;
}
int mic16_irq_ring_take(mic16_irq_ring *r,mic16_irq_lease *out){
 unsigned i;
 if(!valid(r) || !separate(r,sizeof(*r),out,sizeof(*out)) || (uintptr_t)out%_Alignof(mic16_irq_lease))return M16_IRQ_INVALID;
 if(r->failure)return r->failure;
 for(i=0;i<3;i++)if(r->slots[i].state==M16_IRQ_READY && r->slots[i].window==r->taken){
  r->slots[i].state=M16_IRQ_READING;
  *out=(mic16_irq_lease){i,r->slots[i].generation,r->taken,r->slots[i].received_tick};r->taken++;return 0;
 }
 return r->done && r->taken==r->completed?M16_IRQ_DONE:M16_IRQ_EMPTY;
}
int mic16_irq_ring_release(mic16_irq_ring *r,const mic16_irq_lease *lease){
 mic16_irq_slot *p;
 if(!valid(r) || !separate(r,sizeof(*r),lease,sizeof(*lease)) || (uintptr_t)lease%_Alignof(mic16_irq_lease))return M16_IRQ_INVALID;
 if(lease->slot>=3)return M16_IRQ_LEASE;
 p=r->slots+lease->slot;
 if(p->state!=M16_IRQ_READING || p->generation!=lease->generation || p->window!=lease->window)return M16_IRQ_LEASE;
 p->state=M16_IRQ_FREE;return 0;
}
int mic16_irq_ring_quiesced(mic16_irq_ring *r,int stopped){
 unsigned i;
 if(!valid(r) || (stopped!=0 && stopped!=1))return M16_IRQ_INVALID;
 if(!stopped){r->quarantined=1;return M16_IRQ_STOPPED;}
 if(!r->failure && !r->done)mic16_irq_ring_abort(r,M16_IRQ_CANCELLED);
 for(i=0;i<3;i++)if(r->slots[i].state==M16_IRQ_FILLING)r->slots[i].state=M16_IRQ_FREE;
 r->active=0;r->quarantined=0;return 0;
}
