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
#include "mic16_dataflow_fast.h"
#include "mic16_frontend_runtime.h"
typedef struct {unsigned rep,fixture,mode,differences;uint64_t total;mic16_pipeline_profile profile;unsigned calls[3],reserved;} fast_run;
typedef struct {unsigned signature,version,stage,completed,errors,reserved;uint64_t timer_hz;fast_run runs[16];} fast_benchmark;
volatile fast_benchmark g_mic16_feature_fast_benchmark;
const unsigned g_mic16_feature_fast_contract[10]={sizeof(fast_benchmark),sizeof(fast_run),offsetof(fast_run,total),offsetof(fast_run,profile),offsetof(fast_run,calls),offsetof(fast_benchmark,runs),16,61192,sizeof(mic16_pipeline_profile),4};
extern int m16_magnitude_select(unsigned);extern unsigned m16_magnitude_calls[2];
static mic16_pipeline_observation saved[2];
static mic16_pipeline_observation *last;
static mic16_pipeline_profile profile;
static uint64_t tick(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){const mic16_profile_clock clock={0,tick};last=o;m16_dataflow_begin();int rc=mic16_pipeline_observe_batched_timed(x,q,o,w,&clock,&profile);m16_dataflow_end();return rc;}
int main(void){unsigned rep,fixture,order,i,mode;uint64_t begin,end;int rc;Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_feature_fast_benchmark.signature=0x4d464153u;g_mic16_feature_fast_benchmark.version=0x20261022u;g_mic16_feature_fast_benchmark.timer_hz=COUNTS_PER_SECOND;g_mic16_feature_fast_benchmark.stage=1;
 printf("\r\nMIC16 JOINT_RUNTIME_SAME_INPUT_PENDING_MIGRATION_ACCEPTANCE\r\nFAST_CONTRACT timer_hz=%llu expected_checks=61192 checkpoint=%s\r\n",(unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 m16_fft_fast_select(0);m16_feature_fast_select(3);m16_dataflow_select(0);m16_front_hardware_select(0);m16_magnitude_select(0);mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();if(rc){g_mic16_feature_fast_benchmark.errors++;goto done;}
 g_mic16_feature_fast_benchmark.stage=2;
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<4;order++){
  unsigned differences=0;volatile fast_run *r=g_mic16_feature_fast_benchmark.runs+g_mic16_feature_fast_benchmark.completed;
  mode=rep?3-order:order;m16_fft_fast_select(mode>=1?3:0);m16_magnitude_select(mode>=2);m16_feature_fast_select(3);m16_dataflow_select(mode?2:0);m16_front_hardware_select(mode==3);mic16_pipeline_test_prepare(fixture);
  if(!rep && mode)m16_dataflow_clear_signatures();
  printf("FAST_START rep=%u case=%u mode=%u\r\n",rep,fixture,mode);begin=tick(0);rc=mic16_pipeline_test_compute();end=tick(0);
  if(rc){printf("FAST_COMPUTE_FAIL rc=%d\r\n",rc);g_mic16_feature_fast_benchmark.errors++;goto done;}
  if(!rep && !mode)memcpy(saved+fixture,last,sizeof(*last));else for(i=0;i<sizeof(*last);i++)differences+=((unsigned char *)(saved+fixture))[i]!=((unsigned char *)last)[i];
  if((differences && mode<3) || end<begin || !profile.clock_ok || profile.reads!=12 || profile.completed_mask!=63 || profile.attempted_mask!=63 || profile.pipeline_rc)g_mic16_feature_fast_benchmark.errors++;
  r->rep=rep;r->fixture=fixture;r->mode=mode;r->differences=differences;r->total=end-begin;r->profile=profile;for(i=0;i<3;i++)r->calls[i]=m16_feature_fast_calls[i];
  printf("FFT_COUNTS front=%u mono=%u stats=%u\r\n",m16_fft_fast_calls[0],m16_fft_fast_calls[1],m16_fft_fast_calls[2]);
  printf("MAGNITUDE_COUNTS fast=%u fallback=%u\r\n",m16_magnitude_calls[0],m16_magnitude_calls[1]);
  printf("DATAFLOW_COUNTS cold=%u hit=%u batches=%u reused=%u fallback=%u\r\n",m16_dataflow_calls[0],m16_dataflow_calls[1],m16_dataflow_calls[2],m16_dataflow_calls[3],m16_dataflow_calls[4]);
  if(mode==3){
   double mf=0.,md=0.,ms=0.;float ml=0.;unsigned gate=0,j;
   for(j=0;j<16*20*130;j++){double a=(double)last->features[j]-saved[fixture].features[j];if(a<0.)a=-a;if(a>mf)mf=a;}
   for(j=0;j<32;j++){double a=last->directions[j]-saved[fixture].directions[j];if(a<0.)a=-a;if(a>md)md=a;}
   for(j=0;j<16;j++){double a=last->scores[j]-saved[fixture].scores[j];float b=last->logits[j]-saved[fixture].logits[j];if(a<0.)a=-a;if(b<0.f)b=-b;if(a>ms)ms=a;if(b>ml)ml=b;
    if(last->valid[j]!=saved[fixture].valid[j] || (last->scores[j]>=.65)!=(saved[fixture].scores[j]>=.65))gate++;}
   if(last->candidate_count!=saved[fixture].candidate_count || md>1e-4 || ms>3e-5 || ml>1e-4)gate++;
   printf("JOINT_MIGRATION rep=%u case=%u feature_abs=%g direction_abs=%g logit_abs=%g score_abs=%g gate_errors=%u STRICT_GOLDEN_CHECK_FOLLOWS\r\n",rep,fixture,mf,md,(double)ml,ms,gate);
   if(gate)g_mic16_feature_fast_benchmark.errors++;
  }
  mic16_pipeline_test_check(fixture);g_mic16_feature_fast_benchmark.completed++;
  printf("FAST_PHASE rep=%u case=%u mode=%u total_ticks=%llu feature_ticks=%llu differences=%u beam_fast=%u beam_fallback=%u mono_fast=%u\r\n",rep,fixture,mode,(unsigned long long)(end-begin),profile.ticks[4],differences,m16_feature_fast_calls[0],m16_feature_fast_calls[1],m16_feature_fast_calls[2]);
 }
done:
 g_mic16_feature_fast_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_feature_fast_benchmark.completed!=16 || g_mic16_pipeline_test.checks!=61192)g_mic16_feature_fast_benchmark.errors++;
 g_mic16_feature_fast_benchmark.stage=g_mic16_feature_fast_benchmark.errors?4:3;
 Xil_DCacheFlush();
 Xil_DCacheFlushRange((INTPTR)&g_mic16_feature_fast_benchmark,sizeof(g_mic16_feature_fast_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("FAST_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_FUNCTION_TEST_OR_REALTIME_ACCEPTANCE\r\n",g_mic16_feature_fast_benchmark.errors?"FAIL":"PASS",g_mic16_feature_fast_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_feature_fast_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
