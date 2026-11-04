/* 同一原黄金夹具的完整检测作用域检查；不是房间实验或ARM计时。 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mic16_pipeline_unit.h"
#include "mic16_application_acceleration.h"
#include "mic16_fft_fast.h"
#include "mic16_dataflow_fast.h"
#include "mic16_real_fft.h"
#include "mic16_simd_fft.h"
#include "mic16_resample_pipeline.h"
#include "mic16_beam_tiles.h"
#if M16_APP_STATISTICS_NEON
#include "mic16_statistics_neon.h"
#endif
extern unsigned m16_magnitude_enabled;
static int accelerated;
static mic16_pipeline_observation saved[2];
static mic16_pipeline_observation *last;
int m16_application_test_environment(unsigned *f,unsigned *c){*f=0;*c=15u<<20;return 1;}
int application_probe_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 last=o;
 if(accelerated)return __wrap_mic16_pipeline_observe_batched_timed(x,q,o,w,0,0);
 return mic16_pipeline_observe_batched_timed(x,q,o,w,0,0);
}
static int clear(void){return !m16_fft_fast_mode && !m16_feature_fast_mode && !m16_dataflow_mode && !m16_magnitude_enabled && !m16_real_fft_mode && !m16_simd_fft_enabled && !m16_resample_pipeline_enabled && !m16_beam_tiles_enabled
#if M16_APP_STATISTICS_NEON
 && !m16_statistics_neon_mode
#endif
 ;}
static double absmax(const float *a,const float *b,unsigned n){unsigned i;double m=0.;for(i=0;i<n;i++){double d=fabs((double)a[i]-b[i]);if(d>m)m=d;}return m;}
int main(void){
 unsigned pass,which,i,statistics_calls=0;double md=0.,ml=0.,ms=0.,mf=0.;int rc;
 for(pass=0;pass<3;pass++)for(which=0;which<2;which++){
  accelerated=pass==1;mic16_pipeline_test_prepare(which);rc=mic16_pipeline_test_compute();
  if(rc || !clear()){printf("APPLICATION_NUMERIC_FAILURE pass=%u case=%u rc=%d modes_clear=%d\n",pass,which,rc,clear());return 2;}
  if(mic16_pipeline_test_check(which))return 3;
#if M16_APP_STATISTICS_NEON
  if(accelerated)statistics_calls+=g_mic16_application_acceleration.statistics[2];
#endif
  if(!pass)saved[which]=*last;
  else if(pass==2 && memcmp(saved+which,last,sizeof(*last))){puts("BASELINE_CHANGED_AFTER_ACCELERATION");return 4;}
  else if(pass==1){
   for(i=0;i<32;i++){double d=fabs(saved[which].directions[i]-last->directions[i]);if(d>md)md=d;}
   for(i=0;i<16;i++){
    double d=fabs(saved[which].scores[i]-last->scores[i]);if(d>ms)ms=d;
    if(saved[which].valid[i]!=last->valid[i] || ((saved[which].scores[i]>=.65)!=(last->scores[i]>=.65)))return 5;
   }
   {double d=absmax(saved[which].logits,last->logits,16);if(d>ml)ml=d;d=absmax(saved[which].features,last->features,41600);if(d>mf)mf=d;}
  }
 }
 if(g_mic16_pipeline_test.errors || g_mic16_pipeline_test.checks!=22947)return 6;
#if M16_APP_STATISTICS_NEON
 if(!statistics_calls)return 7;
#endif
 printf("STATISTICS_SCOPE_ENABLED=%u rotated_groups=%u\n",(unsigned)M16_APP_STATISTICS_NEON,statistics_calls);
 printf("APPLICATION_NUMERIC_PASS checks=%u errors=%u direction=%g logits=%g scores=%g all_features=%g baseline_after_identical=1 HOST_NOT_ARM_TIMING\n",
  g_mic16_pipeline_test.checks,g_mic16_pipeline_test.errors,md,ml,ms,mf);
 return 0;
}
