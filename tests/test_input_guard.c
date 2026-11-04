/* 无效输入门与冻结基线对照；检测能力、噪声门限和SPL不在本测试中推断。 */
#include "mic16_input_guard.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
static float pcm[40960*16],quiet[8192*16];
static mic16_pipeline_workspace workspace;
static mic16_pipeline_observation baseline,output;
static mic16_input_report report;
static mic16_guarded_observer_context guard;
static mic16_live_input input;
static mic16_live_binding binding;
static mic16_search_memory memory;
static mic16_cycle_output event;
static unsigned char packet[1024];
static unsigned checks,errors,downstream_calls;
static int downstream_rc;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("INPUT_GUARD_ERROR line=%u check=%u\n",(unsigned)__LINE__,checks);}}while(0)
static float from_bits(unsigned u){union{unsigned u;float f;}v;v.u=u;return v.f;}
static void reset_arrays(void){memset(pcm,0,sizeof(pcm));memset(quiet,0,sizeof(quiet));}
static int spy(const float *p,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w,void *context){
    CHECK(p==pcm && q==quiet && o==&output && w==&workspace && context==&downstream_rc);
    downstream_calls++;return downstream_rc;
}
static int actual(const float *p,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w,void *unused){
    (void)unused;downstream_calls++;return mic16_pipeline_observe_reused(p,q,o,w);
}
static void inspect_cases(void){
    unsigned i,j;const unsigned tiny_and_edges[]={1u,0x80000001u,0x00800000u,0x80800000u,0x3f800000u,0xbf800000u};
    const unsigned invalid[]={0x7f800000u,0xff800000u,0x7fc00001u,0x7fa00001u,0x3f800001u,0xbf800001u};
    reset_arrays();
    CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_EXACT_ZERO);
    CHECK(report.status==M16_INPUT_EXACT_ZERO && report.nonzero_observed==0 && report.zero_channel_mask==65535u);
    CHECK(report.observed_checked==40960*16 && report.reference_checked==8192*16 && report.invalid_index==0xffffffffu);
    for(i=0;i<40960u*16u;i+=2)pcm[i]=from_bits(0x80000000u);
    CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_EXACT_ZERO);
    for(i=0;i<sizeof(tiny_and_edges)/sizeof(tiny_and_edges[0]);i++){
        pcm[40960*16-1]=from_bits(tiny_and_edges[i]);
        CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_OK);
        CHECK(report.nonzero_observed==1 && report.zero_channel_mask==32767u);
    }
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        pcm[17]=from_bits(invalid[i]);
        CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_INVALID_PCM);
        CHECK(report.invalid_index==17 && !report.invalid_is_reference && report.observed_checked==18 && report.zero_channel_mask==0xffffffffu);
        pcm[17]=0.;quiet[31]=from_bits(invalid[i]);
        CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_INVALID_REFERENCE);
        CHECK(report.invalid_index==31 && report.invalid_is_reference && report.reference_checked==32);
        quiet[31]=0.;
    }
    report.status=777;
    CHECK(mic16_input_inspect(0,quiet,&report)==M16_INPUT_ARGUMENT && report.status==777);
    CHECK(mic16_input_inspect(pcm,pcm,&report)==M16_INPUT_ARGUMENT && report.status==777);
    CHECK(mic16_input_inspect(pcm,quiet,(mic16_input_report *)pcm)==M16_INPUT_ARGUMENT);
    CHECK(mic16_input_inspect(pcm,quiet,(mic16_input_report *)quiet)==M16_INPUT_ARGUMENT);
    /* 一路严格为零只作诊断，不以它拒绝其它15路的有效非零输入。 */
    for(i=0;i<40960u;i++)for(j=0;j<16;j++)pcm[i*16+j]=j==7?0.f:from_bits(1u);
    CHECK(mic16_input_inspect(pcm,quiet,&report)==M16_INPUT_OK && report.zero_channel_mask==(1u<<7));
}
static void observer_cases(void){
    unsigned i,calls;reset_arrays();memset(&output,0xa5,sizeof(output));
    guard.downstream=spy;guard.downstream_context=&downstream_rc;downstream_rc=0;calls=downstream_calls;
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==M16_INPUT_EXACT_ZERO);
    CHECK(downstream_calls==calls && guard.report.status==M16_INPUT_EXACT_ZERO);
    for(i=0;i<sizeof(output);i++)if(((unsigned char *)&output)[i]!=0xa5){CHECK(0);break;}
    CHECK(i==sizeof(output));
    pcm[0]=from_bits(1u);
    CHECK(!mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard) && downstream_calls==calls+1);
    CHECK(guard.report.status==M16_INPUT_OK && guard.report.nonzero_observed==1);
    downstream_rc=-77;
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==-77 && guard.report.status==M16_INPUT_OK);
    calls=downstream_calls;quiet[10]=from_bits(0x7fc00001u);
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==M16_INPUT_INVALID_REFERENCE && downstream_calls==calls);
    quiet[10]=0.;guard.downstream=0;
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==M16_INPUT_ARGUMENT);
    guard.downstream=mic16_observe_with_input_guard;
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==M16_INPUT_ARGUMENT);
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&output)==M16_INPUT_ARGUMENT);
    guard.downstream=spy;
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,0)==M16_INPUT_ARGUMENT);
    CHECK(downstream_calls==calls);
}
static void live_failure_case(void){
    mic16_live_config config={0};unsigned i,ok,calls=downstream_calls;unsigned char valid[16]={1};
    double directions[32]={0},scores[16]={.9};uint64_t epoch;
    config.scale_exponent=25;config.context_id=20261020;
    for(i=0;i<16;i++)config.logical_to_wire[i]=(unsigned char)i;
    CHECK(!mic16_live_init(&input,&config,quiet,8192*16,pcm,40960*16));
    CHECK(!mic16_live_begin_reference(&input));memset(packet,0,sizeof(packet));
    ok=1;for(i=0;i<512;i++)if(mic16_live_push(&input,packet,1024,i,1)!=(i==511?M16_LIVE_REFERENCE_READY:0))ok=0;
    CHECK(ok);
    CHECK(!mic16_live_begin_audio(&input));
    ok=1;for(i=0;i<2560;i++)if(mic16_live_push(&input,packet,1024,i,1)!=(i==2559?M16_LIVE_WINDOW_READY:0))ok=0;
    CHECK(ok);
    mic16_memory_init(&memory);
    CHECK(!mic16_consume_scored_cycle(&memory,directions,valid,scores,1.,"prior_valid_epoch",1,&event) && memory.count==1);
    memset(&event,0,sizeof(event));binding.epoch=input.epoch;binding.has_epoch=1;epoch=input.epoch;
    guard.downstream=spy;guard.downstream_context=&downstream_rc;
    CHECK(mic16_live_run_with_observer(&input,&binding,&memory,&output,&workspace,&event,
        mic16_observe_with_input_guard,&guard)==-10);
    CHECK(guard.report.status==M16_INPUT_EXACT_ZERO && downstream_calls==calls);
    CHECK(!input.leased && input.mode==M16_LIVE_STOPPED && input.epoch>epoch);
    CHECK(!binding.has_epoch && memory.count==0 && !event.count);
    CHECK(mic16_live_push(&input,packet,1024,2560,1)==M16_LIVE_STATE);
    /* 与完整候选一致：输入故障后由应用使参考失效，不自动恢复或补零续接。 */
    CHECK(!mic16_live_interrupt(&input,1) && input.reference_samples==0);
    CHECK(mic16_live_begin_audio(&input)==M16_LIVE_STATE);
}
static unsigned next_word(unsigned *s){*s=*s*1664525u+1013904223u;return *s;}
static void fill_numeric(float scale,int zero){
    unsigned i,state=20261015u;
    for(i=0;i<8192*16;i++){int v=(int)((next_word(&state)>>16)&65535u)-32768;quiet[i]=(float)v*0x1p-25f*scale;}
    state=314159265u;
    for(i=0;i<40960*16;i++){
        unsigned s=i/16,c=i%16;int v=(int)((next_word(&state)>>16)&65535u)-32768;
        int saw=(int)((s*17u+c*29u)%1024u)-512;
        pcm[i]=zero?0.f:((float)saw*0x1p-13f+(float)v*0x1p-20f)*scale;
    }
}
static void numeric_comparison(void){
    unsigned fixture,calls;
    guard.downstream=actual;guard.downstream_context=0;
    for(fixture=0;fixture<2;fixture++){
        fill_numeric(fixture?0x1p-16f:1.f,0);
        memset(&baseline,0,sizeof(baseline));memset(&output,0,sizeof(output));
        CHECK(!mic16_pipeline_observe_reused(pcm,quiet,&baseline,&workspace));calls=downstream_calls;
        CHECK(!mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard));
        CHECK(guard.report.status==M16_INPUT_OK && downstream_calls==calls+1);
        CHECK(!memcmp(&baseline,&output,sizeof(baseline)));
        printf("INPUT_GUARD_VALID fixture=%u scale=%s observation_bit_differences=0 ONLY_IF_NO_ERRORS\n",fixture,fixture?"2^-16":"1");
    }
    fill_numeric(1.f,1);CHECK(!mic16_pipeline_observe_reused(pcm,quiet,&baseline,&workspace));calls=downstream_calls;
    CHECK(baseline.candidate_count==1 && baseline.scores[0]>=.65);
    CHECK(mic16_observe_with_input_guard(pcm,quiet,&output,&workspace,&guard)==M16_INPUT_EXACT_ZERO);
    CHECK(downstream_calls==calls && guard.report.nonzero_observed==0);
    printf("INPUT_GUARD_ZERO_BASELINE candidates=%u score=%.12g guarded_rc=%d callback_skipped=%u NOT_A_NORMAL_ZERO_SOURCE_RESULT\n",
        baseline.candidate_count,baseline.scores[0],guard.report.status,downstream_calls==calls);
}
int main(void){
    inspect_cases();observer_cases();live_failure_case();numeric_comparison();
    printf("INPUT_GUARD_%s checks=%u errors=%u BOARD_OPERATIONS=0 TRAINING_RUNS=0\n",errors?"FAIL":"PASS",checks,errors);
    return errors?1:0;
}
