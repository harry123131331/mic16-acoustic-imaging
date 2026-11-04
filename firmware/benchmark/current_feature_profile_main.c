/* 特征子阶段独立计时；同输入、同HLS后端，交换当前批次索引分支/插桩路径的顺序。
 * 完整观测逐字节对照放在计时之外；仅用原固定数值夹具，不采音、不启网。 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_feature_hotspots.h"
int mic16_profile_observe_batched_timed(const float *,const float *,mic16_pipeline_observation *,
 mic16_pipeline_workspace *,const mic16_profile_clock *,mic16_pipeline_profile *);
typedef struct {
 unsigned repetition,fixture,instrumented,byte_differences;
 uint64_t total_ticks;
 mic16_pipeline_profile pipeline;
 mic16_hotspot_report hotspot;
} hotspot_run;
typedef struct {
 unsigned signature,version,stage,completed,errors,reserved;
 uint64_t timer_hz;
 hotspot_run runs[8];
} hotspot_benchmark;
volatile hotspot_benchmark g_mic16_current_feature_benchmark;
const unsigned g_mic16_current_feature_contract[10]={sizeof(hotspot_benchmark),sizeof(hotspot_run),sizeof(mic16_pipeline_profile),
 sizeof(mic16_hotspot_report),offsetof(hotspot_run,total_ticks),offsetof(hotspot_run,pipeline),offsetof(hotspot_run,hotspot),
 offsetof(hotspot_benchmark,runs),offsetof(mic16_hotspot_report,items),M16_HOT_COUNT};
static mic16_pipeline_observation baseline[2];
static unsigned baseline_valid[2],instrumented;
static mic16_pipeline_profile current_profile;
static mic16_hotspot_report current_hotspot;
static mic16_pipeline_observation *last_observation;
static uint64_t tick(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *pcm,const float *reference,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 const mic16_profile_clock clock={0,tick};int rc;
 last_observation=o;
 if(instrumented){
  mic16_hotspot_reset(tick,0);
  rc=mic16_profile_observe_batched_timed(pcm,reference,o,w,&clock,&current_profile);
  if(mic16_hotspot_finish(&current_hotspot))return -90;
 }else rc=mic16_pipeline_observe_batched_timed(pcm,reference,o,w,&clock,&current_profile);
 return rc;
}
int main(void){
 unsigned rep,fixture,order,i,j;uint64_t begin,end;int rc;
 Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_current_feature_benchmark.signature=0x4d484f54u;g_mic16_current_feature_benchmark.version=0x20261022u;
 g_mic16_current_feature_benchmark.timer_hz=COUNTS_PER_SECOND;g_mic16_current_feature_benchmark.stage=1;
 printf("\r\nMIC16 CURRENT FEATURE PROFILE SAME_INPUT_NOT_LIVE\r\n");
 printf("CURRENT_HOTSPOT_CONTRACT timer_hz=%llu items=%u order=AB_BA expected_golden_checks=30596 checkpoint=%s\r\n",
  (unsigned long long)COUNTS_PER_SECOND,M16_HOT_COUNT,mic16_frozen_checkpoint_sha256());
 instrumented=0;mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();
 if(rc){g_mic16_current_feature_benchmark.errors++;goto done;}
 g_mic16_current_feature_benchmark.stage=2;
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<2;order++){
  volatile hotspot_run *r=g_mic16_current_feature_benchmark.runs+g_mic16_current_feature_benchmark.completed;
  unsigned differences=0;uint64_t sum=0;
  instrumented=rep?1u-order:order;mic16_pipeline_test_prepare(fixture);
  memset(&current_hotspot,0,sizeof(current_hotspot));
  printf("CURRENT_HOTSPOT_START rep=%u case=%u instrumented=%u\r\n",rep,fixture,instrumented);
  begin=tick(0);rc=mic16_pipeline_test_compute();end=tick(0);
  if(rc){printf("CURRENT_HOTSPOT_COMPUTE_FAIL rc=%d\r\n",rc);g_mic16_current_feature_benchmark.errors++;goto done;}
  if(!baseline_valid[fixture]){
   if(instrumented){g_mic16_current_feature_benchmark.errors++;goto done;}
   memcpy(baseline+fixture,last_observation,sizeof(*last_observation));baseline_valid[fixture]=1;
  }else{
   const unsigned char *a=(const unsigned char *)(baseline+fixture),*b=(const unsigned char *)last_observation;
   for(i=0;i<sizeof(*last_observation);i++)differences+=a[i]!=b[i];
  }
  if(differences || end<begin || !current_profile.clock_ok || current_profile.reads!=12 ||
     current_profile.completed_mask!=63 || current_profile.attempted_mask!=63 || current_profile.pipeline_rc)
   g_mic16_current_feature_benchmark.errors++;
  if(instrumented){
   for(i=0;i<M16_HOT_COUNT;i++)sum+=current_hotspot.items[i].exclusive_ticks;
   if(current_hotspot.errors || current_hotspot.depth || current_hotspot.items[0].calls!=1 ||
      current_hotspot.items[0].inclusive_ticks>current_profile.ticks[MIC16_PROFILE_FEATURES] ||
      sum!=current_hotspot.items[0].inclusive_ticks)g_mic16_current_feature_benchmark.errors++;
  }
  r->repetition=rep;r->fixture=fixture;r->instrumented=instrumented;r->byte_differences=differences;
  r->total_ticks=end-begin;r->pipeline=current_profile;r->hotspot=current_hotspot;
  mic16_pipeline_test_check(fixture);g_mic16_current_feature_benchmark.completed++;
  printf("CURRENT_HOTSPOT_PHASE rep=%u case=%u instrumented=%u total_ticks=%llu feature_ticks=%llu differences=%u\r\n",
   rep,fixture,instrumented,(unsigned long long)(end-begin),current_profile.ticks[MIC16_PROFILE_FEATURES],differences);
  if(instrumented){
   printf("CURRENT_HOTSPOT_META rep=%u case=%u reads=%llu depth=%u max_depth=%u errors=%u\r\n",
    rep,fixture,(unsigned long long)current_hotspot.reads,current_hotspot.depth,current_hotspot.max_depth,current_hotspot.errors);
   for(j=0;j<M16_HOT_COUNT;j++)printf("CURRENT_HOTSPOT_ITEM rep=%u case=%u id=%u calls=%llu inclusive=%llu exclusive=%llu\r\n",
    rep,fixture,j,(unsigned long long)current_hotspot.items[j].calls,
    (unsigned long long)current_hotspot.items[j].inclusive_ticks,(unsigned long long)current_hotspot.items[j].exclusive_ticks);
  }
 }
done:
 g_mic16_current_feature_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_current_feature_benchmark.completed!=8 || g_mic16_pipeline_test.checks!=30596)g_mic16_current_feature_benchmark.errors++;
 g_mic16_current_feature_benchmark.stage=g_mic16_current_feature_benchmark.errors?4:3;
 Xil_DCacheFlushRange((INTPTR)&g_mic16_current_feature_benchmark,sizeof(g_mic16_current_feature_benchmark));
 Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("CURRENT_HOTSPOT_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_current_feature_benchmark.errors?"FAIL":"PASS",
  g_mic16_current_feature_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_current_feature_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
