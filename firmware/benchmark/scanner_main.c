/* 固定输入CPU/HLS对照；不启用真实采集、网络、增强或持续调度。 */
#include <stdio.h>
#include <string.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_scanner_hls.h"
static m16_scanner_workspace scanner __attribute__((aligned(64)));
static float reference_maps[M16_SP_MAP_FLOATS] __attribute__((aligned(64)));
static unsigned use_hls,case_id,local_errors;
static m16_scanner_report scanner_report;
static mic16_pipeline_profile profile;
static uint64_t core_cpu_ticks;
static double map_max_error,map_squared_error;
static unsigned changed_best;
static m16_scanner_refinement refinement;
static uint64_t refinement_ticks,projection_ticks;
int mic16_cpu_spatial_project(const float *,float *,mic16_spatial_workspace *);
static uint64_t ticks(void *context){XTime t;(void)context;XTime_GetTime(&t);return t;}
int mic16_spatial_project(const float *x,float *maps,mic16_spatial_workspace *w){
    uint64_t begin,end;unsigned i,v,g,a,b;int rc;
    if(!use_hls){
        begin=ticks(0);rc=mic16_cpu_spatial_project(x,maps,w);end=ticks(0);
        core_cpu_ticks=end-begin;if(!rc)memcpy(reference_maps,maps,sizeof(reference_maps));return rc;
    }
    begin=ticks(0);rc=m16_scanner_project(&scanner,x,maps,&scanner_report);if(rc)return rc;
    end=ticks(0);rc=m16_scanner_refine(x,maps,w,&refinement);
    refinement_ticks=ticks(0)-end;projection_ticks=ticks(0)-begin;if(rc)return rc;
    map_max_error=0;map_squared_error=0;changed_best=0;
    for(i=0;i<M16_SP_MAP_FLOATS;i++){
        double d=(double)maps[i]-(double)reference_maps[i];if(d<0)d=-d;
        if(d>map_max_error)map_max_error=d;map_squared_error+=d*d;
    }
    for(v=0;v<480;v++){
        a=b=0;for(g=1;g<325;g++){
            if(maps[v*325+g]>maps[v*325+a])a=g;
            if(reference_maps[v*325+g]>reference_maps[v*325+b])b=g;
        }
        if(a!=b)changed_best++;
    }
    /* 在第一次实板运行之前固定门限；失败不放宽，不自动替换CPU基线。 */
    if(map_max_error>1e-4 || changed_best)local_errors++;
    return 0;
}
int measured_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *observation,mic16_pipeline_workspace *workspace){
    const mic16_profile_clock clock={0,ticks};
    return mic16_pipeline_observe_timed(pcm,reference,observation,workspace,&clock,&profile);
}
int main(void){
    unsigned i,k;uint64_t begin,cpu_total,hls_total,init_ticks;int rc;
    Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
    printf("\r\nMIC16 SCANNER HLS BENCHMARK FIXED_NUMERICAL_INPUT\r\n");
    printf("SCANNER_CONTRACT q=24 frames=20 zones=24 grids=325 pairs=32 bins=807 max_map_error=0.0001 require_same_best=1 timer_hz=%llu\r\n",(unsigned long long)COUNTS_PER_SECOND);
    begin=ticks(0);rc=m16_scanner_platform_init(&scanner);init_ticks=ticks(0)-begin;
    if(rc){printf("SCANNER_INIT_FAIL rc=%d\r\n",rc);for(;;){}}
    printf("SCANNER_INIT_PASS arena=0x%08lx init_ticks=%llu\r\n",(unsigned long)scanner.io.arena_address,(unsigned long long)init_ticks);
    for(i=0;i<2;i++){
        case_id=i;use_hls=0;mic16_pipeline_test_prepare(i);
        printf("SCANNER_CPU_START case=%u\r\n",case_id);
        begin=ticks(0);rc=mic16_pipeline_test_compute();cpu_total=ticks(0)-begin;
        if(rc){printf("SCANNER_CPU_FAIL rc=%d\r\n",rc);local_errors++;break;}
        mic16_pipeline_test_check(i);
        if(!profile.clock_ok || profile.completed_mask!=63u)local_errors++;
        use_hls=1;mic16_pipeline_test_prepare(i);
        printf("SCANNER_PL_START case=%u\r\n",case_id);
        begin=ticks(0);rc=mic16_pipeline_test_compute();hls_total=ticks(0)-begin;
        if(rc){printf("SCANNER_PL_FAIL case=%u rc=%d frames=%u ctrl=%08x quarantine=%u\r\n",i,rc,scanner_report.completed_frames,scanner_report.last_control,scanner.quarantined);local_errors++;break;}
        mic16_pipeline_test_check(i);
        if(!profile.clock_ok || profile.completed_mask!=63u || scanner_report.completed_frames!=20u)local_errors++;
        printf("SCANNER_COMPARE case=%u cpu_total_ticks=%llu hls_total_ticks=%llu cpu_core_ticks=%llu adapter_ticks=%llu pack_ticks=%llu device_ticks=%llu decode_ticks=%llu frames=%u max_map_error=%.10g map_mse=%.10g changed_best=%u\r\n",i,
            (unsigned long long)cpu_total,(unsigned long long)hls_total,(unsigned long long)core_cpu_ticks,
            (unsigned long long)scanner_report.total_ticks,(unsigned long long)scanner_report.packing_ticks,
            (unsigned long long)scanner_report.device_ticks,(unsigned long long)scanner_report.decoding_ticks,
            scanner_report.completed_frames,map_max_error,map_squared_error/(double)M16_SP_MAP_FLOATS,changed_best);
        printf("SCANNER_PIPELINE_PHASE case=%u ticks=",i);
        for(k=0;k<MIC16_PROFILE_STAGES;k++)printf("%s%llu",k?",":"",(unsigned long long)profile.ticks[k]);printf("\r\n");
        printf("SCANNER_REFINEMENT case=%u projection_ticks=%llu refinement_ticks=%llu refined_grids=%u full_zones=%u zero_input=%u\r\n",i,(unsigned long long)projection_ticks,(unsigned long long)refinement_ticks,refinement.refined_grids,refinement.full_zones,refinement.zero_input);
    }
    local_errors+=g_mic16_pipeline_test.errors;
    printf("SCANNER_NUMERIC_MAXIMA feature=%.12g logit=%.12g direction=%.12g score=%.12g\r\n",g_mic16_pipeline_test.feature_max_error,g_mic16_pipeline_test.logit_max_error,g_mic16_pipeline_test.direction_max_error,g_mic16_pipeline_test.score_max_error);
    printf("SCANNER_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_CONTINUOUS_REALTIME\r\n",local_errors?"FAIL":"PASS",i,g_mic16_pipeline_test.checks,local_errors);
    for(;;){__asm__ volatile("nop");}
}
