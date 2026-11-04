/* 独立CPU数值计时入口；不采集麦克风，不启动DMA、网络或HLS核。
 * 复用原有固定输入与黄金检查，不需要安静环境或播放设备。 */
#include <stdio.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"

typedef struct {
    unsigned signature,version,stage,current_case,completed_cases,errors;
    unsigned long long timer_hz,total_ticks[2];
    mic16_pipeline_profile phases[2];
} mic16_cpu_benchmark_report;
volatile mic16_cpu_benchmark_report g_mic16_cpu_benchmark;

static unsigned long long counter(void *context){
    XTime t;(void)context;XTime_GetTime(&t);return t;
}
int measured_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *observation,mic16_pipeline_workspace *workspace){
    mic16_pipeline_profile profile;const mic16_profile_clock clock={0,counter};
    int rc=mic16_pipeline_observe_timed(pcm,reference,observation,workspace,&clock,&profile);
    g_mic16_cpu_benchmark.phases[g_mic16_cpu_benchmark.current_case]=profile;
    return rc;
}
int main(void){
    unsigned i,k;int rc;XTime begin,end;
    Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
    g_mic16_cpu_benchmark.signature=0x4d42454eu;
    g_mic16_cpu_benchmark.version=0x20261019u;
    g_mic16_cpu_benchmark.stage=1;g_mic16_cpu_benchmark.timer_hz=COUNTS_PER_SECOND;
    printf("\r\nMIC16 CPU NUMERICAL BENCHMARK NOT_LIVE_AUDIO\r\n");
    printf("checkpoint=%s timer_hz=%llu result=0x%08lx\r\n",mic16_frozen_checkpoint_sha256(),
        (unsigned long long)COUNTS_PER_SECOND,(unsigned long)&g_mic16_cpu_benchmark);
    for(i=0;i<2;i++){
        g_mic16_cpu_benchmark.current_case=i;mic16_pipeline_test_prepare(i);
        printf("CPU_BENCHMARK_START case=%u\r\n",i);
        XTime_GetTime(&begin);rc=mic16_pipeline_test_compute();XTime_GetTime(&end);
        g_mic16_cpu_benchmark.total_ticks[i]=end-begin;
        if(rc){g_mic16_cpu_benchmark.errors++;break;}
        if(end<begin || !g_mic16_cpu_benchmark.phases[i].clock_ok ||
            g_mic16_cpu_benchmark.phases[i].completed_mask!=63u ||
            g_mic16_cpu_benchmark.phases[i].reads!=12u)
            g_mic16_cpu_benchmark.errors++;
        mic16_pipeline_test_check(i);g_mic16_cpu_benchmark.completed_cases++;
        printf("CPU_BENCHMARK_PHASE case=%u total_ticks=%llu clock_ok=%u complete=%u ticks=",i,
            (unsigned long long)(end-begin),g_mic16_cpu_benchmark.phases[i].clock_ok,
            g_mic16_cpu_benchmark.phases[i].completed_mask);
        for(k=0;k<MIC16_PROFILE_STAGES;k++)printf("%s%llu",k?",":"",g_mic16_cpu_benchmark.phases[i].ticks[k]);
        printf("\r\n");
    }
    g_mic16_cpu_benchmark.errors+=g_mic16_pipeline_test.errors;
    g_mic16_cpu_benchmark.stage=g_mic16_cpu_benchmark.errors?3:2;
    Xil_DCacheFlushRange((INTPTR)&g_mic16_cpu_benchmark,sizeof(g_mic16_cpu_benchmark));
    Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
    printf("CPU_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_ACOUSTIC_ACCURACY_OR_REALTIME_ACCEPTANCE\r\n",
        g_mic16_cpu_benchmark.errors?"FAIL":"PASS",g_mic16_cpu_benchmark.completed_cases,
        g_mic16_pipeline_test.checks,g_mic16_cpu_benchmark.errors);
    for(;;){__asm__ volatile("nop");}
}
