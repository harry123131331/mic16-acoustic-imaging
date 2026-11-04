/* 双池所有权、时段故障、时效及虚拟时钟压力测试，不是ARM实测吞吐。 */
#include "mic16_stream_scheduler.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static mic16_stream_scheduler s;
static unsigned checks,errors,scenarios,epoch_counter=100;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("STREAM_ERROR line=%u scenario=%u\n",(unsigned)__LINE__,scenarios);}}while(0)
static mic16_stream_ticket cap,comp,build,send;
static mic16_stream_window view;
static void fresh(unsigned inputs,unsigned outputs,uint64_t wait,uint64_t age){
    mic16_stream_config c={inputs,outputs,1000000u,wait,age,7,9};
    /* 每个场景使用独立虚拟设备；不对应任何仍活动的真实DMA。 */
    memset(&s,0,sizeof(s));scenarios++;CHECK(!mic16_stream_init(&s,&c,epoch_counter++,0));
}
static void input(unsigned index,uint64_t now){
    CHECK(!mic16_stream_input_reserve(&s,(uint64_t)index*40960,now,&cap));
    CHECK(!mic16_stream_input_commit(&s,&cap,now));
    CHECK(!mic16_stream_input_acquire(&s,now,&comp,&view));
}
static void make_result(unsigned index,uint64_t now){input(index,now);CHECK(!mic16_stream_output_reserve(&s,&comp,now,&build));CHECK(!mic16_stream_input_release(&s,&comp));}
static void unit_tests(void){
    mic16_stream_ticket old_cap,old_comp,old_build,second,tmp;mic16_stream_config bad={0};unsigned old_epoch;
    fresh(2,2,1000,2000);CHECK(mic16_stream_init(&s,&bad,1,0)==M16_STREAM_INVALID);
    CHECK(!mic16_stream_input_reserve(&s,0,10,&cap));old_cap=cap;
    CHECK(mic16_stream_input_reserve(&s,0,11,&tmp)==M16_STREAM_BUSY);
    CHECK(!mic16_stream_input_commit(&s,&cap,12));CHECK(mic16_stream_input_commit(&s,&cap,12)==M16_STREAM_LEASE);
    CHECK(!mic16_stream_input_acquire(&s,13,&comp,&view));old_comp=comp;
    CHECK(view.first_sample==0 && view.end_sample==40960 && view.received_tick==12 && view.context_id==7 && view.reference_id==9);
    CHECK(mic16_stream_input_acquire(&s,14,&tmp,&view)==M16_STREAM_BUSY);
    CHECK(!mic16_stream_output_reserve(&s,&comp,14,&build));old_build=build;
    CHECK(mic16_stream_output_reserve(&s,&comp,14,&tmp)==M16_STREAM_BUSY);
    CHECK(!mic16_stream_input_release(&s,&comp));CHECK(mic16_stream_input_release(&s,&comp)==M16_STREAM_LEASE);
    CHECK(!mic16_stream_input_reserve(&s,40960,15,&cap));CHECK(cap.generation>old_cap.generation);
    CHECK(mic16_stream_input_commit(&s,&old_cap,15)==M16_STREAM_LEASE);
    CHECK(!mic16_stream_input_commit(&s,&cap,16));CHECK(!mic16_stream_input_acquire(&s,17,&comp,&view));
    CHECK(!mic16_stream_output_reserve(&s,&comp,18,&second));CHECK(!mic16_stream_input_release(&s,&comp));
    CHECK(!mic16_stream_output_commit(&s,&second,19));CHECK(mic16_stream_output_acquire(&s,19,&send,&view)==M16_STREAM_EMPTY);
    CHECK(!mic16_stream_output_commit(&s,&build,20));CHECK(!mic16_stream_output_acquire(&s,21,&send,&view));
    CHECK(view.first_sample==0 && view.received_tick==12);CHECK(mic16_stream_output_cancel(&s,&old_build)==M16_STREAM_LEASE);
    CHECK(!mic16_stream_output_current(&s,&send,22));CHECK(!mic16_stream_output_finish(&s,&send,23,1));
    CHECK(!mic16_stream_output_acquire(&s,24,&send,&view));CHECK(view.first_sample==40960);
    CHECK(!mic16_stream_output_finish(&s,&send,25,1) && s.stats.outputs_sent==2);
    CHECK(mic16_stream_input_acquire(&s,25,(mic16_stream_ticket *)&s.input[0],&view)==M16_STREAM_INVALID);
    CHECK(mic16_stream_input_acquire(&s,25,&tmp,(mic16_stream_window *)&tmp)==M16_STREAM_INVALID);

    fresh(2,2,1000,2000);make_result(0,1);old_comp=comp;old_build=build;old_epoch=s.epoch;
    CHECK(!mic16_stream_input_reserve(&s,40960,2,&cap));CHECK(!mic16_stream_input_commit(&s,&cap,2));
    CHECK(!mic16_stream_input_acquire(&s,2,&comp,&view));CHECK(!mic16_stream_input_reserve(&s,81920,3,&cap));CHECK(!mic16_stream_input_commit(&s,&cap,3));
    CHECK(mic16_stream_input_reserve(&s,122880,4,&tmp)==M16_STREAM_OVERRUN);
    CHECK(s.input[comp.slot].state==M16_STREAM_COMPUTE && s.output[old_build.slot].state==M16_STREAM_BUILD);
    CHECK(mic16_stream_restart(&s,old_epoch+1,7,9,4)==M16_STREAM_BUSY);
    CHECK(mic16_stream_abort(&s,M16_STREAM_INPUT_INVALID)==M16_STREAM_STOPPED && s.failure==M16_STREAM_OVERRUN && s.reference_invalid);
    CHECK(!mic16_stream_input_release(&s,&comp));CHECK(!mic16_stream_output_cancel(&s,&old_build));
    CHECK(mic16_stream_restart(&s,old_epoch+1,7,9,4)==M16_STREAM_INVALID);
    CHECK(!mic16_stream_restart(&s,old_epoch+1,7,10,4));
    CHECK(mic16_stream_input_release(&s,&old_comp)==M16_STREAM_LEASE);
    CHECK(!mic16_stream_input_reserve(&s,0,5,&cap));CHECK(cap.epoch!=old_cap.epoch && cap.generation>old_comp.generation);

    fresh(2,2,20,20);CHECK(!mic16_stream_input_reserve(&s,0,10,&cap));CHECK(!mic16_stream_input_commit(&s,&cap,10));
    CHECK(!mic16_stream_poll(&s,30));CHECK(mic16_stream_poll(&s,31)==M16_STREAM_EXPIRED && s.input[cap.slot].state==M16_STREAM_FREE);
    fresh(2,2,100,20);make_result(0,10);CHECK(!mic16_stream_output_commit(&s,&build,12));CHECK(!mic16_stream_output_acquire(&s,13,&send,&view));
    CHECK(!mic16_stream_output_current(&s,&send,30));CHECK(mic16_stream_output_current(&s,&send,31)==M16_STREAM_EXPIRED);
    CHECK(s.output[send.slot].state==M16_STREAM_SEND);CHECK(mic16_stream_output_finish(&s,&send,32,0)==M16_STREAM_STOPPED);
    CHECK(s.output[send.slot].state==M16_STREAM_FREE && !s.stats.outputs_sent);
    fresh(2,2,100,200);input(0,10);CHECK(!mic16_stream_output_reserve(&s,&comp,11,&build));
    CHECK(mic16_stream_poll(&s,10)==M16_STREAM_CLOCK);CHECK(!mic16_stream_input_release(&s,&comp));CHECK(!mic16_stream_output_cancel(&s,&build));
    CHECK(mic16_stream_restart(&s,s.epoch+1,7,9,10)==M16_STREAM_INVALID);CHECK(!mic16_stream_restart(&s,s.epoch+1,7,9,11));
    fresh(2,2,100,200);input(0,1);CHECK(mic16_stream_input_release(&s,&comp)==M16_STREAM_NO_RESULT && s.failure==M16_STREAM_NO_RESULT);
    fresh(2,2,100,200);CHECK(mic16_stream_input_reserve(&s,1,1,&cap)==M16_STREAM_GAP);
    fresh(2,2,100,200);CHECK(!mic16_stream_input_reserve(&s,0,1,&cap));CHECK(mic16_stream_input_release(&s,&cap)==M16_STREAM_CANCELLED);
    fresh(2,2,100,200);s.generation=UINT64_MAX;CHECK(mic16_stream_input_reserve(&s,0,1,&cap)==M16_STREAM_COUNTER);
    fresh(2,2,100,200);make_result(0,1);CHECK(!mic16_stream_output_commit(&s,&build,2));CHECK(!mic16_stream_output_acquire(&s,3,&send,&view));
    CHECK(mic16_stream_output_finish(&s,&send,4,0)==M16_STREAM_SEND_FAILED && !s.stats.outputs_sent);

    fresh(2,1,100,200);make_result(0,1);old_build=build;input(1,2);
    CHECK(mic16_stream_output_reserve(&s,&comp,3,&build)==M16_STREAM_BUSY && !s.failure && s.stats.output_backpressure==1);
    CHECK(!mic16_stream_output_commit(&s,&old_build,4));CHECK(!mic16_stream_output_acquire(&s,5,&send,&view));CHECK(!mic16_stream_output_finish(&s,&send,6,1));
    CHECK(!mic16_stream_output_reserve(&s,&comp,7,&build));CHECK(!mic16_stream_input_release(&s,&comp));
    CHECK(!mic16_stream_output_commit(&s,&build,8));CHECK(!mic16_stream_output_acquire(&s,9,&send,&view));CHECK(!mic16_stream_output_finish(&s,&send,10,1));
    CHECK(s.stats.outputs_sent==2 && !s.failure);
}

#define PERIOD 1048576ULL
static void simulate(const char *name,unsigned capacity,unsigned windows,uint64_t service,uint64_t send_time,uint64_t result_age,int expected_failure){
    mic16_stream_ticket ci={0},co={0},pending={0},so={0},ct;mic16_stream_window w;
    unsigned arrived=0,computed=0,have_input=0,computing=0,have_pending=0,sending=0,events=0,ok=1;
    uint64_t now=0,compute_end=UINT64_MAX,send_end=UINT64_MAX;int rc;
    fresh(capacity,3,30*PERIOD,result_age);
    while(s.stats.outputs_sent<windows && !s.failure && events++<windows*12u+30u){
        uint64_t next=arrived<windows?(uint64_t)(arrived+1)*PERIOD:UINT64_MAX;
        if(computing && compute_end<next)next=compute_end;if(sending && send_end<next)next=send_end;
        if(next==UINT64_MAX){ok=0;break;}now=next;
        if(mic16_stream_poll(&s,now))break;
        if(sending && send_end==now){
            if(mic16_stream_output_finish(&s,&so,now,1)){ok=0;break;}sending=0;
        }
        if(computing && compute_end==now){
            if(mic16_stream_input_release(&s,&ci)){ok=0;break;}have_input=computing=0;
            if(have_pending && mic16_stream_output_commit(&s,&pending,now)){ok=0;break;}
            pending=co;have_pending=1;computed++;
            if(computed==windows){if(mic16_stream_output_commit(&s,&pending,now)){ok=0;break;}have_pending=0;}
        }
        if(arrived<windows && (uint64_t)(arrived+1)*PERIOD==now){
            rc=mic16_stream_input_reserve(&s,(uint64_t)arrived*40960,now,&ct);if(rc)break;
            if(mic16_stream_input_commit(&s,&ct,now)){ok=0;break;}arrived++;
        }
        if(!sending){rc=mic16_stream_output_acquire(&s,now,&so,&w);if(!rc){sending=1;send_end=now+send_time;}else if(rc<0){ok=0;break;}}
        if(!have_input){rc=mic16_stream_input_acquire(&s,now,&ci,&w);if(!rc)have_input=1;else if(rc<0){ok=0;break;}}
        if(have_input && !computing){rc=mic16_stream_output_reserve(&s,&ci,now,&co);if(!rc){computing=1;compute_end=now+service;}else if(rc!=M16_STREAM_BUSY){ok=0;break;}}
        if(s.stats.peak_input_slots>capacity || s.stats.peak_output_slots>3)ok=0;
    }
    CHECK(ok && s.failure==expected_failure);
    if(!expected_failure)CHECK(s.stats.outputs_sent==windows && computed==windows);
    else CHECK(s.stats.outputs_sent<windows);
    printf("STREAM_SIM name=%s capacity=%u requested=%u accepted=%llu computed=%u sent=%llu stop_us=%llu failure=%d peak_input=%u peak_output=%u service_us=%llu send_us=%llu VIRTUAL_CLOCK_NOT_ARM\n",
        name,capacity,windows,(unsigned long long)s.stats.inputs_committed,computed,(unsigned long long)s.stats.outputs_sent,
        (unsigned long long)now,s.failure,s.stats.peak_input_slots,s.stats.peak_output_slots,(unsigned long long)service,(unsigned long long)send_time);
    if(expected_failure==M16_STREAM_OVERRUN)CHECK(now==(uint64_t)(capacity+1)*PERIOD);
}
int main(void){
    unit_tests();
    simulate("fast",3,2000,300000,100000,3*PERIOD,0);
    simulate("historical_slow_1",1,20,15287596,100000,30*PERIOD,M16_STREAM_OVERRUN);
    simulate("historical_slow_2",2,20,15287596,100000,30*PERIOD,M16_STREAM_OVERRUN);
    simulate("historical_slow_3",3,20,15287596,100000,30*PERIOD,M16_STREAM_OVERRUN);
    simulate("late_compute",3,20,1500000,100000,2*PERIOD,M16_STREAM_EXPIRED);
    simulate("slow_sender",3,20,300000,2*PERIOD,3*PERIOD,M16_STREAM_EXPIRED);
    printf("STREAM_SCHEDULER_%s checks=%u errors=%u scenarios=%u metadata_bytes=%u BOARD_OPERATIONS=0\n",
        errors?"FAIL":"PASS",checks,errors,scenarios,(unsigned)sizeof(s));
    return errors?1:0;
}
