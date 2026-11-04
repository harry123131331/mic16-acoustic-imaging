#include "mic16_live_bridge.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static float reference[8192u*16u],pcm[40960u*16u];
static mic16_pipeline_workspace workspace;
static mic16_pipeline_observation observation;
static mic16_search_memory memory;
static mic16_cycle_output event;
static mic16_live_input state;
static unsigned char packet[1024];
static unsigned int checks,errors,cases,mock_calls,mock_behavior;
static int check(int ok) { checks++;if(!ok){errors++;printf("FAIL case=%u check=%u\n",cases,checks);}return ok; }
static mic16_live_config config(unsigned int exponent) {
    mic16_live_config c={.scale_exponent=0};unsigned int i;
    c.scale_exponent=exponent;c.context_id=97;
    for(i=0;i<16;i++)c.logical_to_wire[i]=(unsigned char)(15u-i);
    return c;
}
static void put(unsigned int i,uint32_t v) {
    packet[4*i]=(unsigned char)v;packet[4*i+1]=(unsigned char)(v>>8);
    packet[4*i+2]=(unsigned char)(v>>16);packet[4*i+3]=(unsigned char)(v>>24);
}
static int raw_value(uint32_t seq,unsigned int i,unsigned int ch) {
    int x=(int)((seq%256u)*512u+i*16u+ch);
    return ch%2u ? -x : x;
}
static void pattern(uint32_t seq) {
    unsigned int i,ch;
    for(i=0;i<16;i++)for(ch=0;ch<16;ch++)put(i*16+ch,(uint32_t)raw_value(seq,i,ch));
}
static void init(unsigned int exponent) {
    mic16_live_config c=config(exponent);
    check(mic16_live_init(&state,&c,reference,8192u*16u,pcm,40960u*16u)==0);
}
static void reference_ready(void) {
    uint32_t i;int rc=0,ok=1;
    check(mic16_live_begin_reference(&state)==0);
    for(i=0;i<512;i++) { pattern(i);rc=mic16_live_push(&state,packet,1024,i,1);if(rc!=(i==511?1:0))ok=0; }
    check(ok && state.reference_samples==8192 && state.mode==M16_LIVE_STOPPED);
}
static void audio_ready(uint32_t first,int begin) {
    uint32_t i;int rc=0,ok=1;
    if(begin)check(mic16_live_begin_audio(&state)==0);
    for(i=0;i<2560;i++) { pattern(first+i);rc=mic16_live_push(&state,packet,1024,first+i,1);if(rc!=(i==2559?2:0))ok=0; }
    check(ok && state.pcm_samples==40960 && state.mode==M16_LIVE_READY);
}
/* 显式测试替身：此处不提取特征或执行网络推理。链接真实冻结源记忆规则；ARM构建另行链接真实流水线。 */
int mic16_pipeline_observe(const float *p,const float *r,mic16_pipeline_observation *o,mic16_pipeline_workspace *w) {
    unsigned int i;mock_calls++;
    check(p==pcm && r==reference && w==&workspace && state.leased);
    if(mock_behavior==1)return -3;
    if(mock_behavior==2)mic16_live_interrupt(&state,0);
    for(i=0;i<16;i++){o->directions[2*i]=0;o->directions[2*i+1]=0;o->valid[i]=0;o->scores[i]=0;}
    o->valid[0]=1;o->scores[0]=0.9;return 0;
}
int main(void) {
    mic16_live_config c=config(18);mic16_live_view v,old;
    mic16_live_binding binding={.epoch=0};unsigned int i,ch,mismatch;uint64_t epoch;
    float held_r,held_p;int rc;
    cases++; /* 无效合同不得产生可用状态。 */
    check(mic16_live_init(&state,&c,reference,1,pcm,40960u*16u)==M16_LIVE_INVALID);
    check(mic16_live_init(&state,&c,pcm,8192u*16u,pcm,40960u*16u)==M16_LIVE_INVALID);
    c.logical_to_wire[0]=c.logical_to_wire[1];
    check(mic16_live_init(&state,&c,reference,8192u*16u,pcm,40960u*16u)==M16_LIVE_INVALID);
    c=config(32);check(mic16_live_init(&state,&c,reference,8192u*16u,pcm,40960u*16u)==M16_LIVE_INVALID);
    cases++; /* 测试字节序、有符号端点及显式2^31归一化。 */
    init(31);check(mic16_live_begin_audio(&state)==M16_LIVE_STATE);
    check(mic16_live_take(&state,&v)==M16_LIVE_STATE);
    check(mic16_live_begin_reference(&state)==0);memset(packet,0,sizeof(packet));
    put(15,0x80000000u);put(14,0x7fffffffu);put(13,0xffffffffu);
    check(mic16_live_push(&state,packet,1024,0xffffffffu,1)==0);
    check(reference[0]==-1.0f && reference[1]==1.0f && reference[2]==-1.0f/2147483648.0f);
    check(mic16_live_push(&state,packet,1024,0,1)==0); /* 测试uint32序号回绕。 */
    cases++; /* 逐值核对独立安静参考及完整采集窗。 */
    init(18);reference_ready();audio_ready(1000,1);mismatch=0;
    for(i=0;i<8192;i++)for(ch=0;ch<16;ch++)
        if(reference[i*16+ch]!=(float)raw_value(i/16,i%16,15-ch)/262144.0f)mismatch++;
    for(i=0;i<40960;i++)for(ch=0;ch<16;ch++)
        if(pcm[i*16+ch]!=(float)raw_value(1000+i/16,i%16,15-ch)/262144.0f)mismatch++;
    check(mismatch==0);check(mic16_live_take(&state,&v)==0);
    check(v.end_sample==40960 && v.start_sample==0 && mic16_live_view_current(&state,&v));
    check(mic16_live_take(&state,&old)==M16_LIVE_BUSY);
    held_p=pcm[0];held_r=reference[0];
    check(mic16_live_push(&state,packet,1024,3560,1)==M16_LIVE_BUSY && pcm[0]==held_p);
    check(mic16_live_begin_reference(&state)==M16_LIVE_BUSY);
    c=config(18);check(mic16_live_configure(&state,&c)==M16_LIVE_BUSY);
    check(mic16_live_release(&state,&v)==0);old=v;
    check(!mic16_live_view_current(&state,&old));
    cases++; /* 下一个不重叠窗保留时段与独立参考。 */
    epoch=state.epoch;audio_ready(3560,0);check(mic16_live_take(&state,&v)==0);
    check(v.start_sample==40960 && v.end_sample==81920 && v.epoch==epoch && reference[0]==held_r);
    check(mic16_live_release(&state,&old)==M16_LIVE_INVALID && state.leased);
    check(mic16_live_release(&state,&v)==0);
    cases++; /* 跳包或重复包丢弃部分音频。 */
    pattern(6120);check(mic16_live_push(&state,packet,1024,6120,1)==0);
    check(mic16_live_push(&state,packet,1024,6122,1)==M16_LIVE_GAP);
    check(state.pcm_samples==0 && state.reference_samples==8192 && state.mode==0);
    check(mic16_live_take(&state,&v)==M16_LIVE_STATE);
    check(mic16_live_begin_audio(&state)==0);pattern(4);
    check(mic16_live_push(&state,packet,1024,4,1)==0);
    check(mic16_live_push(&state,packet,1024,4,1)==M16_LIVE_GAP);
    cases++; /* 异常或传输无效包不能部分写入。 */
    check(mic16_live_begin_audio(&state)==0);held_p=pcm[0];
    check(mic16_live_push(&state,packet,1023,0,1)==M16_LIVE_INVALID && pcm[0]==held_p);
    check(mic16_live_begin_audio(&state)==0);
    check(mic16_live_push(&state,packet,1024,0,0)==M16_LIVE_INVALID);
    check(mic16_live_begin_audio(&state)==0);
    check(mic16_live_push(&state,(const unsigned char *)pcm,1024,0,1)==M16_LIVE_INVALID);
    cases++; /* 末字越界时不得先写入首字。 */
    check(mic16_live_begin_audio(&state)==0);pattern(0);put(255,262145u);held_p=pcm[0];
    check(mic16_live_push(&state,packet,1024,0,1)==M16_LIVE_RANGE && pcm[0]==held_p);
    cases++; /* 参考采集出现缺口则作废，不自动替换安静参考。 */
    check(mic16_live_begin_reference(&state)==0);pattern(0);
    check(mic16_live_push(&state,packet,1024,0,1)==0);
    check(mic16_live_push(&state,packet,1024,2,1)==M16_LIVE_GAP);
    check(state.reference_samples==0 && mic16_live_begin_audio(&state)==M16_LIVE_STATE);
    cases++; /* 数据被借用时取消，只作废结果，不改数组。 */
    reference_ready();audio_ready(0,1);check(mic16_live_take(&state,&v)==0);
    held_p=pcm[0];held_r=reference[0];check(mic16_live_interrupt(&state,1)==0);
    check(!mic16_live_view_current(&state,&v) && pcm[0]==held_p && reference[0]==held_r);
    check(mic16_live_begin_reference(&state)==M16_LIVE_BUSY);
    check(mic16_live_release(&state,&v)==M16_LIVE_GAP);
    check(mic16_live_begin_audio(&state)==M16_LIVE_STATE);
    cases++; /* 姿态、通道或幅值更新后必须重新采独立参考。 */
    reference_ready();c=config(19);c.context_id=98;
    check(mic16_live_configure(&state,&c)==0 && state.reference_samples==0);
    check(mic16_live_begin_audio(&state)==M16_LIVE_STATE);
    cases++; /* 有效的全零负载不构成虚构静音门控。 */
    init(18);check(mic16_live_begin_reference(&state)==0);memset(packet,0,sizeof(packet));
    for(i=0;i<512;i++)rc=mic16_live_push(&state,packet,1024,i,1);
    check(rc==M16_LIVE_REFERENCE_READY && reference[8191*16+15]==0.0f);
    cases++; /* 同步接口使用测试替身，但链接真实冻结源记忆。 */
    init(18);reference_ready();audio_ready(0,1);mock_behavior=0;mic16_memory_init(&memory);
    check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==0);
    check(mock_calls==1 && memory.count==1 && event.new_source_valid && memory.cycle==0);
    audio_ready(2560,0);
    check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==0);
    check(mock_calls==2 && memory.cycle==1 && !event.new_source_valid);
    check(mic16_live_interrupt(&state,0)==0);audio_ready(9000,1);
    check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==0);
    check(mock_calls==3 && memory.cycle==0 && event.new_source_valid);
    cases++; /* 推理失败则停止、释放所有权并清空记忆。 */
    audio_ready(11560,0);mock_behavior=1;
    check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==-10);
    check(!state.leased && state.mode==0 && memory.count==0 && !binding.has_epoch);
    cases++; /* 回调中取消的结果不得进入状态。 */
    audio_ready(0,1);mock_behavior=2;
    check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==M16_LIVE_GAP);
    check(!state.leased && memory.count==0 && !binding.has_epoch);
    cases++; /* 输入不完整时不调用数值流水线。 */
    i=mock_calls;check(mic16_live_run(&state,&binding,&memory,&observation,&workspace,&event)==M16_LIVE_STATE);
    check(mock_calls==i);
    printf("LIVE_INPUT_HOST_RESULT cases=%u checks=%u errors=%u model_forwards=0 pipeline_test_double_calls=%u\n",cases,checks,errors,mock_calls);
    return errors ? 1 : 0;
}
