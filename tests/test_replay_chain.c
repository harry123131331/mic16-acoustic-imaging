/* 离线整链夹具：真实包解码、冻结记忆、逐源增强、跨窗重建与真实协议编码。
 * real为精确全零拒绝，nonzero运行真实C16；其余指定观测，只验接口，不评价检出率。 */
#include "mic16_live_bridge.h"
#include "mic16_input_guard.h"
#include "mic16_stream_scheduler.h"
#include "mic16_stream_runner.h"
#include "mic16_irq_stream.h"
#include "../firmware/modules/mic16_cooperate.h"
#include "mic16_enhancement.h"
#include "mic16_enh_prediction.h"
#include "mic16_continuous.h"
#include "mic16_source_wire.h"
#include "mic16_directions_wire.h"
#include <stddef.h>
extern int printf(const char *,...);
extern int strcmp(const char *,const char *);
extern void *memset(void *,int,size_t);
extern void *memcpy(void *,const void *,size_t);
extern void *fopen(const char *,const char *);
extern size_t fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *);
static float quiet[8192*16],pcm[40960*16];
static mic16_pipeline_workspace work;
static mic16_pipeline_observation observation;
static mic16_cycle_output event;
static mic16_search_memory memory;
static mic16_live_input input;
static mic16_live_binding binding;
static m16_enh_workspace enhancement;
static m16_cont_workspace continuous;
static m16_cont_prediction prediction;
static unsigned checks,errors,cycle,reuse,suite,observer_calls,sink_calls,wire_calls;
static unsigned enhanced_sources,failed_writes,guarded,scheduled,runner_mode,prediction_only;
static mic16_stream_runner runner;
static uint64_t runner_time,runner_due;
static unsigned runner_active,irq_mode,irq_input_slot,irq_opened,irq_stopped;
static uint64_t irq_next_tick;
static mic16_irq_ring irq_ring;
static mic16_irq_stream irq_bridge;
static unsigned char irq_raw[3][2560][1024];
static void irq_pump(void);
/* 仅离线夹具暂存完整报文窗；生产调度器本身只有528字节元数据，不包含这些载荷。
 * 此处串行使用原PCM区，不代表DMA与计算已经在实板并发。 */
#define STAGED_WINDOW_BYTES (264u+24u*132u*(4u+1084u))
static unsigned char staged_packets[3][STAGED_WINDOW_BYTES];
static unsigned staged_lengths[3];
static mic16_stream_scheduler scheduler;
static mic16_stream_ticket capture_ticket,compute_ticket,window_build[3];
static unsigned scheduler_input_held;
static uint64_t scheduler_tick;
static mic16_guarded_observer_context input_guard;
static void *wire_file,*audio_file,*observation_file,*prediction_file,*double_audio_file;
static unsigned char packet[1024];
static uint32_t next_sequence;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("REPLAY_ERROR line=%u cycle=%u\n",(unsigned)__LINE__,cycle);}}while(0)
static void put32(unsigned char *p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint32_t get32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
/* 每一步推进虚拟时钟，只核验顺序/所有权，不将宿主机耗时当成ARM耗时。 */
static int schedule_begin_input(void){
    if(!scheduled)return 0;
    CHECK(!mic16_stream_input_reserve(&scheduler,(uint64_t)cycle*40960,++scheduler_tick,&capture_ticket));
    scheduler_input_held=M16_STREAM_CAPTURE;return errors?-1:0;
}
static int schedule_begin_compute(void){
    mic16_stream_window w;mic16_stream_ticket *b=window_build+cycle%3;
    if(!scheduled)return 0;
    CHECK(!mic16_stream_input_commit(&scheduler,&capture_ticket,++scheduler_tick));
    CHECK(!mic16_stream_input_acquire(&scheduler,++scheduler_tick,&compute_ticket,&w));
    scheduler_input_held=M16_STREAM_COMPUTE;
    CHECK(w.first_sample==(uint64_t)cycle*40960 && w.reference_id==20261020u);
    CHECK(!mic16_stream_output_reserve(&scheduler,&compute_ticket,++scheduler_tick,b));
    staged_lengths[b->slot]=0;return errors?-1:0;
}
static int schedule_stage(const unsigned char *data,unsigned n){
    unsigned window=get32(data+12),slot,offset;mic16_stream_ticket *b=window_build+window%3;
    if(runner_mode){
        if(mic16_stream_runner_build_slot(&runner,window,&slot))return -1;
        offset=staged_lengths[slot];
        if(n<36 || n>1084 || offset>STAGED_WINDOW_BYTES-(n+4u))return -1;
        put32(staged_packets[slot]+offset,n);memcpy(staged_packets[slot]+offset+4,data,n);
        staged_lengths[slot]=offset+n+4;return 0;
    }
    if(n<36 || n>1084 || b->pool!=M16_STREAM_OUTPUT || b->slot>=3)return -1;
    slot=b->slot;offset=staged_lengths[slot];
    if(scheduler.output[slot].state!=M16_STREAM_BUILD || scheduler.output[slot].generation!=b->generation ||
        scheduler.output[slot].window.first_sample!=(uint64_t)window*40960 || offset>STAGED_WINDOW_BYTES-(n+4u))return -1;
    put32(staged_packets[slot]+offset,n);memcpy(staged_packets[slot]+offset+4,data,n);
    staged_lengths[slot]=offset+n+4;return 0;
}
static int schedule_drain(void){
    mic16_stream_ticket send;mic16_stream_window w;int rc;unsigned offset,n,slot;
    if(!scheduled)return 0;
    for(;;){
        rc=mic16_stream_output_acquire(&scheduler,++scheduler_tick,&send,&w);
        if(rc==M16_STREAM_EMPTY)return 0;
        if(rc)return -1;
        slot=send.slot;offset=0;
        while(offset<staged_lengths[slot]){
            if(mic16_stream_output_current(&scheduler,&send,++scheduler_tick)){
                (void)mic16_stream_output_finish(&scheduler,&send,++scheduler_tick,0);return -1;
            }
            n=get32(staged_packets[slot]+offset);
            if(n<36 || n>1084 || offset+4u+n>staged_lengths[slot] ||
                get32(staged_packets[slot]+offset+4+12)!=w.first_sample/40960 ||
                fwrite(staged_packets[slot]+offset,1,n+4,wire_file)!=n+4){
                (void)mic16_stream_output_finish(&scheduler,&send,++scheduler_tick,0);return -1;
            }
            offset+=n+4;
        }
        if(mic16_stream_output_finish(&scheduler,&send,++scheduler_tick,1))return -1;
        staged_lengths[slot]=0;
    }
}
static int schedule_publish(unsigned previous){
    unsigned w;if(!scheduled)return 0;
    /* 连续重建已复制所需历史后，才可归还输入；结果独立等待后一窗。 */
    if(scheduler_input_held){CHECK(!mic16_stream_input_release(&scheduler,&compute_ticket));scheduler_input_held=0;}
    for(w=previous;w<continuous.emitted;w++)CHECK(!mic16_stream_output_commit(&scheduler,window_build+w%3,++scheduler_tick));
    CHECK(!schedule_drain());return errors?-1:0;
}
static void schedule_abort(int reason){
    unsigned i;if(!scheduled)return;
    (void)mic16_stream_abort(&scheduler,reason);
    if(scheduler_input_held){
        CHECK(!mic16_stream_input_release(&scheduler,scheduler_input_held==M16_STREAM_CAPTURE?&capture_ticket:&compute_ticket));
        scheduler_input_held=0;
    }
    for(i=0;i<3;i++)if(window_build[i].pool==M16_STREAM_OUTPUT){
        mic16_stream_ticket *b=window_build+i;mic16_stream_slot *p=scheduler.output+b->slot;
        if(p->generation==b->generation && p->state==M16_STREAM_BUILD)CHECK(!mic16_stream_output_cancel(&scheduler,b));
    }
}
static uint32_t word(uint32_t *state){*state=*state*1664525u+1013904223u;return *state;}
static unsigned wire_channel(unsigned c){return (5u+3u*c)%16u;}
static int raw_sample(unsigned kind,unsigned sample,unsigned channel,uint32_t *state){
    int value=(int)((word(state)>>16)&65535u)-32768;
    if(kind==0)return value;
    if(kind==1)return ((int)((sample*17u+channel*29u)%1024u)-512)*4096+value*32;
    return 0;
}
static void fill_packet(unsigned kind,unsigned offset,uint32_t *state){
    unsigned i,c;
    for(i=0;i<16;i++)for(c=0;c<16;c++)put32(packet+4u*(i*16u+wire_channel(c)),(uint32_t)raw_sample(kind,offset+i,c,state));
}
static void check_decoded(unsigned kind,unsigned n,const float *data){
    unsigned i,c,mismatches=0;uint32_t state=kind==0?20261015u:314159265u;
    for(i=0;i<n;i++)for(c=0;c<16;c++){
        int raw=raw_sample(kind,i,c,&state);float value=(float)((double)raw/33554432.);
        if(data[i*16+c]!=value)mismatches++;
    }
    CHECK(mismatches==0);
}
static int wire_sink(void *unused,const unsigned char *data,unsigned n){
    unsigned char length[4];(void)unused;wire_calls++;
    /* 在第二个输出窗的第二个分包注入发送失败；前一个完整窗仍可追溯。 */
    if(suite==3 && n>=60 && data[3]=='S' && get32(data+12)==1 && get32(data+40)==128){failed_writes++;return -1;}
    if(scheduled || runner_mode)return schedule_stage(data,n);
    put32(length,n);
    return fwrite(length,1,4,wire_file)!=4 || fwrite(data,1,n,wire_file)!=n;
}
static int audio_sink(void *unused,const m16_cont_info *info,const double *a,const double *b,unsigned n){
    uint32_t small[8]={info->epoch,info->window,info->source_id,info->ordinal,info->stream_count,info->current_confirmed,info->final,n};
    uint64_t large[5]={info->output_start,info->output_end,info->input_start,info->input_end,info->available_input};
    unsigned j;int rc;(void)unused;sink_calls++;
    if(fwrite(small,4,8,audio_file)!=8 || fwrite(large,8,5,audio_file)!=5)return -1;
    if(fwrite(a,8,n,double_audio_file)!=n || fwrite(b,8,n,double_audio_file)!=n)return -1;
    /* 线格式规定F32，此处同步保存独立期望值，不从报文反解期望值。 */
    for(j=0;j<n;j++){float row[2]={(float)a[j],(float)b[j]};if(fwrite(row,4,2,audio_file)!=2)return -1;}
    rc=m16_source_encode(info,a,b,n,wire_sink,0);return rc<0;
}
static int collect_prediction(void *unused,unsigned id,unsigned candidate,const float *features,
    const float *mask,const double *spectrum,const double *wdas,const double *enhanced,unsigned n){
    unsigned i,index=prediction.count;(void)unused;(void)features;(void)spectrum;(void)wdas;(void)enhanced;
    if(index>=8 || candidate>=16 || n!=16778)return -1;
    prediction.ids[index]=id;prediction.directions[index][0]=observation.directions[candidate*2];
    prediction.directions[index][1]=observation.directions[candidate*2+1];
    for(i=0;i<67*64;i++)prediction.masks[index][i]=mask[i];prediction.count++;return 0;
}
/* 新回调只接受预测参数；借用原夹具的同一复制逻辑，不传入伪造音频。 */
static int collect_parameters(void *p,unsigned id,unsigned candidate,const float *mask){
    return collect_prediction(p,id,candidate,0,mask,0,0,0,16778);
}
static int observe(const float *p,const float *r,mic16_pipeline_observation *o,mic16_pipeline_workspace *w,void *unused){
    unsigned i;(void)unused;observer_calls++;
    CHECK(input.leased && p==pcm && r==quiet && w==&work);
    if(!suite || suite==4)return reuse?mic16_pipeline_observe_reused(p,r,o,w):mic16_pipeline_observe(p,r,o,w);
    memset(o,0,sizeof(*o));
    if(suite!=2){
        o->candidate_count=2;o->directions[0]=-30.;o->directions[2]=30.;o->valid[0]=o->valid[1]=1;
        if(cycle<2){o->scores[0]=.9;o->scores[1]=.85;}
        else if(cycle==2)o->scores[1]=.85;
    }
    for(i=0;i<16;i++)if(o->scores[i] && !o->valid[i])return -1;
    return 0;
}
static int setup(void){
    mic16_live_config config={0};unsigned i;uint32_t state=20261015u;
    config.scale_exponent=25;config.context_id=20261020u;
    for(i=0;i<16;i++)config.logical_to_wire[i]=(unsigned char)wire_channel(i);
    CHECK(!mic16_live_init(&input,&config,quiet,8192*16,pcm,40960*16));
    mic16_memory_init(&memory);CHECK(!mic16_live_begin_reference(&input));
    for(i=0;i<512;i++){
        fill_packet(0,i*16,&state);
        if(mic16_live_push(&input,packet,1024,i,1)!=(i==511?M16_LIVE_REFERENCE_READY:0))return -1;
    }
    check_decoded(0,8192,quiet);CHECK(!mic16_live_begin_audio(&input));
    next_sequence=0xffffff00u;CHECK(!m16_cont_init(&continuous,20261020u));
    if(scheduled){
        mic16_stream_config c={2,3,1000000u,100000000u,100000000u,20261020u,20261020u};
        CHECK(!mic16_stream_init(&scheduler,&c,20261020u,0));
    }
    return errors?-1:0;
}
static int replay_window(unsigned kind){
    uint32_t state=314159265u;unsigned i,previous;int rc,count;
    if(schedule_begin_input())return -6;
    for(i=0;i<2560;i++){
        const unsigned char *raw=packet;
        if(irq_mode){raw=mic16_irq_stream_packet(&irq_bridge,irq_input_slot,cycle,i);if(!raw)return -9;}
        else fill_packet(kind,i*16,&state);
        rc=mic16_live_push(&input,raw,1024,next_sequence++,1);
        if(rc!=(i==2559?M16_LIVE_WINDOW_READY:0))return -1;
    }
    check_decoded(kind,40960,pcm);
    if(irq_mode && mic16_irq_stream_release(&irq_bridge,irq_input_slot))return -10;
    if(schedule_begin_compute())return -7;
    memset(&event,0,sizeof(event));
    rc=mic16_live_run_with_observer(&input,&binding,&memory,&observation,&work,&event,
        guarded?mic16_observe_with_input_guard:observe,guarded?&input_guard:0);
    if(guarded && !suite && cycle==0){
        CHECK(rc==-10 && input_guard.report.status==M16_INPUT_EXACT_ZERO && observer_calls==0);
        CHECK(!input.leased && input.mode==M16_LIVE_STOPPED && !binding.has_epoch && memory.count==0);
        CHECK(!event.count && !enhanced_sources && !wire_calls && !continuous.windows);
        CHECK(!mic16_live_interrupt(&input,1) && !input.reference_samples);
        CHECK(mic16_live_begin_audio(&input)==M16_LIVE_STATE);
        schedule_abort(M16_STREAM_INPUT_INVALID);
        return 2;
    }
    CHECK(!rc && !input.leased && binding.has_epoch && input.mode==M16_LIVE_AUDIO);
    if(rc)return -2;
    if(fwrite(&observation,1,sizeof(observation),observation_file)!=sizeof(observation) ||
        fwrite(&event,1,sizeof(event),observation_file)!=sizeof(event))return -3;
    if(suite==1 || suite==3){
        const unsigned expected_current[4]={1,2,1,0},expected_retained[4]={1,2,2,2};
        CHECK((unsigned)event.confirmed_count==expected_current[cycle]);
        CHECK((unsigned)event.count==expected_retained[cycle]);
        if(cycle==1)CHECK(event.new_source_valid && event.new_source.source_id==2);
        if(cycle==2)CHECK(event.tracks[0].source_id==1 && !event.tracks[0].confirmed_this_cycle);
    }
    CHECK(m16_directions_encode(20261020u,cycle,&event,wire_sink,0)==1);
    memset(&prediction,0,sizeof(prediction));
    if(prediction_only)count=m16_enh_predict_window(pcm,quiet,observation.directions,observation.valid,&event,&work.extended,
        &enhancement,collect_parameters,0);
    else count=m16_enh_window(pcm,quiet,observation.directions,observation.valid,&event,&work.extended,
        &enhancement,collect_prediction,0);
    CHECK(count==event.confirmed_count && count==(int)prediction.count);
    if(count<0)return -4;
    enhanced_sources+=(unsigned)count;
    if(count)for(i=0;i<320;i++)prediction.weights[i]=enhancement.weights[i];
    if(fwrite(&prediction,1,sizeof(prediction),prediction_file)!=sizeof(prediction))return -11;
    previous=continuous.emitted;
    rc=m16_cont_push(&continuous,pcm,&prediction,(uint64_t)cycle*40960,audio_sink,0);
    if(suite==3 && cycle==2){
        unsigned calls=wire_calls;
        CHECK(rc<0 && continuous.failed && failed_writes==1 && continuous.emitted==1);
        CHECK(m16_cont_finish(&continuous,audio_sink,0)<0);
        CHECK(m16_cont_push(&continuous,pcm,&prediction,3ULL*40960,audio_sink,0)<0);
        CHECK(wire_calls==calls);schedule_abort(M16_STREAM_CANCELLED);return 1;
    }
    CHECK(rc>=0 && continuous.emitted==(cycle?previous+1:previous));
    if(rc>=0 && schedule_publish(previous))return -8;
    printf("REPLAY_CYCLE suite=%u reuse=%u window=%u candidates=%u retained=%d current=%d emitted=%u\n",
        suite,reuse,cycle,observation.candidate_count,event.count,event.confirmed_count,continuous.emitted);
    return rc<0?-5:0;
}
/* 固定PCM回放后端不接真实DMA：虚拟采集只报告就绪；compute仍用上面的
 * 同一个真实解码/冻结检测/逐源增强/跨窗重建函数。另有独立测试验证异步载荷所有权。 */
static int irq_open_(void *p){(void)p;irq_opened=1;irq_next_tick=runner_time+1000000;return mic16_irq_ring_init(&irq_ring,0,runner_time);}
static int irq_status_(void *p){(void)p;return irq_ring.failure;}
static int irq_take_(void *p,mic16_irq_lease *l){(void)p;return mic16_irq_ring_take(&irq_ring,l);}
static const unsigned char *irq_packet_(void *p,const mic16_irq_lease *l,unsigned k){
 mic16_irq_slot *s;(void)p;if(l->slot>=3 || k>=2560)return 0;s=irq_ring.slots+l->slot;
 if(s->state!=M16_IRQ_READING || s->generation!=l->generation || s->window!=l->window)return 0;
 return irq_raw[l->slot][k];
}
static int irq_release_(void *p,const mic16_irq_lease *l){(void)p;return mic16_irq_ring_release(&irq_ring,l);}
static int irq_stop_(void *p){(void)p;irq_stopped=1;mic16_irq_ring_quiesced(&irq_ring,1);return 1;}
static void irq_pump(void){
 unsigned k,slot,p,kind;uint32_t state;int rc;
 if(!irq_mode || !irq_opened || irq_stopped)return;
 while(runner_time>=irq_next_tick && !irq_ring.failure){
  state=314159265u;kind=!suite?(irq_ring.completed==1?1u:2u):1u;
  for(k=0;k<2560;k++){
   rc=mic16_irq_ring_destination(&irq_ring,&slot,&p);if(rc)return;
   fill_packet(kind,k*16,&state);memcpy(irq_raw[slot][p],packet,1024);
   rc=mic16_irq_ring_complete(&irq_ring,0x1002,1024,2,runner_time);if(rc)return;
  }
  irq_next_tick+=1000000;
 }
}
static uint64_t runner_ticks(void *p){(void)p;return runner_time;}
static int runner_start(void *p,unsigned slot,const mic16_stream_window *w){
    if(irq_mode)return mic16_irq_stream_begin(&irq_bridge,slot,w);
    (void)p;(void)slot;(void)w;CHECK(!runner_active);runner_active=1;runner_due=runner_time+1000000;return 0;
}
static int runner_poll(void *p){(void)p;if(irq_mode)return mic16_irq_stream_poll(&irq_bridge);if(runner_time<runner_due)return 0;runner_active=0;return 1;}
static int runner_stop(void *p){(void)p;if(irq_mode)return mic16_irq_stream_stop(&irq_bridge);runner_active=0;return 1;}
static void runner_yield(void *p){
 (void)p;runner_time++;irq_pump();
 if(irq_mode && mic16_irq_stream_status(&irq_bridge)<0)(void)mic16_stream_runner_stop(&runner,M16_STREAM_INPUT_INVALID);
 else (void)mic16_stream_runner_checkpoint(&runner);
}
static int runner_compute(void *p,mic16_stream_runner *r,unsigned input_slot,
    const mic16_stream_window *w,unsigned output_slot,uint64_t *emitted){
    int rc;(void)p;(void)r;(void)input_slot;
    cycle=(unsigned)(w->first_sample/40960);staged_lengths[output_slot]=0;irq_input_slot=input_slot;
    rc=replay_window(!suite?(cycle==1?1u:2u):1u);
    *emitted=continuous.emitted;return rc;
}
static int runner_finish(void *p,mic16_stream_runner *r,uint64_t *emitted){
    int rc;(void)p;(void)r;rc=m16_cont_finish(&continuous,audio_sink,0);*emitted=continuous.emitted;return rc<0?-1:0;
}
static int runner_send(void *p,unsigned slot,const mic16_stream_window *w){
    unsigned n,offset;static unsigned offsets[3];(void)p;offset=offsets[slot];
    if(offset>=staged_lengths[slot])return -1;
    n=get32(staged_packets[slot]+offset);
    if(n<36 || n>1084 || offset+4u+n>staged_lengths[slot] ||
        get32(staged_packets[slot]+offset+16)!=w->first_sample/40960 ||
        fwrite(staged_packets[slot]+offset,1,n+4,wire_file)!=n+4)return -1;
    offsets[slot]=offset+n+4;
    if(offsets[slot]==staged_lengths[slot]){offsets[slot]=0;staged_lengths[slot]=0;return 1;}
    return 0;
}
static int run_runner(unsigned windows){
    mic16_stream_runner_config c={{3,3,1000000,100000000,100000000,20261020,20261020},2000000,0};
    mic16_stream_runner_ops o={0,runner_ticks,runner_start,runner_poll,runner_stop,runner_compute,runner_finish,runner_send};
    int rc=0;unsigned iterations;
    if(irq_mode){mic16_irq_stream_ops b={0,irq_open_,irq_status_,irq_take_,irq_packet_,irq_release_,irq_stop_};CHECK(!mic16_irq_stream_init(&irq_bridge,&b));}
    c.input_limit=windows;CHECK(!mic16_stream_runner_init(&runner,&c,&o,20261020));
    CHECK(!m16_cooperate_bind(runner_yield,0));
    for(iterations=0;iterations<100000;iterations++){
        runner_time+=1000;irq_pump();rc=mic16_stream_runner_step(&runner);if(rc)break;
    }
    CHECK(iterations<100000);CHECK(!m16_cooperate_bind(0,0));
    if(irq_mode){CHECK(mic16_irq_stream_close(&irq_bridge)==1);printf("IRQ_CHAIN windows=%llu taken=%llu packets=%llu stopped=%u failure=%d VIRTUAL_IRQ_NOT_BOARD\n",irq_ring.completed,irq_ring.taken,irq_ring.packets,irq_stopped,irq_bridge.failure);}
    if(!suite)CHECK(rc==M16_STREAM_INPUT_INVALID && runner.queue.reference_invalid && !runner.queue.stats.outputs_sent);
    else if(suite==3)CHECK(rc==M16_STREAM_INPUT_INVALID && runner.queue.stats.outputs_sent==1);
    else CHECK(rc==M16_RUNNER_DONE && runner.queue.stats.outputs_sent==windows);
    for(iterations=0;iterations<3;iterations++)CHECK(runner.queue.input[iterations].state==M16_STREAM_FREE && runner.queue.output[iterations].state==M16_STREAM_FREE);
    printf("RUNNER_REPLAY sent=%llu failure=%d checkpoints=%llu computed=%llu finished=%u VIRTUAL_CLOCK_NOT_ARM\n",
        (unsigned long long)runner.queue.stats.outputs_sent,runner.queue.failure,
        (unsigned long long)runner.checkpoint_calls,(unsigned long long)runner.computed,runner.finished);
    return rc;
}
int main(int argc,char **argv){
    unsigned windows,i;int rc=0;
    if(argc!=3)return 2;
    prediction_only=!strcmp(argv[1],"predictionrunner");
    irq_mode=prediction_only || !strcmp(argv[1],"irqrunner");runner_mode=irq_mode || !strcmp(argv[1],"runner");scheduled=!strcmp(argv[1],"scheduled");
    guarded=runner_mode || scheduled || !strcmp(argv[1],"guarded");reuse=guarded || !strcmp(argv[1],"reused");
    if(strcmp(argv[1],"baseline") && !reuse)return 2;
    if(!strcmp(argv[2],"real"))suite=0;
    else if(!strcmp(argv[2],"nonzero"))suite=4;
    else if(!strcmp(argv[2],"multisource"))suite=1;
    else if(!strcmp(argv[2],"empty"))suite=2;
    else if(!strcmp(argv[2],"failure"))suite=3;
    else return 2;
    wire_file=fopen("packets.bin","wb");audio_file=fopen("audio_expected.bin","wb");
    observation_file=fopen("observations.bin","wb");
    prediction_file=fopen("predictions.bin","wb");
    double_audio_file=fopen("audio_f64.bin","wb");
    if(!wire_file || !audio_file || !observation_file || !prediction_file || !double_audio_file)return 3;
    if(setup())return 4;
    input_guard.downstream=observe;input_guard.downstream_context=0;
    windows=(suite==1)?4u:3u;
    if(runner_mode)rc=run_runner(windows);
    else for(cycle=0;cycle<windows;cycle++){
        rc=replay_window(!suite?(cycle==1?1u:2u):1u);
        if(rc)break;
    }
    if(!runner_mode && guarded && !suite){
        CHECK(rc==2 && !observer_calls && !wire_calls && !continuous.windows && !enhanced_sources);
    }else if(!runner_mode && suite!=3){
        CHECK(rc==0 && observer_calls==windows);
        i=continuous.emitted;
        CHECK(m16_cont_finish(&continuous,audio_sink,0)>=0);
        CHECK(!schedule_publish(i));
        CHECK(continuous.closed && continuous.emitted==windows && !continuous.failed);
        i=wire_calls;CHECK(m16_cont_finish(&continuous,audio_sink,0)<0 && wire_calls==i);
    }else if(!runner_mode)CHECK(rc==1 && observer_calls==3 && failed_writes==1);
    else if(suite && suite!=3)CHECK(continuous.closed && continuous.emitted==windows && observer_calls==windows && !continuous.failed);
    if(scheduled){
        for(i=0;i<3;i++)CHECK(scheduler.input[i].state==M16_STREAM_FREE && scheduler.output[i].state==M16_STREAM_FREE);
        if(!suite)CHECK(scheduler.failure==M16_STREAM_INPUT_INVALID && scheduler.reference_invalid && !scheduler.stats.outputs_sent);
        else if(suite==3)CHECK(scheduler.failure==M16_STREAM_CANCELLED && scheduler.stats.outputs_sent==1);
        else CHECK(!scheduler.failure && scheduler.stats.outputs_sent==windows && scheduler.stats.inputs_committed==windows);
        printf("STREAM_REPLAY sent=%llu failure=%d peak_input=%u peak_output=%u virtual_tick=%llu test_payload_bytes=%u VIRTUAL_CLOCK_NOT_ARM\n",
            (unsigned long long)scheduler.stats.outputs_sent,scheduler.failure,scheduler.stats.peak_input_slots,scheduler.stats.peak_output_slots,
            (unsigned long long)scheduler_tick,(unsigned)sizeof(staged_packets));
    }
    CHECK(!fclose(wire_file));CHECK(!fclose(audio_file));CHECK(!fclose(observation_file));CHECK(!fclose(prediction_file));CHECK(!fclose(double_audio_file));
    printf("REPLAY_CHAIN_%s checks=%u errors=%u suite=%u reuse=%u windows=%u enhanced_sources=%u audio_callbacks=%u wire_calls=%u failed_writes=%u guarded=%u guard_status=%d scheduled=%u BOARD_OPERATIONS=0\n",
        errors?"FAIL":"PASS",checks,errors,suite,reuse,continuous.windows,enhanced_sources,sink_calls,wire_calls,failed_writes,guarded,input_guard.report.status,scheduled);
    return errors?1:0;
}
