/* 固定输入四路对照：保留基线、FP64数据流/FIR、混合精度NEON、NEON再加波束分块。不采音、不启网。 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "xil_cache.h"
#include "xtime_l.h"
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_cooperate.h"
#include "mic16_simd_fft.h"
#include "mic16_resample_pipeline.h"
#include "mic16_beam_tiles.h"
extern int m16_magnitude_select(unsigned);extern unsigned m16_magnitude_calls[2];
#include "mic16_dataflow_fast.h"
typedef struct {unsigned rep,fixture,mode,differences;uint64_t total;mic16_pipeline_profile profile;unsigned calls[3],reserved;} fast_run;
typedef struct {unsigned signature,version,stage,completed,errors,reserved;uint64_t timer_hz;fast_run runs[16];} fast_benchmark;
volatile fast_benchmark g_mic16_feature_fast_benchmark;
const unsigned g_mic16_feature_fast_contract[10]={sizeof(fast_benchmark),sizeof(fast_run),offsetof(fast_run,total),offsetof(fast_run,profile),offsetof(fast_run,calls),offsetof(fast_benchmark,runs),16,61192,sizeof(mic16_pipeline_profile),4};

typedef struct {unsigned rep,fixture,mode,fft4096,fft512,table_builds,fallbacks,vector_groups,fir[4];} simd_run;
typedef struct {unsigned signature,version,completed,fpscr,cpacr,errors;simd_run runs[16];} simd_detail;
volatile simd_detail g_mic16_simd_detail __attribute__((aligned(64)));
const unsigned g_mic16_simd_contract[4]={sizeof(simd_detail),sizeof(simd_run),offsetof(simd_detail,runs),16};
static const unsigned simd_probes[10]={0,23,24,55,63,87,88,105,106,129};
static mic16_pipeline_observation saved[2];
static mic16_pipeline_observation *last;
static mic16_pipeline_profile profile;
static uint64_t tick(void *unused){XTime t;(void)unused;XTime_GetTime(&t);return t;}
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){const mic16_profile_clock clock={0,tick};last=o;m16_dataflow_begin();int rc=mic16_pipeline_observe_batched_timed(x,q,o,w,&clock,&profile);m16_dataflow_end();return rc;}
int main(void){unsigned rep,fixture,order,i,mode;uint64_t begin,end;int rc;Xil_ICacheEnable();Xil_DCacheEnable();setvbuf(stdout,0,_IONBF,0);
 g_mic16_feature_fast_benchmark.signature=0x4d464153u;g_mic16_feature_fast_benchmark.version=0x20261022u;g_mic16_feature_fast_benchmark.timer_hz=COUNTS_PER_SECOND;g_mic16_feature_fast_benchmark.stage=1;
 printf("\r\nMIC16 FEATURE FAST EXACT_COMPUTE_SAME_INPUT\r\nFAST_CONTRACT timer_hz=%llu expected_checks=61192 checkpoint=%s\r\n",(unsigned long long)COUNTS_PER_SECOND,mic16_frozen_checkpoint_sha256());
 m16_fft_fast_select(0);m16_feature_fast_select(3);m16_dataflow_select(0);m16_magnitude_select(0);m16_real_fft_select(0);m16_simd_fft_select(0);m16_resample_pipeline_select(0);m16_beam_tiles_select(0);mic16_pipeline_test_prepare(0);rc=mic16_pipeline_test_compute();if(rc){g_mic16_feature_fast_benchmark.errors++;goto done;}
 g_mic16_simd_detail.signature=0x4d53494du;g_mic16_simd_detail.version=0x20261022u;
 {unsigned fpscr,cpacr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr));__asm__ volatile("mrc p15, 0, %0, c1, c0, 2":"=r"(cpacr));
  g_mic16_simd_detail.fpscr=fpscr;g_mic16_simd_detail.cpacr=cpacr;
  printf("SIMD_ENV fpscr=%08x cpacr=%08x preserve_public=%u\r\n",fpscr,cpacr,m16_simd_fft_preserve_public);
  /* 不偷偷修改舍入模式、协处理器权限或时钟；不符则保留诊断并停止。 */
  if((fpscr&(3u<<22)) || (cpacr&(15u<<20))!=(15u<<20) || (cpacr&0xc0000000u) || !m16_simd_fft_preserve_public){g_mic16_feature_fast_benchmark.errors++;g_mic16_simd_detail.errors++;goto done;}
 }
 g_mic16_feature_fast_benchmark.stage=2;
 for(rep=0;rep<2;rep++)for(fixture=0;fixture<2;fixture++)for(order=0;order<4;order++){
  unsigned differences=0;volatile fast_run *r=g_mic16_feature_fast_benchmark.runs+g_mic16_feature_fast_benchmark.completed;
  mode=rep?3-order:order;m16_fft_fast_select(mode?3:0);m16_feature_fast_select(3);m16_dataflow_select(mode?2:0);m16_magnitude_select(mode?1:0);m16_real_fft_select(mode?3:0);m16_simd_fft_select(mode>=2?2:0);m16_resample_pipeline_select(mode?1:0);m16_beam_tiles_select(mode==3);mic16_pipeline_test_prepare(fixture);
  if(!rep && mode){m16_dataflow_clear_signatures();m16_simd_fft_clear_tables();m16_resample_pipeline_clear_tables();}
  printf("FAST_START rep=%u case=%u mode=%u\r\n",rep,fixture,mode);begin=tick(0);rc=mic16_pipeline_test_compute();end=tick(0);
  if(rc){printf("FAST_COMPUTE_FAIL rc=%d\r\n",rc);g_mic16_feature_fast_benchmark.errors++;goto done;}
  if(!rep && !mode)memcpy(saved+fixture,last,sizeof(*last));else for(i=0;i<sizeof(*last);i++)differences+=((unsigned char *)(saved+fixture))[i]!=((unsigned char *)last)[i];
  if((differences && mode<2) || end<begin || !profile.clock_ok || profile.reads!=12 || profile.completed_mask!=63 || profile.attempted_mask!=63 || profile.pipeline_rc)g_mic16_feature_fast_benchmark.errors++;
  r->rep=rep;r->fixture=fixture;r->mode=mode;r->differences=differences;r->total=end-begin;r->profile=profile;for(i=0;i<3;i++)r->calls[i]=m16_feature_fast_calls[i];
  printf("FFT_COUNTS front=%u mono=%u stats=%u\r\n",m16_fft_fast_calls[0],m16_fft_fast_calls[1],m16_fft_fast_calls[2]);
  printf("BEAM_TILES calls=%u blocks4=%u original=%u\r\n",m16_beam_tiles_calls[0],m16_beam_tiles_calls[1],m16_beam_tiles_calls[2]);
  printf("REAL_COUNTS fft4096=%u fft512=%u quiet_skipped=%u MAGNITUDE fast=%u fallback=%u\r\n",m16_real_fft_calls[0],m16_real_fft_calls[1],m16_real_fft_calls[2],m16_magnitude_calls[0],m16_magnitude_calls[1]);
  printf("DATAFLOW_COUNTS cold=%u hit=%u batches=%u reused=%u fallback=%u\r\n",m16_dataflow_calls[0],m16_dataflow_calls[1],m16_dataflow_calls[2],m16_dataflow_calls[3],m16_dataflow_calls[4]);
  {volatile simd_run *d=g_mic16_simd_detail.runs+g_mic16_feature_fast_benchmark.completed;unsigned j;
   d->rep=rep;d->fixture=fixture;d->mode=mode;d->fft4096=m16_simd_fft_calls[0];d->fft512=m16_simd_fft_calls[1];d->table_builds=m16_simd_fft_calls[2];d->fallbacks=m16_simd_fft_calls[3];d->vector_groups=m16_simd_fft_vector_groups;
   for(j=0;j<4;j++)d->fir[j]=m16_resample_pipeline_calls[j];
   printf("SIMD_COUNTS rep=%u case=%u mode=%u fft4096=%u fft512=%u table=%u fallback=%u vector_groups=%u fir=%u blocks4=%u boundary=%u original=%u\r\n",rep,fixture,mode,d->fft4096,d->fft512,d->table_builds,d->fallbacks,d->vector_groups,d->fir[0],d->fir[1],d->fir[2],d->fir[3]);
   if(mode>=2 && !d->vector_groups){g_mic16_feature_fast_benchmark.errors++;g_mic16_simd_detail.errors++;}
  }
  if(mode>=2){
   unsigned j,t,k,gate=0;double md=0.,ml=0.,ms=0.,mp=0.,mf=0.;
   for(j=0;j<32;j++){double a=last->directions[j]-saved[fixture].directions[j];if(a<0.)a=-a;if(a>md)md=a;}
   for(j=0;j<16;j++){double a=last->logits[j]-saved[fixture].logits[j],b=last->scores[j]-saved[fixture].scores[j];if(a<0.)a=-a;if(b<0.)b=-b;if(a>ml)ml=a;if(b>ms)ms=b;
    gate+=last->valid[j]!=saved[fixture].valid[j] || ((last->scores[j]>=.65)!=(saved[fixture].scores[j]>=.65));
    for(t=0;t<20;t++)for(k=0;k<10;k++){unsigned p=(j*20+t)*130+simd_probes[k];a=(double)last->features[p]-saved[fixture].features[p];if(a<0.)a=-a;if(a>mp)mp=a;}
   }
   for(j=0;j<16*20*130;j++){double a=(double)last->features[j]-saved[fixture].features[j];if(a<0.)a=-a;if(a>mf)mf=a;}
   gate+=last->candidate_count!=saved[fixture].candidate_count || md>1e-4 || ml>1e-4 || ms>3e-5 || mp>5e-5;
   printf("SIMD_MIGRATION rep=%u case=%u mode=%u direction=%g logit=%g score=%g probes=%g features=%g gates=%u STRICT_ORIGINAL_GOLDEN_FOLLOWS\r\n",rep,fixture,mode,md,ml,ms,mp,mf,gate);
   if(gate){g_mic16_feature_fast_benchmark.errors++;g_mic16_simd_detail.errors++;}
  }
  mic16_pipeline_test_check(fixture);g_mic16_feature_fast_benchmark.completed++;g_mic16_simd_detail.completed=g_mic16_feature_fast_benchmark.completed;
  printf("FAST_PHASE rep=%u case=%u mode=%u total_ticks=%llu feature_ticks=%llu differences=%u beam_fast=%u beam_fallback=%u mono_fast=%u\r\n",rep,fixture,mode,(unsigned long long)(end-begin),profile.ticks[4],differences,m16_feature_fast_calls[0],m16_feature_fast_calls[1],m16_feature_fast_calls[2]);
 }
done:
 g_mic16_feature_fast_benchmark.errors+=g_mic16_pipeline_test.errors;
 if(g_mic16_feature_fast_benchmark.completed!=16 || g_mic16_pipeline_test.checks!=61192)g_mic16_feature_fast_benchmark.errors++;
 g_mic16_feature_fast_benchmark.stage=g_mic16_feature_fast_benchmark.errors?4:3;
 Xil_DCacheFlush();
 Xil_DCacheFlushRange((INTPTR)&g_mic16_feature_fast_benchmark,sizeof(g_mic16_feature_fast_benchmark));Xil_DCacheFlushRange((INTPTR)&g_mic16_pipeline_test,sizeof(g_mic16_pipeline_test));
 printf("FAST_BENCHMARK_%s cases=%u checks=%u errors=%u NOT_LIVE_OR_REALTIME\r\n",g_mic16_feature_fast_benchmark.errors?"FAIL":"PASS",g_mic16_feature_fast_benchmark.completed,g_mic16_pipeline_test.checks,g_mic16_feature_fast_benchmark.errors);
 for(;;){__asm__ volatile("nop");}
}
