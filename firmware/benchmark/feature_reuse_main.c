/* 同一HLS后端比较原特征与单窗复用。固定数值输入，不采集声音、不启动网络。
 * 两轮按AB/BA交换顺序，逐次保留六阶段原始计时；不能当作声学精度或持续实时验收。 */
#include <stdio.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_scanner_hls.h"
#include "mic16_feature_layout_check.h"
const unsigned g_mic16_feature_workspace_contract[3]={sizeof(mic16_prior_physical_layout),
    sizeof(mic16_physical_workspace),sizeof(mic16_statistics_reuse_storage)};
typedef struct {
    unsigned signature,version,stage,repetition,fixture,reuse,completed,errors;
    unsigned long long timer_hz,total[2][2][2];
    mic16_pipeline_profile phases[2][2][2];
} mic16_feature_benchmark_report;
volatile mic16_feature_benchmark_report g_mic16_feature_benchmark;
static unsigned long long counter(void *context){XTime t;(void)context;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *observation,mic16_pipeline_workspace *workspace){
    mic16_pipeline_profile profile;const mic16_profile_clock clock={0,counter};
    int rc=g_mic16_feature_benchmark.reuse?
        mic16_pipeline_observe_reused_timed(pcm,reference,observation,workspace,&clock,&profile):
        mic16_pipeline_observe_timed(pcm,reference,observation,workspace,&clock,&profile);
    g_mic16_feature_benchmark.phases[g_mic16_feature_benchmark.repetition]
        [g_mic16_feature_benchmark.fixture][g_mic16_feature_benchmark.reuse]=profile;
    return rc;
}
int main(void){
    unsigned rep,fixture,order,reuse,k;unsigned long long begin,end;int rc;
    Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
    g_mic16_feature_benchmark.signature=0x4d465242u;
    g_mic16_feature_benchmark.version=0x20261020u;
    g_mic16_feature_benchmark.timer_hz=COUNTS_PER_SECOND;
    printf("\r\nMIC16 FEATURE REUSE BENCHMARK FIXED_INPUT_SAME_REFINED_HLS\r\n");
    printf("FEATURE_BENCHMARK_CONTRACT timer_hz=%llu order=AB_BA checkpoint=%s expected_golden_checks=30596\r\n",
        (unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
    /* 预热触发HLS查表装载，不纳入前后计时；之后每次仍执行自己的完整流水线。 */
    g_mic16_feature_benchmark.stage=1;mic16_pipeline_test_prepare(0);
    rc=mic16_pipeline_test_compute();
    if(rc){printf("FEATURE_BENCHMARK_WARMUP_FAIL rc=%d\r\n",rc);g_mic16_feature_benchmark.errors++;goto done;}
    for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<2;order++){
        const volatile mic16_pipeline_profile *p;
        reuse=rep?(1u-order):order;
        g_mic16_feature_benchmark.repetition=rep;g_mic16_feature_benchmark.fixture=fixture;
        g_mic16_feature_benchmark.reuse=reuse;mic16_pipeline_test_prepare(fixture);
        printf("FEATURE_BENCHMARK_START rep=%u case=%u reuse=%u\r\n",rep,fixture,reuse);
        g_mic16_feature_benchmark.stage=2;begin=counter(0);rc=mic16_pipeline_test_compute();end=counter(0);
        p=&g_mic16_feature_benchmark.phases[rep][fixture][reuse];
        g_mic16_feature_benchmark.total[rep][fixture][reuse]=end-begin;
        if(rc){printf("FEATURE_BENCHMARK_COMPUTE_FAIL rc=%d\r\n",rc);g_mic16_feature_benchmark.errors++;goto done;}
        if(end<begin || !p->clock_ok || p->reads!=12 || p->completed_mask!=63 ||
            p->attempted_mask!=63)g_mic16_feature_benchmark.errors++;
        mic16_pipeline_test_check(fixture);g_mic16_feature_benchmark.completed++;
        printf("FEATURE_BENCHMARK_PHASE rep=%u case=%u reuse=%u total_ticks=%llu ticks=",rep,fixture,reuse,end-begin);
        for(k=0;k<MIC16_PROFILE_STAGES;k++)printf("%s%llu",k?",":"",p->ticks[k]);
        printf("\r\n");
    }
done:
    g_mic16_feature_benchmark.errors+=g_mic16_pipeline_test.errors;
    if(g_mic16_feature_benchmark.completed!=8 || g_mic16_pipeline_test.checks!=30596)g_mic16_feature_benchmark.errors++;
    g_mic16_feature_benchmark.stage=g_mic16_feature_benchmark.errors?4:3;
    Xil_DCacheFlushRange((INTPTR)&g_mic16_feature_benchmark,sizeof(g_mic16_feature_benchmark));
    Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
    printf("FEATURE_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_CONTINUOUS_REALTIME\r\n",
        g_mic16_feature_benchmark.errors?"FAIL":"PASS",g_mic16_feature_benchmark.completed,
        g_mic16_pipeline_test.checks,g_mic16_feature_benchmark.errors);
    for(;;){__asm__ volatile("nop");}
}
