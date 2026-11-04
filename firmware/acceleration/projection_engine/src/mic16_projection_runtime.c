#include "mic16_projection_runtime.h"
#include <string.h>
#define MAGIC 0x4d505254u
#define GUARD UINT64_C(0x7ab43d91e8c2605f)
enum {CTRL=0,RETURN=0x10,ARENA_LOW=0x18,ARENA_HIGH=0x1c,WORDS=0x24,DESCRIPTORS=0x2c,JOBS=0x34};
static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int valid(const m16_projection_runtime *w){return w&&w->magic==MAGIC;}
static int fail(m16_projection_runtime *w,int rc){w->phase=M16_PR_QUARANTINE;w->report.error=rc;return rc;}
static int now(m16_projection_runtime *w,uint64_t *tick){
    *tick=w->io.ticks(w->io.context);if(*tick<w->last_tick)return fail(w,M16_PR_CLOCK);w->last_tick=*tick;return 0;
}
static int guards(const m16_projection_runtime *w){unsigned i;for(i=0;i<8;i++)if(w->arena[i]!=GUARD||w->arena[w->words-8+i]!=GUARD)return 0;return 1;}
static void add(m16_projection_runtime *w,unsigned begin,unsigned n){
    unsigned end=(begin+n+7u)&~7u;begin&=~7u;
    w->spans[w->span_count++]=(m16_projection_span){begin,end-begin};
}
static void spans(m16_projection_runtime *w,unsigned collecting){
    unsigned i,j,n=0;w->span_count=0;w->span_index=w->span_cursor=0;
    for(i=0;i<w->jobs;i++){
        const m16_projection_job *p=w->plan+i;
        if(!collecting){add(w,p->obs,p->no);add(w,p->coef,p->nc);}
        add(w,p->out,p->nr);add(w,p->report,2);
    }
    if(!collecting)add(w,w->descriptors,w->jobs*8);
    add(w,0,8);add(w,w->words-8,8);
    for(i=1;i<w->span_count;i++){
        m16_projection_span value=w->spans[i];j=i;
        while(j&&w->spans[j-1].begin>value.begin){w->spans[j]=w->spans[j-1];j--;}
        w->spans[j]=value;
    }
    for(i=0;i<w->span_count;i++){
        m16_projection_span value=w->spans[i];
        if(n&&value.begin<=w->spans[n-1].begin+w->spans[n-1].words){
            unsigned end=value.begin+value.words,prior=w->spans[n-1].begin+w->spans[n-1].words;
            if(end>prior)w->spans[n-1].words=end-w->spans[n-1].begin;
        }else w->spans[n++]=value;
    }
    w->span_count=n;
}
static int idle(m16_projection_runtime *w){
    unsigned ctrl=w->io.read(w->io.context,CTRL);w->report.last_control=ctrl;
    return (ctrl&4u)&&!(ctrl&0x81u);
}
int m16_projection_runtime_init(m16_projection_runtime *w,const m16_projection_io *io,
                                uint64_t *arena,unsigned words,unsigned abi){
    unsigned i,ctrl;
    if(!w||!io||!arena||((uintptr_t)w%_Alignof(m16_projection_runtime))||w->magic||
       ((uintptr_t)arena&63u)||words<32||(words&7u)||words>M16_PROJECTION_MAX_WORDS||
       !io->read||!io->write||!io->flush||!io->invalidate||!io->ticks||!io->barrier||!io->timeout_ticks||
       io->arena_address<0x100040u||(io->arena_address&63u)||io->arena_address>0x10000000u-words*8u||
       overlap(w,sizeof(*w),arena,(uintptr_t)words*8u)||overlap(io,sizeof(*io),w,sizeof(*w))||
       overlap(io,sizeof(*io),arena,(uintptr_t)words*8u))return M16_PR_INVALID;
    if(abi!=M16_PROJECTION_BUILD_ABI)return M16_PR_ABI;
    ctrl=io->read(io->context,CTRL);if(!(ctrl&4u)||(ctrl&0x81u))return M16_PR_BUSY;
    memset(w,0,sizeof(*w));w->magic=MAGIC;w->io=*io;w->arena=arena;w->words=words;
    w->last_tick=io->ticks(io->context);
    for(i=0;i<8;i++)arena[i]=arena[words-8+i]=GUARD;
    return 0;
}
int m16_projection_runtime_available(const m16_projection_runtime *w){
 return valid(w)&&w->phase==M16_PR_IDLE&&(!w->arbitrated||!w->producer);
}
int m16_projection_runtime_schedulable(const m16_projection_runtime *w){
 return valid(w)&&(w->arbitrated?w->phase!=M16_PR_QUARANTINE:m16_projection_runtime_available(w));
}
int m16_projection_runtime_owns(const m16_projection_runtime *w,const void *client){
 return valid(w)&&client&&w->arbitrated&&w->producer==client;
}
int m16_projection_runtime_client_released(const m16_projection_runtime *w,const void *client){
 if(!valid(w)||!client||w->phase==M16_PR_QUARANTINE)return 0;
 if(!w->arbitrated)return m16_projection_runtime_available(w);
 if(w->producer==client)return 0;
 for(unsigned i=0;i<8;i++)if(w->waiters[i].client==client)return 0;
 return 1;
}
int m16_projection_runtime_prepare_available(const m16_projection_runtime *w,const void *client){
 return valid(w)&&w->phase==M16_PR_IDLE&&(!w->arbitrated||w->producer==client);
}
int m16_projection_runtime_enable_arbiter(m16_projection_runtime *w){
 if(!m16_projection_runtime_available(w)||w->arbitrated||!idle(w))return M16_PR_BUSY;
 w->arbitrated=1;w->arena_revision=1;return 0;
}
static void remove_waiter(m16_projection_runtime *w,const void *client){
 for(unsigned i=0;i<8;i++)if(w->waiters[i].client==client)w->waiters[i]=(m16_projection_waiter){0};
}
int m16_projection_runtime_claim(m16_projection_runtime *w,const void *client,unsigned priority){
 unsigned free_slot=8,slot=8,best=8;uint64_t best_priority=0;
 if(!valid(w)||!client||priority>7)return M16_PR_INVALID;
 if(!w->arbitrated)return m16_projection_runtime_available(w)?0:1;
 if(w->phase==M16_PR_QUARANTINE)return w->report.error;
 if(w->producer==client)return 0;
 for(unsigned i=0;i<8;i++){
  if(w->waiters[i].client==client)slot=i;
  if(!w->waiters[i].client&&free_slot==8)free_slot=i;
 }
 if(slot==8){
  if(free_slot==8)return M16_PR_QUEUE;
  if(w->request_order==UINT64_MAX)return M16_PR_QUEUE;
  slot=free_slot;w->waiters[slot]=(m16_projection_waiter){client,priority,++w->request_order,w->grants};
 }
 if(w->producer||w->phase!=M16_PR_IDLE)return 1;
 for(unsigned i=0;i<8;i++)if(w->waiters[i].client){
  uint64_t effective=w->waiters[i].priority+(w->grants-w->waiters[i].granted_at)/8u;
  if(best==8||effective>best_priority||(effective==best_priority&&w->waiters[i].order<w->waiters[best].order)){
   best=i;best_priority=effective;
  }
 }
 if(best!=slot)return 1;
 if(w->grants==UINT64_MAX||(w->previous_producer!=client&&w->arena_revision==UINT64_MAX))return M16_PR_QUEUE;
 if(w->previous_producer!=client)w->arena_revision++;
 w->previous_producer=w->producer=client;w->grants++;remove_waiter(w,client);return 0;
}
int m16_projection_runtime_unclaim(m16_projection_runtime *w,const void *client){
 if(!valid(w)||!client)return M16_PR_INVALID;
 if(!w->arbitrated)return w->phase==M16_PR_IDLE?1:0;
 if(w->producer!=client){remove_waiter(w,client);return 1;}
 if(w->phase!=M16_PR_IDLE)return 0;
 w->producer=0;return 1;
}

int m16_projection_runtime_submit_for(m16_projection_runtime *w,const void *client,unsigned descriptors,unsigned jobs){
    unsigned i;uint64_t tick,high;
    if(!valid(w))return M16_PR_INVALID;
    if(w->arbitrated&&(!client||w->producer!=client))return M16_PR_LEASE;
    if(w->phase!=M16_PR_IDLE)return M16_PR_BUSY;
    if(descriptors<8||descriptors>=w->words-8||m16_projection_validate(w->arena,w->words-8,descriptors,jobs,w->plan))return M16_PR_INVALID;
    high=w->tag_high;
    for(i=0;i<jobs;i++){
        const m16_projection_job *p=w->plan+i;
        if(p->obs<8||p->coef<8||p->out<8||p->report<8||(!w->arbitrated&&p->tag<=w->tag_high))return M16_PR_INVALID;
        if(p->tag>high)high=p->tag;
    }
    if(!idle(w))return M16_PR_BUSY;
    if(!guards(w))return fail(w,M16_PR_GUARD);
    if(now(w,&tick))return w->report.error;
    if(w->arbitrated){
        if(w->tag_high>UINT64_MAX-jobs)return M16_PR_INVALID;
        high=w->tag_high+jobs;
        for(i=0;i<jobs;i++){
            w->plan[i].tag=w->tag_high+i+1u;w->arena[descriptors+i*8u+1u]=w->plan[i].tag;
        }
    }
    w->report=(m16_projection_report){0};w->descriptors=descriptors;w->jobs=jobs;
    memcpy(w->snapshot,w->arena+descriptors,jobs*64u);
    for(i=0;i<jobs;i++){w->arena[w->plan[i].report]=0;w->arena[w->plan[i].report+1]=UINT64_MAX;}
    w->tag_high=high;w->started=tick;w->done_seen=0;spans(w,0);w->phase=M16_PR_PREPARE;return 0;
}
static void cache_chunk(m16_projection_runtime *w,unsigned collecting){
    const m16_projection_span *s=w->spans+w->span_index;
    unsigned n=s->words-w->span_cursor;if(n>128)n=128;
    const void *p=w->arena+s->begin+w->span_cursor;
    if(collecting){w->io.invalidate(w->io.context,p,n*8u);w->report.invalidate_calls++;}
    else{w->io.flush(w->io.context,p,n*8u);w->report.flush_calls++;}
    w->span_cursor+=n;if(w->span_cursor==s->words){w->span_cursor=0;w->span_index++;}
}
int m16_projection_runtime_poll(m16_projection_runtime *w){
    uint64_t tick;unsigned i,ctrl;
    if(!valid(w)||w->phase==M16_PR_IDLE)return M16_PR_INVALID;
    if(w->phase==M16_PR_QUARANTINE)return w->report.error;
    if(w->phase==M16_PR_COMPLETE)return 0;
    if(now(w,&tick))return w->report.error;
    if(w->phase==M16_PR_PREPARE){
        if(w->span_index<w->span_count){cache_chunk(w,0);return now(w,&tick)?w->report.error:1;}
        if(memcmp(w->snapshot,w->arena+w->descriptors,w->jobs*64u))return fail(w,M16_PR_CHANGED);
        if(!idle(w))return fail(w,M16_PR_BUSY);
        w->io.barrier(w->io.context);
        w->io.write(w->io.context,4,0);w->io.write(w->io.context,8,0);
        w->io.write(w->io.context,ARENA_LOW,w->io.arena_address);w->io.write(w->io.context,ARENA_HIGH,0);
        w->io.write(w->io.context,WORDS,w->words);w->io.write(w->io.context,DESCRIPTORS,w->descriptors);
        w->io.write(w->io.context,JOBS,w->jobs);w->io.barrier(w->io.context);
        if(now(w,&tick))return w->report.error;
        w->submitted=tick;w->report.prepare_ticks=tick-w->started;
        w->io.write(w->io.context,CTRL,1);w->phase=M16_PR_DEVICE;return 1;
    }
    if(w->phase==M16_PR_DEVICE){
        ctrl=w->io.read(w->io.context,CTRL);w->report.last_control=ctrl;
        if(ctrl&2u)w->done_seen=1;
        if(!(w->done_seen&&(ctrl&4u)&&!(ctrl&0x81u))){
            if(tick-w->submitted>=w->io.timeout_ticks)return fail(w,M16_PR_TIMEOUT);
            return 1;
        }
        w->report.device_ticks=tick-w->submitted;
        w->report.kernel_return=(int32_t)w->io.read(w->io.context,RETURN);
        if(memcmp(w->snapshot,w->arena+w->descriptors,w->jobs*64u))return fail(w,M16_PR_CHANGED);
        w->io.barrier(w->io.context);w->collected=tick;spans(w,1);w->phase=M16_PR_COLLECT;return 1;
    }
    if(w->phase!=M16_PR_COLLECT)return fail(w,M16_PR_INVALID);
    if(w->span_index<w->span_count){cache_chunk(w,1);return now(w,&tick)?w->report.error:1;}
    w->io.barrier(w->io.context);
    if(!guards(w))return fail(w,M16_PR_GUARD);
    if(memcmp(w->snapshot,w->arena+w->descriptors,w->jobs*64u))return fail(w,M16_PR_CHANGED);
    if(w->report.kernel_return!=(int)w->jobs)return fail(w,M16_PR_RETURN);
    for(i=0;i<w->jobs;i++){
        const m16_projection_job *p=w->plan+i;
        if(w->arena[p->report]!=p->tag||w->arena[p->report+1]!=((uint64_t)p->nr<<32))return fail(w,M16_PR_RECEIPT);
    }
    if(now(w,&tick))return w->report.error;
    w->report.collect_ticks=tick-w->collected;w->report.total_ticks=tick-w->started;
    w->phase=M16_PR_COMPLETE;return 0;
}
int m16_projection_runtime_retire_for(m16_projection_runtime *w,const void *client){
    if(!valid(w)||w->phase!=M16_PR_COMPLETE)return M16_PR_BUSY;
    if(w->arbitrated&&(!client||w->producer!=client))return M16_PR_LEASE;
    w->phase=M16_PR_IDLE;if(w->arbitrated)w->producer=0;return 0;
}
int m16_projection_runtime_cancel_for(m16_projection_runtime *w,const void *client){
    if(!valid(w)||w->phase==M16_PR_QUARANTINE)return M16_PR_INVALID;
    if(w->arbitrated&&(!client||w->producer!=client)){
        if(client)remove_waiter(w,client);
        return client?1:M16_PR_LEASE;
    }
    if(w->phase==M16_PR_IDLE){if(w->arbitrated)w->producer=0;return 1;}
    if(w->phase!=M16_PR_PREPARE)return 0;
    if(!idle(w))return fail(w,M16_PR_BUSY);
    w->phase=M16_PR_IDLE;if(w->arbitrated)w->producer=0;return 1;
}

int m16_projection_runtime_poll_for(m16_projection_runtime *w,const void *client){
 if(!valid(w))return M16_PR_INVALID;
 if(w->arbitrated&&(!client||w->producer!=client))return M16_PR_LEASE;
 return m16_projection_runtime_poll(w);
}
/* 历史接口仅用于未启用仲裁的保留参考；新图生产者必须带客户端租约。 */
int m16_projection_runtime_submit(m16_projection_runtime *w,unsigned d,unsigned n){return m16_projection_runtime_submit_for(w,0,d,n);}
int m16_projection_runtime_retire(m16_projection_runtime *w){return m16_projection_runtime_retire_for(w,0);}
int m16_projection_runtime_cancel(m16_projection_runtime *w){return m16_projection_runtime_cancel_for(w,0);}
