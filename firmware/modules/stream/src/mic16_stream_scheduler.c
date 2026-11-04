#include "mic16_stream_scheduler.h"
#include <stddef.h>
#define MAGIC 0x4d535351u
static int initialized(const mic16_stream_scheduler *s){return s && s->magic==MAGIC;}
static void increment(uint64_t *v){if(*v!=UINT64_MAX)(*v)++;}
static int disjoint(const void *a,uintptr_t na,const void *b,uintptr_t nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!a || !b || !na || !nb || x>UINTPTR_MAX-na || y>UINTPTR_MAX-nb)return 0;
    return x+na<=y || y+nb<=x;
}
static int writable(const mic16_stream_scheduler *s,const void *p,uintptr_t n,uintptr_t align){
    return p && (uintptr_t)p%align==0 && disjoint(s,sizeof(*s),p,n);
}
static unsigned used(const mic16_stream_slot *p,unsigned n){unsigned i,r=0;for(i=0;i<n;i++)r+=p[i].state!=M16_STREAM_FREE;return r;}
static void peaks(mic16_stream_scheduler *s){
    unsigned a=used(s->input,s->config.input_slots),b=used(s->output,s->config.output_slots);
    if(a>s->stats.peak_input_slots)s->stats.peak_input_slots=a;
    if(b>s->stats.peak_output_slots)s->stats.peak_output_slots=b;
}
static int reason_valid(int reason){return reason<=M16_STREAM_OVERRUN && reason>=M16_STREAM_INPUT_INVALID;}
int mic16_stream_abort(mic16_stream_scheduler *s,int reason){
    unsigned i;if(!initialized(s) || !reason_valid(reason))return M16_STREAM_INVALID;
    if(reason==M16_STREAM_INPUT_INVALID)s->reference_invalid=1;
    if(s->failure)return M16_STREAM_STOPPED;
    s->failure=reason;increment(&s->stats.epochs_failed);
    if(reason==M16_STREAM_OVERRUN)increment(&s->stats.input_overruns);
    for(i=0;i<s->config.input_slots;i++)if(s->input[i].state==M16_STREAM_READY){s->input[i].state=M16_STREAM_FREE;increment(&s->stats.queued_discarded);}
    for(i=0;i<s->config.output_slots;i++)if(s->output[i].state==M16_STREAM_READY){s->output[i].state=M16_STREAM_FREE;increment(&s->stats.queued_discarded);}
    return reason;
}
int mic16_stream_init(mic16_stream_scheduler *s,const mic16_stream_config *c,unsigned epoch,uint64_t now){
    if(!s || !c || (uintptr_t)s%_Alignof(mic16_stream_scheduler) || (uintptr_t)c%_Alignof(mic16_stream_config) || s->magic || !epoch ||
        !disjoint(s,sizeof(*s),c,sizeof(*c)) || !c->input_slots || c->input_slots>3 ||
        !c->output_slots || c->output_slots>3 || !c->tick_hz || !c->max_input_wait_ticks ||
        !c->max_result_age_ticks || !c->context_id || !c->reference_id)return M16_STREAM_INVALID;
    *s=(mic16_stream_scheduler){0};s->magic=MAGIC;s->epoch=epoch;s->config=*c;s->last_tick=now;return 0;
}
int mic16_stream_poll(mic16_stream_scheduler *s,uint64_t now){
    unsigned i;if(!initialized(s))return M16_STREAM_INVALID;
    if(s->failure)return M16_STREAM_STOPPED;
    if(now<s->last_tick)return mic16_stream_abort(s,M16_STREAM_CLOCK);
    s->last_tick=now;
    for(i=0;i<s->config.input_slots;i++){
        mic16_stream_slot *p=s->input+i;
        if((p->state==M16_STREAM_READY && now-p->window.received_tick>s->config.max_input_wait_ticks) ||
           (p->state==M16_STREAM_COMPUTE && now-p->window.received_tick>s->config.max_result_age_ticks))
            return mic16_stream_abort(s,M16_STREAM_EXPIRED);
    }
    for(i=0;i<s->config.output_slots;i++)if(s->output[i].state!=M16_STREAM_FREE &&
        now-s->output[i].window.received_tick>s->config.max_result_age_ticks)return mic16_stream_abort(s,M16_STREAM_EXPIRED);
    return 0;
}
static int generation(mic16_stream_scheduler *s,uint64_t *out){
    if(s->generation==UINT64_MAX)return mic16_stream_abort(s,M16_STREAM_COUNTER);
    *out=++s->generation;return 0;
}
static mic16_stream_ticket ticket(const mic16_stream_scheduler *s,unsigned pool,unsigned index,unsigned role){
    const mic16_stream_slot *p=(pool==M16_STREAM_INPUT?s->input:s->output)+index;
    mic16_stream_ticket t={pool,index,role,s->epoch,p->generation};return t;
}
static mic16_stream_slot *owned(mic16_stream_scheduler *s,const mic16_stream_ticket *t,unsigned pool,unsigned role){
    mic16_stream_slot *p;unsigned n=pool==M16_STREAM_INPUT?s->config.input_slots:s->config.output_slots;
    if(!t || (uintptr_t)t%_Alignof(mic16_stream_ticket) || !disjoint(s,sizeof(*s),t,sizeof(*t)) || t->pool!=pool || t->slot>=n || t->owner!=role || t->epoch!=s->epoch)goto invalid;
    p=(pool==M16_STREAM_INPUT?s->input:s->output)+t->slot;
    if(p->state!=role || p->generation!=t->generation)goto invalid;
    return p;
invalid:increment(&s->stats.lease_rejections);return 0;
}
int mic16_stream_input_reserve(mic16_stream_scheduler *s,uint64_t first,uint64_t now,mic16_stream_ticket *out){
    unsigned i;int rc;uint64_t gen=0;
    if(!initialized(s) || !writable(s,out,sizeof(*out),_Alignof(mic16_stream_ticket)))return M16_STREAM_INVALID;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    if(first!=s->next_input_first)return mic16_stream_abort(s,M16_STREAM_GAP);
    if(first>UINT64_MAX-M16_STREAM_WINDOW)return mic16_stream_abort(s,M16_STREAM_COUNTER);
    for(i=0;i<s->config.input_slots;i++)if(s->input[i].state==M16_STREAM_CAPTURE)return M16_STREAM_BUSY;
    for(i=0;i<s->config.input_slots;i++)if(s->input[i].state==M16_STREAM_FREE)break;
    if(i==s->config.input_slots)return mic16_stream_abort(s,M16_STREAM_OVERRUN);
    rc=generation(s,&gen);if(rc)return rc;
    s->input[i]=(mic16_stream_slot){M16_STREAM_CAPTURE,0,gen,{first,first+M16_STREAM_WINDOW,0,s->config.context_id,s->config.reference_id}};
    *out=ticket(s,M16_STREAM_INPUT,i,M16_STREAM_CAPTURE);peaks(s);return 0;
}
int mic16_stream_input_commit(mic16_stream_scheduler *s,const mic16_stream_ticket *t,uint64_t now){
    return mic16_stream_input_commit_received(s,t,now,now);
}
int mic16_stream_input_commit_received(mic16_stream_scheduler *s,const mic16_stream_ticket *t,uint64_t received,uint64_t now){
    mic16_stream_slot *p;int rc;if(!initialized(s))return M16_STREAM_INVALID;
    p=owned(s,t,M16_STREAM_INPUT,M16_STREAM_CAPTURE);if(!p)return M16_STREAM_LEASE;
    if(received>now)return M16_STREAM_INVALID;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    if(now-received>s->config.max_input_wait_ticks)return mic16_stream_abort(s,M16_STREAM_EXPIRED);
    p->window.received_tick=received;p->state=M16_STREAM_READY;s->next_input_first=p->window.end_sample;
    increment(&s->stats.inputs_committed);return 0;
}
static int outputs_separate(const mic16_stream_scheduler *s,mic16_stream_ticket *t,mic16_stream_window *w){
    return writable(s,t,sizeof(*t),_Alignof(mic16_stream_ticket)) && writable(s,w,sizeof(*w),_Alignof(mic16_stream_window)) && disjoint(t,sizeof(*t),w,sizeof(*w));
}
int mic16_stream_input_acquire(mic16_stream_scheduler *s,uint64_t now,mic16_stream_ticket *out,mic16_stream_window *w){
    unsigned i,index=3;int rc;uint64_t gen=0;
    if(!initialized(s) || !outputs_separate(s,out,w))return M16_STREAM_INVALID;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    for(i=0;i<s->config.input_slots;i++)if(s->input[i].state==M16_STREAM_COMPUTE)return M16_STREAM_BUSY;
    for(i=0;i<s->config.input_slots;i++)if(s->input[i].state==M16_STREAM_READY && s->input[i].window.first_sample==s->next_compute_first){index=i;break;}
    if(index==3)return M16_STREAM_EMPTY;
    rc=generation(s,&gen);if(rc)return rc;
    s->input[index].state=M16_STREAM_COMPUTE;s->input[index].generation=gen;
    *out=ticket(s,M16_STREAM_INPUT,index,M16_STREAM_COMPUTE);*w=s->input[index].window;
    s->next_compute_first=w->end_sample;increment(&s->stats.inputs_acquired);return 0;
}
int mic16_stream_output_reserve(mic16_stream_scheduler *s,const mic16_stream_ticket *in,uint64_t now,mic16_stream_ticket *out){
    mic16_stream_slot *p;unsigned i;int rc;uint64_t gen=0;
    if(!initialized(s) || !in || !writable(s,out,sizeof(*out),_Alignof(mic16_stream_ticket)) || !disjoint(in,sizeof(*in),out,sizeof(*out)))return M16_STREAM_INVALID;
    p=owned(s,in,M16_STREAM_INPUT,M16_STREAM_COMPUTE);if(!p)return M16_STREAM_LEASE;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    if(p->result_reserved)return M16_STREAM_BUSY;
    for(i=0;i<s->config.output_slots;i++)if(s->output[i].state==M16_STREAM_FREE)break;
    if(i==s->config.output_slots){increment(&s->stats.output_backpressure);return M16_STREAM_BUSY;}
    rc=generation(s,&gen);if(rc)return rc;
    s->output[i]=(mic16_stream_slot){M16_STREAM_BUILD,0,gen,p->window};p->result_reserved=1;
    *out=ticket(s,M16_STREAM_OUTPUT,i,M16_STREAM_BUILD);increment(&s->stats.outputs_reserved);peaks(s);return 0;
}
int mic16_stream_input_release(mic16_stream_scheduler *s,const mic16_stream_ticket *t){
    mic16_stream_slot *p;int rc=0;if(!initialized(s) || !t || (uintptr_t)t%_Alignof(mic16_stream_ticket) || !disjoint(s,sizeof(*s),t,sizeof(*t)))return M16_STREAM_INVALID;
    if(t->owner!=M16_STREAM_CAPTURE && t->owner!=M16_STREAM_COMPUTE)return M16_STREAM_LEASE;
    p=owned(s,t,M16_STREAM_INPUT,t->owner);if(!p)return M16_STREAM_LEASE;
    if(!s->failure){
        if(t->owner==M16_STREAM_CAPTURE)rc=mic16_stream_abort(s,M16_STREAM_CANCELLED);
        else if(!p->result_reserved)rc=mic16_stream_abort(s,M16_STREAM_NO_RESULT);
    }
    p->state=M16_STREAM_FREE;return rc;
}
int mic16_stream_output_commit(mic16_stream_scheduler *s,const mic16_stream_ticket *t,uint64_t now){
    mic16_stream_slot *p;int rc;if(!initialized(s))return M16_STREAM_INVALID;
    p=owned(s,t,M16_STREAM_OUTPUT,M16_STREAM_BUILD);if(!p)return M16_STREAM_LEASE;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    p->state=M16_STREAM_READY;increment(&s->stats.outputs_committed);return 0;
}
int mic16_stream_output_acquire(mic16_stream_scheduler *s,uint64_t now,mic16_stream_ticket *out,mic16_stream_window *w){
    unsigned i,index=3;int rc;uint64_t gen=0;
    if(!initialized(s) || !outputs_separate(s,out,w))return M16_STREAM_INVALID;
    rc=mic16_stream_poll(s,now);if(rc)return rc;
    for(i=0;i<s->config.output_slots;i++)if(s->output[i].state==M16_STREAM_SEND)return M16_STREAM_BUSY;
    for(i=0;i<s->config.output_slots;i++)if(s->output[i].state==M16_STREAM_READY &&
        s->output[i].window.first_sample/M16_STREAM_WINDOW==s->next_output_window){index=i;break;}
    if(index==3)return M16_STREAM_EMPTY;
    rc=generation(s,&gen);if(rc)return rc;
    s->output[index].state=M16_STREAM_SEND;s->output[index].generation=gen;
    *out=ticket(s,M16_STREAM_OUTPUT,index,M16_STREAM_SEND);*w=s->output[index].window;return 0;
}
int mic16_stream_output_current(mic16_stream_scheduler *s,const mic16_stream_ticket *t,uint64_t now){
    if(!initialized(s))return M16_STREAM_INVALID;
    if(!owned(s,t,M16_STREAM_OUTPUT,M16_STREAM_SEND))return M16_STREAM_LEASE;
    return mic16_stream_poll(s,now);
}
int mic16_stream_output_finish(mic16_stream_scheduler *s,const mic16_stream_ticket *t,uint64_t now,int sent_ok){
    mic16_stream_slot *p;int rc;if(!initialized(s) || (sent_ok!=0 && sent_ok!=1))return M16_STREAM_INVALID;
    p=owned(s,t,M16_STREAM_OUTPUT,M16_STREAM_SEND);if(!p)return M16_STREAM_LEASE;
    rc=mic16_stream_poll(s,now);
    if(!rc && !sent_ok)rc=mic16_stream_abort(s,M16_STREAM_SEND_FAILED);
    if(!rc){s->next_output_window++;increment(&s->stats.outputs_sent);}
    /* finish明确归还内存，即使发送失败/时钟异常，也不再由发送方访问。 */
    p->state=M16_STREAM_FREE;return rc;
}
int mic16_stream_output_cancel(mic16_stream_scheduler *s,const mic16_stream_ticket *t){
    mic16_stream_slot *p;int rc=0;if(!initialized(s) || !t || (uintptr_t)t%_Alignof(mic16_stream_ticket) || !disjoint(s,sizeof(*s),t,sizeof(*t)))return M16_STREAM_INVALID;
    if(t->owner!=M16_STREAM_BUILD && t->owner!=M16_STREAM_SEND)return M16_STREAM_LEASE;
    p=owned(s,t,M16_STREAM_OUTPUT,t->owner);if(!p)return M16_STREAM_LEASE;
    if(!s->failure)rc=mic16_stream_abort(s,M16_STREAM_CANCELLED);
    p->state=M16_STREAM_FREE;return rc;
}
int mic16_stream_restart(mic16_stream_scheduler *s,unsigned epoch,uint64_t context,uint64_t reference,uint64_t now){
    unsigned i;if(!initialized(s) || !s->failure || !epoch || epoch<=s->epoch || !context || !reference || now<s->last_tick)return M16_STREAM_INVALID;
    if(used(s->input,s->config.input_slots) || used(s->output,s->config.output_slots))return M16_STREAM_BUSY;
    if((s->reference_invalid || context!=s->config.context_id) && reference==s->config.reference_id)return M16_STREAM_INVALID;
    for(i=0;i<3;i++){s->input[i]=(mic16_stream_slot){0};s->output[i]=(mic16_stream_slot){0};}
    s->epoch=epoch;s->config.context_id=context;s->config.reference_id=reference;s->last_tick=now;
    s->failure=0;s->reference_invalid=0;s->next_input_first=s->next_compute_first=s->next_output_window=0;
    return 0;
}
