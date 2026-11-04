#include "mic16_pipeline_unit.h"
extern int printf(const char *,...);
static mic16_pipeline_profile phases;
static unsigned long long tick;
static unsigned long long counter(void *context){(void)context;return ++tick;}
int measured_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
    const mic16_profile_clock clock={0,counter};
    return mic16_pipeline_observe_timed(pcm,reference,o,w,&clock,&phases);
}
int main(void){
    unsigned i;
    for(i=0;i<2;i++){
        mic16_pipeline_test_prepare(i);
        if(mic16_pipeline_test_compute())return 1;
        if(phases.completed_mask!=63 || !phases.clock_ok)return 2;
        if(mic16_pipeline_test_check(i))return 3;
    }
    printf("CPU_BENCHMARK_HOST_PASS cases=2 checks=%u errors=%u fake_clock_not_ARM_timing=1\n",
        g_mic16_pipeline_test.checks,g_mic16_pipeline_test.errors);
    return g_mic16_pipeline_test.errors?4:0;
}
