/* 虚拟DMA和虚拟时钟验证所有权、并发到达、反压和故障；不代表实板测量。
 * 数值算法与真实协议另由整链夹具核验，此处不伪造检出准确率。 */
#include "mic16_stream_runner.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static unsigned checks,errors,scenarios;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("RUNNER_ERROR line=%u scenario=%u\n",(unsigned)__LINE__,scenarios);}}while(0)
typedef struct {
    mic16_stream_runner runner;
    uint64_t tick,period,due,compute_us,send_us,start_tick,first_failure_tick;
    uint64_t tags[3],output_tags[3],starts,computes,sends,completed,completed_during_compute;
    unsigned active,slot,poll_fail,start_fail,stop_wait,stop_calls,compute_fail,finish_fail,send_fail;
    unsigned no_complete,no_yield,bad_emitted,bad_finish,recursive_checked,invalid_reference,clock_back;
    unsigned packets[3];
} fixture;
static fixture f;
static uint64_t clock_read(void *p){return ((fixture *)p)->tick;}
static int start(void *p,unsigned slot,const mic16_stream_window *w){
    fixture *x=p;
    CHECK(!x->active && slot<3 && w->first_sample==x->starts*M16_STREAM_WINDOW);
    x->active=1;x->slot=slot;x->starts++;x->tags[slot]=UINT64_MAX;
    x->due+=x->period;x->start_tick=x->tick;
    return x->start_fail?-901:0;
}
static int poll_capture(void *p){
    fixture *x=p;CHECK(x->active);
    if(x->poll_fail)return -902;
    if(x->no_complete || x->tick<x->due)return 0;
    x->tags[x->slot]=x->starts-1;x->active=0;x->completed++;
    if(x->runner.in_callback)x->completed_during_compute++;
    return 1;
}
static int stop(void *p){
    fixture *x=p;x->stop_calls++;
    if(x->stop_wait){x->stop_wait--;return 0;}
    x->active=0;return 1;
}
static int compute(void *p,mic16_stream_runner *r,unsigned input_slot,const mic16_stream_window *w,
    unsigned output_slot,uint64_t *emitted){
    fixture *x=p;uint64_t left=x->compute_us,k=w->first_sample/M16_STREAM_WINDOW;unsigned slot;int rc;
    CHECK(x->tags[input_slot]==k && output_slot<3 && r->queue.input[input_slot].state==M16_STREAM_COMPUTE);
    CHECK(!mic16_stream_runner_build_slot(r,k,&slot) && slot==output_slot);
    x->output_tags[output_slot]=k;x->packets[output_slot]=3;x->computes++;
    if(!x->recursive_checked){CHECK(mic16_stream_runner_step(r)==M16_STREAM_BUSY);x->recursive_checked=1;}
    while(left){
        uint64_t n=left>25000?25000:left;x->tick+=n;left-=n;
        if(!x->no_yield){
            rc=mic16_stream_runner_checkpoint(r);
            if(rc){
                CHECK(r->queue.input[input_slot].state==M16_STREAM_COMPUTE && x->tags[input_slot]==k);
                return -903;
            }
        }
    }
    CHECK(x->tags[input_slot]==k);
    if(x->clock_back)x->tick=0;
    if(x->compute_fail)return -904;
    *emitted=x->bad_emitted?k+2:k;return 0;
}
static int finish(void *p,mic16_stream_runner *r,uint64_t *emitted){
    fixture *x=p;(void)r;
    if(x->finish_fail)return -905;
    *emitted=x->bad_finish?0:x->computes;return 0;
}
static int send_one(void *p,unsigned slot,const mic16_stream_window *w){
    fixture *x=p;uint64_t k=w->first_sample/M16_STREAM_WINDOW;
    CHECK(x->output_tags[slot]==k && k==x->sends && x->packets[slot]>0);
    x->tick+=x->send_us;
    if(x->send_fail)return -906;
    x->packets[slot]--;
    if(!x->packets[slot]){x->sends++;return 1;}
    return 0;
}
static void init(uint64_t limit,unsigned inputs,unsigned outputs,uint64_t age){
    mic16_stream_runner_config c={{0},0,0};mic16_stream_runner_ops o;
    scenarios++;memset(&f,0,sizeof(f));f.period=1048576;f.compute_us=300000;f.send_us=500;
    c.queue.input_slots=inputs;c.queue.output_slots=outputs;c.queue.tick_hz=1000000;
    c.queue.max_input_wait_ticks=age;c.queue.max_result_age_ticks=age;c.queue.context_id=1;c.queue.reference_id=2;
    c.capture_timeout_ticks=2*f.period;c.input_limit=limit;
    o=(mic16_stream_runner_ops){&f,clock_read,start,poll_capture,stop,compute,finish,send_one};
    CHECK(!mic16_stream_runner_init(&f.runner,&c,&o,20261021));
}
static int run(unsigned bound){
    int rc=0;unsigned i;
    for(i=0;i<bound;i++){
        f.tick+=10000;rc=mic16_stream_runner_step(&f.runner);
        if(rc){f.first_failure_tick=f.tick;break;}
    }
    CHECK(i<bound);return rc;
}
static void free_all(void){
    unsigned i;for(i=0;i<3;i++)CHECK(f.runner.queue.input[i].state==M16_STREAM_FREE && f.runner.queue.output[i].state==M16_STREAM_FREE);
    CHECK(!f.runner.capture_held && !f.runner.compute_held && !f.runner.send_held && !f.active);
}
static void normal(void){
    int rc;
    init(2000,3,3,10ULL*1048576);rc=run(300000);
    CHECK(rc==M16_RUNNER_DONE && f.runner.finished && f.sends==2000 && f.computes==2000);
    CHECK(f.runner.queue.stats.outputs_sent==2000 && f.runner.finish_called==1 && !f.runner.queue.failure);
    CHECK(mic16_stream_runner_step(&f.runner)==M16_RUNNER_DONE);free_all();
    printf("RUNNER_SIM name=fast windows=%llu computed=%llu sent=%llu elapsed_us=%llu peak_inputs=%u peak_outputs=%u\n",
        (unsigned long long)f.completed,(unsigned long long)f.computes,(unsigned long long)f.sends,
        (unsigned long long)f.tick,f.runner.queue.stats.peak_input_slots,f.runner.queue.stats.peak_output_slots);
    init(12,3,3,10ULL*1048576);f.compute_us=1100000;rc=run(10000);
    CHECK(rc==M16_RUNNER_DONE && f.completed_during_compute>0 && f.sends==12);free_all();
    printf("RUNNER_SIM name=overlap capture_completions_during_compute=%llu sent=%llu\n",
        (unsigned long long)f.completed_during_compute,(unsigned long long)f.sends);
}
static void overload(void){
    unsigned n;int rc;
    for(n=1;n<=3;n++){
        init(0,n,3,100ULL*1048576);f.compute_us=8713353;rc=run(10000);
        CHECK(rc==M16_STREAM_OVERRUN && f.runner.queue.failure==M16_STREAM_OVERRUN && !f.sends);free_all();
        printf("RUNNER_SIM name=slow_%u stop_us=%llu inputs=%llu computed=%llu failure=%d\n",n,
            (unsigned long long)f.first_failure_tick,(unsigned long long)f.completed,
            (unsigned long long)f.computes,f.runner.queue.failure);
    }
    init(0,3,3,4ULL*1048576);f.send_us=700000;rc=run(10000);
    CHECK(rc==M16_STREAM_EXPIRED || rc==M16_STREAM_OVERRUN);free_all();
    printf("RUNNER_SIM name=slow_send sent=%llu failure=%d\n",(unsigned long long)f.sends,f.runner.queue.failure);
}
static void faults(void){
    int rc;unsigned slot=99,i;
    init(2,3,3,10ULL*1048576);f.start_fail=1;CHECK(run(100)==M16_STREAM_INPUT_INVALID);free_all();
    init(2,3,3,10ULL*1048576);f.poll_fail=1;CHECK(run(100)==M16_STREAM_INPUT_INVALID);free_all();
    init(2,3,3,10ULL*1048576);f.no_complete=1;CHECK(run(1000)==M16_STREAM_EXPIRED);free_all();
    init(2,3,3,10ULL*1048576);f.compute_fail=1;CHECK(run(1000)==M16_STREAM_INPUT_INVALID && !f.sends);free_all();
    init(2,3,3,10ULL*1048576);f.finish_fail=1;CHECK(run(1000)==M16_STREAM_INPUT_INVALID && f.sends<=1);free_all();
    init(2,3,3,10ULL*1048576);f.send_fail=1;CHECK(run(1000)==M16_STREAM_SEND_FAILED && !f.runner.queue.stats.outputs_sent);free_all();
    init(2,3,3,10ULL*1048576);f.bad_emitted=1;CHECK(run(1000)==M16_STREAM_NO_RESULT && !f.sends);free_all();
    init(2,3,3,10ULL*1048576);f.bad_finish=1;CHECK(run(1000)==M16_STREAM_NO_RESULT);free_all();
    init(2,3,3,10ULL*1048576);f.clock_back=1;CHECK(run(1000)==M16_STREAM_CLOCK);free_all();
    init(2,3,3,500000);f.no_yield=1;f.compute_us=900000;
    CHECK(run(1000)==M16_STREAM_EXPIRED && !f.sends);free_all();
    init(1,3,3,10ULL*1048576);CHECK(run(1000)==M16_RUNNER_DONE && f.sends==1);free_all();
    init(0,3,3,10ULL*1048576);CHECK(!mic16_stream_runner_step(&f.runner));
    CHECK(f.active && f.runner.capture_held);f.stop_wait=3;
    rc=mic16_stream_runner_stop(&f.runner,M16_STREAM_CANCELLED);CHECK(rc==M16_RUNNER_QUIESCING);
    CHECK(f.runner.capture_held && f.runner.queue.input[f.runner.capture.slot].state==M16_STREAM_CAPTURE);
    for(i=0;i<2;i++)CHECK(mic16_stream_runner_step(&f.runner)==M16_RUNNER_QUIESCING && f.runner.capture_held);
    CHECK(mic16_stream_runner_step(&f.runner)==M16_STREAM_CANCELLED);free_all();
    CHECK(mic16_stream_runner_build_slot(&f.runner,0,&slot)==M16_STREAM_CANCELLED && slot==99);
    CHECK(mic16_stream_runner_stop(&f.runner,M16_STREAM_SEND_FAILED)==M16_STREAM_CANCELLED);
    CHECK(mic16_stream_runner_stop(&f.runner,-999)==M16_STREAM_INVALID);
    init(0,3,1,4ULL*1048576);rc=run(10000);
    CHECK(rc==M16_STREAM_OVERRUN || rc==M16_STREAM_EXPIRED);
    CHECK(f.runner.queue.stats.output_backpressure>0 && !f.sends);free_all();
    init(1,3,3,10ULL*1048576);CHECK(mic16_stream_runner_build_slot(&f.runner,0,&slot)==M16_STREAM_NO_RESULT);
    CHECK(mic16_stream_runner_stop(&f.runner,M16_STREAM_INPUT_INVALID)==M16_STREAM_INPUT_INVALID && f.runner.queue.reference_invalid);free_all();
}
int main(void){
    CHECK(mic16_stream_runner_step(0)==M16_STREAM_INVALID);
    normal();overload();faults();
    printf("STREAM_RUNNER_%s checks=%u errors=%u scenarios=%u metadata_bytes=%u BOARD_OPERATIONS=0 VIRTUAL_CLOCK_ONLY\n",
        errors?"FAIL":"PASS",checks,errors,scenarios,(unsigned)sizeof(mic16_stream_runner));return errors?1:0;
}
