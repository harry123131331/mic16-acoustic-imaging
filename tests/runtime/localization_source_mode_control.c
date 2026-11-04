/* 只验证仅定位控制语义；原记忆/多源判决与原初始化均使用真实实现。
 * 输入为显式评分交接夹具，不宣称真实检测准确率或实板性能。 */
#include "mic16_source_audio_task.h"
#include <string.h>
#include <stdint.h>
#define REQUIRE(x) do{cases++;if(!(x))return -(int)__LINE__;}while(0)
static m16_source_audio_task source;
static m16_enh_prediction_task enhancement;
static m16_continuous_task continuous;
static mic16_extended_workspace shared;
static m16_enh_workspace enhancement_work;
static m16_cont_workspace continuous_work;
static mic16_pipeline_observation observation;
static mic16_search_memory original_memory;
static mic16_cycle_output original_tracks;
static float pcm[40960u*16u],reference[8192u*16u];
static unsigned cases;
int localization_mode_test(void){
 const char key[]="localization_control_Golden";
 const mic16_cycle_output *tracks;const mic16_pipeline_observation *o;
 const m16_cont_info *info;const double *a,*b;unsigned n;
 REQUIRE(m16_enh_prediction_init(&enhancement,&shared,&enhancement_work)==0);
 REQUIRE(m16_continuous_task_init(&continuous,&continuous_work,1u)==0);
 REQUIRE(m16_source_audio_init(&source,&enhancement,&continuous,key)==0);
 REQUIRE(source.localization_only==0u);
 REQUIRE(m16_source_audio_set_localization_only(&source,2u)==M16_SA_INVALID);
 REQUIRE(m16_source_audio_set_localization_only(&source,1u)==0);
 mic16_memory_init(&original_memory);
 observation.candidate_count=16u;
 for(unsigned frame=0;frame<4u;frame++){
  memset(observation.valid,0,sizeof(observation.valid));memset(observation.scores,0,sizeof(observation.scores));
  for(unsigned i=0;i<16u;i++){
   observation.directions[i*2u]=-60.+i*8.;observation.directions[i*2u+1u]=0.;
   if(i<frame+1u){observation.valid[i]=1;observation.scores[i]=.9f;}
  }
  uint64_t first=(uint64_t)frame*40960u;
  REQUIRE(m16_source_audio_begin(&source,pcm,reference,&observation,first)==0);
  REQUIRE(m16_source_audio_set_localization_only(&source,0u)==M16_SA_BUSY);
  REQUIRE(m16_source_audio_step(&source)==1);
  REQUIRE(mic16_consume_scored_cycle(&original_memory,observation.directions,observation.valid,observation.scores,
   (double)(first+40960u)/39062.5,key,1,&original_tracks)==0);
  REQUIRE(m16_source_audio_location(&source,&tracks,&o)==1 && o==&observation);
  REQUIRE(memcmp(tracks,&original_tracks,sizeof(original_tracks))==0);
  REQUIRE(memcmp(&source.memory,&original_memory,sizeof(original_memory))==0);
  REQUIRE(enhancement.steps==0 && continuous.steps==0 && continuous_work.windows==0);
  REQUIRE(m16_source_audio_output(&source,&info,&a,&b,&n)==0);
  REQUIRE(m16_source_audio_location_release(&source,1u,frame+1u)==M16_SA_INVALID);
  REQUIRE(m16_source_audio_location_release(&source,1u,frame)==0);
  REQUIRE(m16_source_audio_step(&source)==0 && m16_source_audio_releasable(&source));
  REQUIRE(source.next_sample==first+40960u);
 }
 REQUIRE(m16_source_audio_finish(&source)==0);
 REQUIRE(source.closed==1u && continuous_work.closed==0u && continuous.steps==0u);
 REQUIRE(m16_source_audio_begin(&source,pcm,reference,&observation,4u*40960u)==M16_SA_SEQUENCE);
 REQUIRE(m16_source_audio_reset(&source,2u,key)==0 && source.localization_only==1u);
 REQUIRE(source.next_sample==0u && source.closed==0u);
 /* 新epoch可以显式返回保留的完整模式，原增强入口仍在。 */
 REQUIRE(m16_source_audio_set_localization_only(&source,0u)==0);
 REQUIRE(m16_source_audio_begin(&source,pcm,reference,&observation,0u)==0);
 REQUIRE(m16_source_audio_step(&source)==1 && source.phase==M16_SA_PREDICT);
 REQUIRE(m16_source_audio_stop(&source)==1);
 REQUIRE(m16_source_audio_releasable(&source));
 return (int)cases;
}
