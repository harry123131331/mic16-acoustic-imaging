/* 原有固定数值基准分别运行保留实现和复用实现；原门限、黄金值及容差不变。 */
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern int memcmp(const void *,const void *,size_t);
static unsigned mode,extra_errors;
static unsigned long long tick;
static mic16_pipeline_observation baseline;
static unsigned long long counter(void *unused){(void)unused;return ++tick;}
int measured_pipeline_observe(const float *pcm,const float *quiet,mic16_pipeline_observation *out,mic16_pipeline_workspace *w){
    mic16_profile_clock clock={0,counter};mic16_pipeline_profile profile;
    int rc=mode?mic16_pipeline_observe_batched_timed(pcm,quiet,out,w,&clock,&profile):
        mic16_pipeline_observe_indexed_timed(pcm,quiet,out,w,&clock,&profile);
    if(!profile.clock_ok || profile.reads!=12 || profile.completed_mask!=63 || profile.attempted_mask!=63)extra_errors++;
    if(!rc){if(!mode)memcpy(&baseline,out,sizeof(baseline));else if(memcmp(&baseline,out,sizeof(baseline)))extra_errors++;}
    return rc;
}
int main(void){
    unsigned fixture;
    for(fixture=0;fixture<2;fixture++)for(mode=0;mode<2;mode++){
        mic16_pipeline_test_prepare(fixture);
        if(mic16_pipeline_test_compute())return 2;
        if(mic16_pipeline_test_check(fixture))return 3;
    }
    extra_errors+=g_mic16_pipeline_test.errors;
    printf("STFT_BATCH_GOLDEN_%s cases=%u checks=%u errors=%u full_observation_bit_differences=0 ONLY_IF_NO_ERRORS\n",
        extra_errors?"FAIL":"PASS",g_mic16_pipeline_test.completed_cases,g_mic16_pipeline_test.checks,extra_errors);
    return extra_errors?1:0;
}
