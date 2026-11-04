/* 同输入、同批次特征与同HLS核，只切换CPU精算累计值存放方式。 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_scanner_accumulators.h"
typedef struct {
 unsigned signature,version,stage,repetition,fixture,accumulators,completed,errors;
 unsigned long long timer_hz,total[2][2][2];
 mic16_pipeline_profile phases[2][2][2];
 unsigned differences[8],refined_grids[8],full_zones[8],zero_input[8];
 unsigned long long refinement_ticks[8];
} accumulator_benchmark;
volatile accumulator_benchmark g_mic16_scanner_accumulator_benchmark;
const unsigned g_mic16_scanner_accumulator_contract[10]={sizeof(accumulator_benchmark),offsetof(accumulator_benchmark,total),
 offsetof(accumulator_benchmark,phases),offsetof(accumulator_benchmark,differences),offsetof(accumulator_benchmark,refined_grids),
 offsetof(accumulator_benchmark,full_zones),offsetof(accumulator_benchmark,zero_input),offsetof(accumulator_benchmark,refinement_ticks),sizeof(mic16_pipeline_profile),sizeof(m16_scanner_refinement)};
static mic16_pipeline_observation baseline[2],*last_observation;
static unsigned saved[2];
static unsigned long long counter(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int m16_scanner_refine_benchmark_dispatch(const float *x,float *maps,mic16_spatial_workspace *w,m16_scanner_refinement *r){
 unsigned index=g_mic16_scanner_accumulator_benchmark.repetition*4+g_mic16_scanner_accumulator_benchmark.fixture*2+g_mic16_scanner_accumulator_benchmark.accumulators;
 unsigned long long start=counter(0),end;int rc;
 rc=g_mic16_scanner_accumulator_benchmark.accumulators?m16_scanner_refine_accumulators(x,maps,w,r):m16_scanner_refine(x,maps,w,r);
 end=counter(0);if(end<start)return M16_SCAN_CLOCK;
 g_mic16_scanner_accumulator_benchmark.refinement_ticks[index]=end-start;
 if(!rc){g_mic16_scanner_accumulator_benchmark.refined_grids[index]=r->refined_grids;g_mic16_scanner_accumulator_benchmark.full_zones[index]=r->full_zones;g_mic16_scanner_accumulator_benchmark.zero_input[index]=r->zero_input;}
 return rc;
}
int measured_pipeline_observe(const float *pcm,const float *reference,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 const mic16_profile_clock clock={0,counter};mic16_pipeline_profile p;int rc;
 rc=mic16_pipeline_observe_batched_timed(pcm,reference,o,w,&clock,&p);
 g_mic16_scanner_accumulator_benchmark.phases[g_mic16_scanner_accumulator_benchmark.repetition][g_mic16_scanner_accumulator_benchmark.fixture][g_mic16_scanner_accumulator_benchmark.accumulators]=p;
 last_observation=o;return rc;
}
int main(void){
 unsigned rep,fixture,order,mode,k,i,index,differences;unsigned long long begin,end;int rc;
 Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_scanner_accumulator_benchmark.signature=0x53434143u;g_mic16_scanner_accumulator_benchmark.version=0x20261021u;
 g_mic16_scanner_accumulator_benchmark.timer_hz=COUNTS_PER_SECOND;
 printf("\r\nMIC16 SCANNER ACCUMULATORS BENCHMARK SAME_BATCHED_SAME_HLS\r\n");
 printf("ACCUM_BENCHMARK_CONTRACT timer_hz=%llu order=AB_BA checkpoint=%s expected_golden_checks=30596\r\n",(unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 g_mic16_scanner_accumulator_benchmark.stage=1;mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();
 if(rc){g_mic16_scanner_accumulator_benchmark.errors++;goto done;}
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<2;order++){
  const volatile mic16_pipeline_profile *p;
  mode=rep?1u-order:order;index=rep*4+fixture*2+mode;
  g_mic16_scanner_accumulator_benchmark.repetition=rep;g_mic16_scanner_accumulator_benchmark.fixture=fixture;g_mic16_scanner_accumulator_benchmark.accumulators=mode;
  mic16_pipeline_test_prepare(fixture);printf("ACCUM_BENCHMARK_START rep=%u case=%u mode=%u\r\n",rep,fixture,mode);
  g_mic16_scanner_accumulator_benchmark.stage=2;begin=counter(0);rc=mic16_pipeline_test_compute();end=counter(0);
  g_mic16_scanner_accumulator_benchmark.total[rep][fixture][mode]=end-begin;
  p=&g_mic16_scanner_accumulator_benchmark.phases[rep][fixture][mode];
  if(rc || end<begin || !p->clock_ok || p->reads!=12 || p->attempted_mask!=63 || p->completed_mask!=63){g_mic16_scanner_accumulator_benchmark.errors++;goto done;}
  differences=0;
  if(!saved[fixture]){if(mode){g_mic16_scanner_accumulator_benchmark.errors++;goto done;}memcpy(baseline+fixture,last_observation,sizeof(*last_observation));saved[fixture]=1;}
  else for(i=0;i<sizeof(*last_observation);i++)differences+=((unsigned char *)(baseline+fixture))[i]!=((unsigned char *)last_observation)[i];
  g_mic16_scanner_accumulator_benchmark.differences[index]=differences;if(differences)g_mic16_scanner_accumulator_benchmark.errors++;
  mic16_pipeline_test_check(fixture);g_mic16_scanner_accumulator_benchmark.completed++;
  printf("ACCUM_BENCHMARK_PHASE rep=%u case=%u mode=%u total_ticks=%llu refinement_ticks=%llu differences=%u refined=%u full_zones=%u zero=%u ticks=",rep,fixture,mode,end-begin,
   g_mic16_scanner_accumulator_benchmark.refinement_ticks[index],differences,g_mic16_scanner_accumulator_benchmark.refined_grids[index],g_mic16_scanner_accumulator_benchmark.full_zones[index],g_mic16_scanner_accumulator_benchmark.zero_input[index]);
  for(k=0;k<6;k++)printf("%s%llu",k?",":"",p->ticks[k]);printf("\r\n");
 }
done:
 g_mic16_scanner_accumulator_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_scanner_accumulator_benchmark.completed!=8 || g_mic16_pipeline_test.checks!=30596)g_mic16_scanner_accumulator_benchmark.errors++;
 g_mic16_scanner_accumulator_benchmark.stage=g_mic16_scanner_accumulator_benchmark.errors?4:3;
 Xil_DCacheFlushRange((INTPTR)&g_mic16_scanner_accumulator_benchmark,sizeof(g_mic16_scanner_accumulator_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("ACCUM_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_scanner_accumulator_benchmark.errors?"FAIL":"PASS",g_mic16_scanner_accumulator_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_scanner_accumulator_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
