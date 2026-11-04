/* 两项精确优化分别开关，先正序后反序；仅固定数值输入，不采音、不启网。 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_cooperate.h"
#include "mic16_fft_fast.h"
typedef struct {unsigned rep,fixture,mode,differences;uint64_t total;mic16_pipeline_profile profile;unsigned calls[3],reserved;} fast_run;
typedef struct {unsigned signature,version,stage,completed,errors,reserved;uint64_t timer_hz;fast_run runs[16];} fast_benchmark;
volatile fast_benchmark g_mic16_feature_fast_benchmark;
const unsigned g_mic16_feature_fast_contract[10]={sizeof(fast_benchmark),sizeof(fast_run),offsetof(fast_run,total),offsetof(fast_run,profile),offsetof(fast_run,calls),offsetof(fast_benchmark,runs),16,61192,sizeof(mic16_pipeline_profile),4};
static mic16_pipeline_observation saved[2];
static mic16_pipeline_observation *last;
static mic16_pipeline_profile profile;
static uint64_t tick(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){const mic16_profile_clock clock={0,tick};last=o;return mic16_pipeline_observe_batched_timed(x,q,o,w,&clock,&profile);}
int main(void){unsigned rep,fixture,order,i,mode;uint64_t begin,end;int rc;Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_feature_fast_benchmark.signature=0x4d464153u;g_mic16_feature_fast_benchmark.version=0x20261022u;g_mic16_feature_fast_benchmark.timer_hz=COUNTS_PER_SECOND;g_mic16_feature_fast_benchmark.stage=1;
 printf("\r\nMIC16 FEATURE FAST EXACT_COMPUTE_SAME_INPUT\r\nFAST_CONTRACT timer_hz=%llu expected_checks=61192 checkpoint=%s\r\n",(unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 m16_fft_fast_select(0);mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();if(rc){g_mic16_feature_fast_benchmark.errors++;goto done;}
 g_mic16_feature_fast_benchmark.stage=2;
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<4;order++){
  unsigned differences=0;volatile fast_run *r=g_mic16_feature_fast_benchmark.runs+g_mic16_feature_fast_benchmark.completed;
  mode=rep?3-order:order;m16_fft_fast_select(mode);mic16_pipeline_test_prepare(fixture);
  printf("FAST_START rep=%u case=%u mode=%u\r\n",rep,fixture,mode);begin=tick(0);rc=mic16_pipeline_test_compute();end=tick(0);
  if(rc){printf("FAST_COMPUTE_FAIL rc=%d\r\n",rc);g_mic16_feature_fast_benchmark.errors++;goto done;}
  if(!rep && !mode)memcpy(saved+fixture,last,sizeof(*last));else for(i=0;i<sizeof(*last);i++)differences+=((unsigned char *)(saved+fixture))[i]!=((unsigned char *)last)[i];
  if(differences || end<begin || !profile.clock_ok || profile.reads!=12 || profile.completed_mask!=63 || profile.attempted_mask!=63 || profile.pipeline_rc)g_mic16_feature_fast_benchmark.errors++;
  r->rep=rep;r->fixture=fixture;r->mode=mode;r->differences=differences;r->total=end-begin;r->profile=profile;for(i=0;i<3;i++)r->calls[i]=m16_feature_fast_calls[i];
  printf("FFT_COUNTS front=%u mono=%u stats=%u\r\n",m16_fft_fast_calls[0],m16_fft_fast_calls[1],m16_fft_fast_calls[2]);
  mic16_pipeline_test_check(fixture);g_mic16_feature_fast_benchmark.completed++;
  printf("FAST_PHASE rep=%u case=%u mode=%u total_ticks=%llu feature_ticks=%llu differences=%u beam_fast=%u beam_fallback=%u mono_fast=%u\r\n",rep,fixture,mode,(unsigned long long)(end-begin),profile.ticks[4],differences,m16_feature_fast_calls[0],m16_feature_fast_calls[1],m16_feature_fast_calls[2]);
 }
done:
 g_mic16_feature_fast_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_feature_fast_benchmark.completed!=16 || g_mic16_pipeline_test.checks!=61192)g_mic16_feature_fast_benchmark.errors++;
 g_mic16_feature_fast_benchmark.stage=g_mic16_feature_fast_benchmark.errors?4:3;
 Xil_DCacheFlushRange((INTPTR)&g_mic16_feature_fast_benchmark,sizeof(g_mic16_feature_fast_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("FAST_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_feature_fast_benchmark.errors?"FAIL":"PASS",g_mic16_feature_fast_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_feature_fast_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
