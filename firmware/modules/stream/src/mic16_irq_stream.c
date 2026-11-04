#include "mic16_irq_stream.h"
#include <stddef.h>
#define MAGIC 0x4d495342u
static int valid(const mic16_irq_stream *b){return b && b->magic==MAGIC;}
static int fail(mic16_irq_stream *b,int rc){if(!b->failure)b->failure=rc<0?rc:M16_IRQ_INVALID;return b->failure;}
int mic16_irq_stream_init(mic16_irq_stream *b,const mic16_irq_stream_ops *o){
 uintptr_t a=(uintptr_t)b,z=(uintptr_t)o;
 if(!b || !o || a%_Alignof(mic16_irq_stream) || b->magic || a>UINTPTR_MAX-sizeof(*b) || z>UINTPTR_MAX-sizeof(*o) ||
  !(a+sizeof(*b)<=z || z+sizeof(*o)<=a) || !o->open || !o->status || !o->take || !o->packet || !o->release || !o->stop)return M16_IRQ_INVALID;
 *b=(mic16_irq_stream){0};b->ops=*o;b->magic=MAGIC;return 0;
}
int mic16_irq_stream_status(mic16_irq_stream *b){
 int rc;if(!valid(b))return M16_IRQ_INVALID;
 if(b->failure)return b->failure;
 if(b->stopped)return M16_IRQ_STOPPED;
 if(!b->opened)return 0;
 rc=b->ops.status(b->ops.context);return rc?fail(b,rc):0;
}
int mic16_irq_stream_begin(mic16_irq_stream *b,unsigned slot,const mic16_stream_window *w){
 int rc;if(!valid(b) || !w || slot>=3 || b->pending || b->held[slot])return M16_IRQ_INVALID;
 if((rc=mic16_irq_stream_status(b)))return rc;
 if(b->next_window>UINT64_MAX/M16_IRQ_WINDOW-1u || w->first_sample!=b->next_window*M16_IRQ_WINDOW || w->end_sample!=w->first_sample+M16_IRQ_WINDOW)return fail(b,M16_IRQ_COUNTER);
 if(!b->opened){b->opened=1;rc=b->ops.open(b->ops.context);if(rc)return fail(b,rc);}
 b->pending=1;b->pending_slot=slot;b->pending_window=b->next_window;return 0;
}
int mic16_irq_stream_poll(mic16_irq_stream *b){
 int rc;unsigned s;mic16_irq_lease lease;
 if(!valid(b) || !b->pending)return M16_IRQ_INVALID;
 if((rc=mic16_irq_stream_status(b)))return rc;
 rc=b->ops.take(b->ops.context,&lease);
 if(rc==M16_IRQ_EMPTY)return 0;
 if(rc)return fail(b,rc<0?rc:M16_IRQ_COUNTER);
 s=b->pending_slot;b->leases[s]=lease;b->held[s]=1;b->pending=0;
 /* 即使错误窗号也保留已取得租约，不能在故障中丢失回收责任。 */
 if(lease.slot>=3 || lease.window!=b->pending_window)return fail(b,M16_IRQ_COUNTER);
 b->next_window++;return 1;
}
const unsigned char *mic16_irq_stream_packet(mic16_irq_stream *b,unsigned slot,uint64_t window,unsigned packet){
 if(!valid(b) || slot>=3 || !b->held[slot] || b->leases[slot].window!=window || packet>=2560 || b->failure)return 0;
 return b->ops.packet(b->ops.context,b->leases+slot,packet);
}
int mic16_irq_stream_release(mic16_irq_stream *b,unsigned slot){
 int rc;if(!valid(b) || slot>=3 || !b->held[slot])return M16_IRQ_LEASE;
 rc=b->ops.release(b->ops.context,b->leases+slot);
 if(rc)return fail(b,rc);
 b->held[slot]=0;return 0;
}
int mic16_irq_stream_stop(mic16_irq_stream *b){
 int rc;if(!valid(b))return 0;if(b->stopped)return 1;
 if(!b->opened){b->stopped=1;return 1;}
 rc=b->ops.stop(b->ops.context);if(rc==1){b->stopped=1;b->pending=0;return 1;}
 return 0;
}
int mic16_irq_stream_close(mic16_irq_stream *b){
 unsigned i;if(!mic16_irq_stream_stop(b))return 0;
 for(i=0;i<3;i++)if(b->held[i] && mic16_irq_stream_release(b,i))return 0;
 return 1;
}
