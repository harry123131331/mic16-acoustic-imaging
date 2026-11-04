/* 宿主机仅核验数值与计时嵌套，时钟为合成计数器，不输出ARM性能结论。 */
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_scanner_accumulators.h"
#include "mic16_feature_hotspots.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
int mic16_profile_observe_batched_timed(const float *,const float *,mic16_pipeline_observation *,
 mic16_pipeline_workspace *,const mic16_profile_clock *,mic16_pipeline_profile *);
/* 主机只用受控CPU粗图核验插桩等价，不称真实HLS结果。 */
extern int mic16_cpu_spatial_project(const float *,float *,mic16_spatial_workspace *);
void m16_cooperate(void){}
int mic16_spatial_project(const float *x,float *maps,mic16_spatial_workspace *w){
 unsigned i;m16_scanner_refinement r;int rc=mic16_cpu_spatial_project(x,maps,w);if(rc)return rc;
 for(i=0;i<M16_SP_MAP_FLOATS;i++)maps[i]=(float)(int)(maps[i]*0x1p24f)*0x1p-24f;
 return m16_scanner_refine_accumulators(x,maps,w,&r);
}
static mic16_pipeline_observation saved;
static unsigned mode,differences,errors,fixture,checks;
static uint64_t now;
static uint64_t tick(void *unused){(void)unused;return ++now;}
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("CURRENT_HOTSPOT_PARITY_ERROR line=%u case=%u mode=%u\n",(unsigned)__LINE__,fixture,mode);}}while(0)
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 const mic16_profile_clock clock={0,tick};mic16_pipeline_profile p;mic16_hotspot_report h;unsigned i;uint64_t sum=0;int rc;
 if(mode){
  mic16_hotspot_reset(tick,0);rc=mic16_profile_observe_batched_timed(x,q,o,w,&clock,&p);
  CHECK(!mic16_hotspot_finish(&h));CHECK(!h.errors && !h.depth && h.items[0].calls==1);
  for(i=0;i<M16_HOT_COUNT;i++)sum+=h.items[i].exclusive_ticks;
  CHECK(sum==h.items[0].inclusive_ticks && h.items[0].inclusive_ticks<=p.ticks[4]);
  for(i=0;i<sizeof(saved);i++)differences+=((unsigned char *)&saved)[i]!=((unsigned char *)o)[i];
  CHECK(!differences);
  CHECK(h.items[M16_HOT_BEAMS].calls==o->candidate_count && h.items[M16_HOT_MONO].calls==o->candidate_count);
  CHECK(h.items[M16_HOT_RESAMPLE].calls==32+o->candidate_count);
  CHECK(h.items[M16_HOT_STFT].calls==16*67+16*15+o->candidate_count*67);
  CHECK(h.items[M16_HOT_STFT_VALIDATE].calls==32+o->candidate_count);
  CHECK(h.items[M16_HOT_STFT_FFT].calls==h.items[M16_HOT_STFT].calls);
  CHECK(h.items[M16_HOT_PAIRS].calls==(o->candidate_count>1));
  if(o->candidate_count>1){CHECK(h.items[M16_HOT_PROJECT].calls==o->candidate_count*67);CHECK(h.items[M16_HOT_NORMALIZE].calls==17);}
  printf("CURRENT_HOTSPOT_PARITY_DETAIL case=%u candidates=%u reads=%llu max_depth=%u root_ticks=%llu\n",fixture,o->candidate_count,(unsigned long long)h.reads,h.max_depth,(unsigned long long)h.items[0].inclusive_ticks);
 }else{rc=mic16_pipeline_observe_batched_timed(x,q,o,w,&clock,&p);memcpy(&saved,o,sizeof(saved));}
 CHECK(!rc && p.reads==12 && p.clock_ok && p.attempted_mask==63 && p.completed_mask==63);
 return rc;
}
int main(void){
 for(fixture=0;fixture<2;fixture++)for(mode=0;mode<2;mode++){
  mic16_pipeline_test_prepare(fixture);CHECK(!mic16_pipeline_test_compute());mic16_pipeline_test_check(fixture);
 }
 CHECK(g_mic16_pipeline_test.checks==15298 && !g_mic16_pipeline_test.errors);
 printf("CURRENT_HOTSPOT_PARITY_%s checks=%u golden_checks=%u differences=%u errors=%u SYNTHETIC_CLOCK_NOT_ARM\n",
  errors?"FAIL":"PASS",checks,g_mic16_pipeline_test.checks,differences,errors);
 return errors?1:0;
}
