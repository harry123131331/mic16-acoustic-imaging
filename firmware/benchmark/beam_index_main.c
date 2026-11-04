/* 原复用版与仅索引优化版的AB/BA；完整观测对照和单独波束计时均在检测计时之外。 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_beam_index.h"
typedef struct {
 unsigned signature,version,stage,repetition,fixture,indexed,completed,errors;
 unsigned long long timer_hz,total[2][2][2];
 mic16_pipeline_profile phases[2][2][2];
 unsigned differences[8];unsigned long long beam_ticks[8];
} beam_benchmark;
volatile beam_benchmark g_mic16_beam_benchmark;
const unsigned g_mic16_beam_benchmark_contract[6]={sizeof(beam_benchmark),offsetof(beam_benchmark,total),
 offsetof(beam_benchmark,phases),offsetof(beam_benchmark,differences),offsetof(beam_benchmark,beam_ticks),sizeof(mic16_pipeline_profile)};
static mic16_pipeline_observation baseline[2],*last_observation;
static mic16_pipeline_workspace *last_work;
static const float *last_pcm,*last_reference;
static unsigned saved[2];
static unsigned long long counter(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *pcm,const float *reference,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 const mic16_profile_clock clock={0,counter};mic16_pipeline_profile p;int rc;
 rc=g_mic16_beam_benchmark.indexed?mic16_pipeline_observe_indexed_timed(pcm,reference,o,w,&clock,&p):
  mic16_pipeline_observe_reused_timed(pcm,reference,o,w,&clock,&p);
 g_mic16_beam_benchmark.phases[g_mic16_beam_benchmark.repetition][g_mic16_beam_benchmark.fixture][g_mic16_beam_benchmark.indexed]=p;
 last_pcm=pcm;last_reference=reference;last_work=w;last_observation=o;return rc;
}
static int beams(unsigned indexed){
 unsigned c,g,qg;float arrivals[16],lags[32];mic16_physical_workspace *p=&last_work->extended.physical;
 for(c=0;c<last_work->pool.count;c++){
  const double *d=last_work->physical_directions+c*2;
  if(mic16_candidate_geometry(d,arrivals,lags))return -1;
  if(indexed){
   if(mic16_candidate_beam_indexed(last_pcm,40960,d,p->beam,&g) || mic16_candidate_beam_indexed(last_reference,8192,d,p->quiet_beam,&qg))return -1;
  }else if(mic16_candidate_beam(last_pcm,40960,d,p->beam,&g) || mic16_candidate_beam(last_reference,8192,d,p->quiet_beam,&qg))return -1;
 }
 return 0;
}
int main(void){
 unsigned rep,fixture,order,indexed,k,i,index,differences;unsigned long long begin,end,micro;int rc;
 Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_beam_benchmark.signature=0x4d424958u;g_mic16_beam_benchmark.version=0x20261020u;
 g_mic16_beam_benchmark.timer_hz=COUNTS_PER_SECOND;
 printf("\r\nMIC16 BEAM INDEX BENCHMARK SAME_REUSED_FEATURES_SAME_HLS\r\n");
 printf("BEAM_BENCHMARK_CONTRACT timer_hz=%llu order=AB_BA checkpoint=%s expected_golden_checks=30596\r\n",
  (unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 g_mic16_beam_benchmark.stage=1;mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();
 if(rc){g_mic16_beam_benchmark.errors++;goto done;}
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<2;order++){
  const volatile mic16_pipeline_profile *p;
  indexed=rep?1u-order:order;index=rep*4+fixture*2+indexed;
  g_mic16_beam_benchmark.repetition=rep;g_mic16_beam_benchmark.fixture=fixture;g_mic16_beam_benchmark.indexed=indexed;
  mic16_pipeline_test_prepare(fixture);printf("BEAM_BENCHMARK_START rep=%u case=%u indexed=%u\r\n",rep,fixture,indexed);
  g_mic16_beam_benchmark.stage=2;begin=counter(0);rc=mic16_pipeline_test_compute();end=counter(0);
  g_mic16_beam_benchmark.total[rep][fixture][indexed]=end-begin;
  p=&g_mic16_beam_benchmark.phases[rep][fixture][indexed];
  if(rc || end<begin || !p->clock_ok || p->reads!=12 || p->attempted_mask!=63 || p->completed_mask!=63){g_mic16_beam_benchmark.errors++;goto done;}
  differences=0;
  if(!saved[fixture]){if(indexed){g_mic16_beam_benchmark.errors++;goto done;}memcpy(baseline+fixture,last_observation,sizeof(*last_observation));saved[fixture]=1;}
  else for(i=0;i<sizeof(*last_observation);i++)differences+=((unsigned char *)(baseline+fixture))[i]!=((unsigned char *)last_observation)[i];
  g_mic16_beam_benchmark.differences[index]=differences;if(differences)g_mic16_beam_benchmark.errors++;
  mic16_pipeline_test_check(fixture);
  begin=counter(0);rc=beams(indexed);micro=counter(0)-begin;if(rc){g_mic16_beam_benchmark.errors++;goto done;}
  g_mic16_beam_benchmark.beam_ticks[index]=micro;g_mic16_beam_benchmark.completed++;
  printf("BEAM_BENCHMARK_PHASE rep=%u case=%u indexed=%u total_ticks=%llu beam_ticks=%llu differences=%u ticks=",rep,fixture,indexed,g_mic16_beam_benchmark.total[rep][fixture][indexed],micro,differences);
  /* 总计数直接从已保存结果输出，避免微基准复用局部begin变量。 */
  for(k=0;k<6;k++)printf("%s%llu",k?",":"",p->ticks[k]);
  printf("\r\n");
 }
done:
 g_mic16_beam_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_beam_benchmark.completed!=8 || g_mic16_pipeline_test.checks!=30596)g_mic16_beam_benchmark.errors++;
 g_mic16_beam_benchmark.stage=g_mic16_beam_benchmark.errors?4:3;
 Xil_DCacheFlushRange((INTPTR)&g_mic16_beam_benchmark,sizeof(g_mic16_beam_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("BEAM_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_beam_benchmark.errors?"FAIL":"PASS",
  g_mic16_beam_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_beam_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
