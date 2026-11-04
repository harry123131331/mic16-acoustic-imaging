/* 索引优化基线与STFT批次版的AB/BA；完整观测与独立STFT计时在检测计时之外。 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
typedef struct {
 unsigned signature,version,stage,repetition,fixture,batched,completed,errors;
 unsigned long long timer_hz,total[2][2][2];
 mic16_pipeline_profile phases[2][2][2];
 unsigned differences[8];unsigned long long stft_ticks[8];
} stft_benchmark;
volatile stft_benchmark g_mic16_stft_benchmark;
const unsigned g_mic16_stft_benchmark_contract[6]={sizeof(stft_benchmark),offsetof(stft_benchmark,total),
 offsetof(stft_benchmark,phases),offsetof(stft_benchmark,differences),offsetof(stft_benchmark,stft_ticks),sizeof(mic16_pipeline_profile)};
static mic16_pipeline_observation baseline[2],*last_observation;
static mic16_pipeline_workspace *last_work;
static const float *last_pcm,*last_reference;
static unsigned saved[2];
static unsigned long long counter(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *pcm,const float *reference,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 const mic16_profile_clock clock={0,counter};mic16_pipeline_profile p;int rc;
 rc=g_mic16_stft_benchmark.batched?mic16_pipeline_observe_batched_timed(pcm,reference,o,w,&clock,&p):
  mic16_pipeline_observe_indexed_timed(pcm,reference,o,w,&clock,&p);
 g_mic16_stft_benchmark.phases[g_mic16_stft_benchmark.repetition][g_mic16_stft_benchmark.fixture][g_mic16_stft_benchmark.batched]=p;
 last_pcm=pcm;last_reference=reference;last_work=w;last_observation=o;return rc;
}
static int consume_test(unsigned t,const double *s,void *context){(void)t;(void)s;(void)context;return 0;}
static int transforms(unsigned batched){
 unsigned t;mic16_extended_workspace *w=&last_work->extended;
 if(batched)return mic16_stft512_visit(w->resampled,16778,67,w,consume_test,0);
 for(t=0;t<67;t++)if(mic16_stft512_frame(w->resampled,16778,t,w))return -1;
 return 0;
}
int main(void){
 unsigned rep,fixture,order,batched,k,i,index,differences;unsigned long long begin,end,micro;int rc;
 Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_stft_benchmark.signature=0x4d424958u;g_mic16_stft_benchmark.version=0x20261020u;
 g_mic16_stft_benchmark.timer_hz=COUNTS_PER_SECOND;
 printf("\r\nMIC16 STFT BATCH BENCHMARK SAME_INDEXED_BEAM_SAME_HLS\r\n");
 printf("STFT_BENCHMARK_CONTRACT timer_hz=%llu order=AB_BA checkpoint=%s expected_golden_checks=30596\r\n",
  (unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 g_mic16_stft_benchmark.stage=1;mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();
 if(rc){g_mic16_stft_benchmark.errors++;goto done;}
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<2;order++){
  const volatile mic16_pipeline_profile *p;
  batched=rep?1u-order:order;index=rep*4+fixture*2+batched;
  g_mic16_stft_benchmark.repetition=rep;g_mic16_stft_benchmark.fixture=fixture;g_mic16_stft_benchmark.batched=batched;
  mic16_pipeline_test_prepare(fixture);printf("STFT_BENCHMARK_START rep=%u case=%u batched=%u\r\n",rep,fixture,batched);
  g_mic16_stft_benchmark.stage=2;begin=counter(0);rc=mic16_pipeline_test_compute();end=counter(0);
  g_mic16_stft_benchmark.total[rep][fixture][batched]=end-begin;
  p=&g_mic16_stft_benchmark.phases[rep][fixture][batched];
  if(rc || end<begin || !p->clock_ok || p->reads!=12 || p->attempted_mask!=63 || p->completed_mask!=63){g_mic16_stft_benchmark.errors++;goto done;}
  differences=0;
  if(!saved[fixture]){if(batched){g_mic16_stft_benchmark.errors++;goto done;}memcpy(baseline+fixture,last_observation,sizeof(*last_observation));saved[fixture]=1;}
  else for(i=0;i<sizeof(*last_observation);i++)differences+=((unsigned char *)(baseline+fixture))[i]!=((unsigned char *)last_observation)[i];
  g_mic16_stft_benchmark.differences[index]=differences;if(differences)g_mic16_stft_benchmark.errors++;
  mic16_pipeline_test_check(fixture);
  for(i=0;i<16778;i++)last_work->extended.resampled[i]=(double)((int)(i%257)-128)/256.;
  begin=counter(0);rc=transforms(batched);micro=counter(0)-begin;if(rc){g_mic16_stft_benchmark.errors++;goto done;}
  g_mic16_stft_benchmark.stft_ticks[index]=micro;g_mic16_stft_benchmark.completed++;
  printf("STFT_BENCHMARK_PHASE rep=%u case=%u batched=%u total_ticks=%llu stft_ticks=%llu differences=%u ticks=",rep,fixture,batched,g_mic16_stft_benchmark.total[rep][fixture][batched],micro,differences);
  /* 总计数直接从已保存结果输出，避免微基准复用局部begin变量。 */
  for(k=0;k<6;k++)printf("%s%llu",k?",":"",p->ticks[k]);
  printf("\r\n");
 }
done:
 g_mic16_stft_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_stft_benchmark.completed!=8 || g_mic16_pipeline_test.checks!=30596)g_mic16_stft_benchmark.errors++;
 g_mic16_stft_benchmark.stage=g_mic16_stft_benchmark.errors?4:3;
 Xil_DCacheFlushRange((INTPTR)&g_mic16_stft_benchmark,sizeof(g_mic16_stft_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("STFT_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_stft_benchmark.errors?"FAIL":"PASS",
  g_mic16_stft_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_stft_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
