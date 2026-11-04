#include "mic16_scanner_accumulators.h"
#include "mic16_stft_batch.h"
#include "mic16_pipeline_unit.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern int memcmp(const void *,const void *,size_t);
extern int mic16_cpu_spatial_project(const float *,float *,mic16_spatial_workspace *);
static mic16_pipeline_observation baseline[2];
static float old_maps[M16_SP_MAP_FLOATS];
static double old_votes[480*9];
static unsigned short old_indices[480];
static unsigned variant,fixture,differences,refinements;
void m16_cooperate(void){}
int mic16_spatial_project(const float *x,float *maps,mic16_spatial_workspace *w){
 unsigned i;m16_scanner_refinement r;int rc=mic16_cpu_spatial_project(x,maps,w);if(rc)return rc;
 /* 用同一CPU图的Q24截断作为受控粗图，只测试精算替换，不冒充HLS或RTL。 */
 for(i=0;i<M16_SP_MAP_FLOATS;i++)maps[i]=(float)(int)(maps[i]*0x1p24f)*0x1p-24f;
 rc=variant?m16_scanner_refine_accumulators(x,maps,w,&r):m16_scanner_refine(x,maps,w,&r);
 if(!variant)memcpy(old_maps,maps,sizeof(old_maps));else if(memcmp(old_maps,maps,sizeof(old_maps)))differences++;
 refinements++;return rc;
}
int measured_pipeline_observe(const float *x,const float *q,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 int rc=mic16_pipeline_observe_batched(x,q,o,w);if(rc)return rc;
 if(!variant){memcpy(&baseline[fixture],o,sizeof(*o));memcpy(old_votes,w->votes,sizeof(old_votes));memcpy(old_indices,w->best_indices,sizeof(old_indices));}
 else {if(memcmp(&baseline[fixture],o,sizeof(*o)))differences++;if(memcmp(old_votes,w->votes,sizeof(old_votes)))differences++;if(memcmp(old_indices,w->best_indices,sizeof(old_indices)))differences++;}
 return 0;
}
int main(void){
 for(fixture=0;fixture<2;fixture++)for(variant=0;variant<2;variant++){
  mic16_pipeline_test_prepare(fixture);if(mic16_pipeline_test_compute())return 2;mic16_pipeline_test_check(fixture);
 }
 printf("SCANNER_ACCUMULATORS_GOLDEN_PASS checks=%u refinements=%u differences=%u errors=%u CONTROLLED_CPU_COARSE_MAP_NOT_HARDWARE\n",
 g_mic16_pipeline_test.checks,refinements,differences,g_mic16_pipeline_test.errors);
 return differences || g_mic16_pipeline_test.errors || g_mic16_pipeline_test.checks!=15298 || refinements!=4;
}
