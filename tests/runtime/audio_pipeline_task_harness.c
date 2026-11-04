/* 复用实际粗扫/共享核C模型及已知FFT控制注入，检查整条计算图而非同步函数包装。 */
#include "detection_task_harness.c"
#include "mic16_audio_pipeline_task.h"
#include "mic16_enh_prediction.h"
#include <math.h>
static m16_audio_pipeline_task audio_pipeline;
static m16_source_audio_task source_audio;
static m16_enh_prediction_task enhancement_task;
static m16_continuous_task continuous_task;
static m16_enh_workspace enhancement_work,original_enhancement;
static m16_cont_workspace continuous_work,original_continuous;
static mic16_extended_workspace original_enhanced_shared;
static mic16_search_memory original_memory;
static mic16_cycle_output original_tracks;
static m16_cont_prediction original_parameters;
static double expected_audio[25][2][16778];
static m16_cont_info expected_info[25];
static unsigned expected_size[25],expected_count,consumed_audio,compare_enabled,expected_built,override_scores;
static uint64_t compared_detection_values,compared_audio_values,location_updates;
int ap_setup(const float *x,const float *q){
 int rc=dt_setup(x,q,0);if(rc)return rc;
 (void)m16_detection_stop(&detection);
 for(unsigned i=0;i<100u && !m16_detection_releasable(&detection);i++)(void)m16_detection_step(&detection);
 if(!m16_detection_releasable(&detection))return -999;
 memset(&audio_pipeline,0,sizeof(audio_pipeline));memset(&source_audio,0,sizeof(source_audio));memset(&enhancement_task,0,sizeof(enhancement_task));memset(&continuous_task,0,sizeof(continuous_task));
 memset(&enhancement_work,0,sizeof(enhancement_work));memset(&original_enhancement,0,sizeof(original_enhancement));
 memset(&continuous_work,0,sizeof(continuous_work));memset(&original_continuous,0,sizeof(original_continuous));memset(&original_enhanced_shared,0,sizeof(original_enhanced_shared));
 memset(&original_parameters,0,sizeof(original_parameters));mic16_memory_init(&original_memory);
 for(unsigned i=0;i<2048u;i++){original_enhancement.twiddle_re[i]=cos(2.*3.14159265358979323846264338327950288*i/4096.);original_enhancement.twiddle_im[i]=-sin(2.*3.14159265358979323846264338327950288*i/4096.);}
 rc=m16_enh_prediction_init(&enhancement_task,&detection_work.extended,&enhancement_work);if(rc)return rc;
 rc=m16_continuous_task_init(&continuous_task,&continuous_work,13);if(rc)return rc;
 rc=m16_cont_init(&original_continuous,13);if(rc)return rc;
 rc=m16_source_audio_init(&source_audio,&enhancement_task,&continuous_task,"pose:1/ref:1/source:1");if(rc)return rc;
 compared_detection_values=compared_audio_values=location_updates=0;compare_enabled=expected_built=expected_count=consumed_audio=override_scores=0;
 return m16_audio_pipeline_init(&audio_pipeline,&detection,&source_audio,&detection_output);
}
int ap_prepare(const float *x,unsigned prepared_scores){
 pcm=x;override_scores=prepared_scores;compare_enabled=expected_built=expected_count=consumed_audio=0;calls=ready=checks=0;return 0;
}
int ap_reference_detection(void){int rc=dt_reference();if(!rc)compare_enabled=1;return rc;}
int ap_begin(void){int rc=m16_audio_pipeline_begin(&audio_pipeline,pcm,quiet,source_audio.next_sample);if(!rc)base=served;return rc;}
static int check_detection(void){
 if(memcmp(detection_output.features,expected,sizeof(expected)) || memcmp(detection_output.logits,expected_logits,sizeof(expected_logits)) ||
  memcmp(detection_output.scores,expected_scores,sizeof(expected_scores)) || memcmp(detection_output.directions,expected_model,sizeof(expected_model)) ||
  memcmp(detection_output.valid,expected_bits,sizeof(expected_bits)) || memcmp(detection_work.maps,expected_maps,sizeof(expected_maps)) ||
  memcmp(detection_work.votes,expected_votes,sizeof(expected_votes)) || memcmp(detection_work.best_indices,expected_indices,sizeof(expected_indices)) || !dt_pool_equal())return -1100;
 compared_detection_values+=202480u;return 0;
}
static int prediction_sink(void *context,unsigned id,unsigned candidate,const float *mask){
 unsigned i=original_parameters.count++;(void)context;if(i>=8u)return -1101;
 original_parameters.ids[i]=id;original_parameters.directions[i][0]=detection_output.directions[candidate*2u];original_parameters.directions[i][1]=detection_output.directions[candidate*2u+1u];
 memcpy(original_parameters.masks[i],mask,67u*64u*4u);return 0;
}
static int audio_sink(void *context,const m16_cont_info *info,const double *a,const double *b,unsigned n){
 unsigned i=expected_count++;(void)context;if(i>=25u || n>16778u)return -1102;
 expected_size[i]=n;expected_info[i]=*info;memcpy(expected_audio[i][0],a,n*8u);memcpy(expected_audio[i][1],b,n*8u);return 0;
}
static int build_reference(void){
 int rc=check_detection();if(rc)return rc;
 if(override_scores){
  /* 仅生命周期夹具：完成真实检测对照后显式替换评分交接，不作为检测成绩或生产路径。 */
  for(unsigned i=0;i<16u;i++){detection_output.directions[i*2u]=-60.+i*8.;detection_output.directions[i*2u+1u]=-30.+i*4.;detection_output.valid[i]=1;detection_output.scores[i]=.9;}
  detection_output.candidate_count=16;
 }
 rc=mic16_consume_scored_cycle(&original_memory,detection_output.directions,detection_output.valid,detection_output.scores,
  (double)(source_audio.next_sample+40960u)/39062.5,"pose:1/ref:1/source:1",1,&original_tracks);if(rc)return rc;
 memcpy(&original_enhanced_shared,&detection_work.extended,sizeof(original_enhanced_shared));original_parameters.count=0;
 rc=m16_enh_predict_window(pcm,quiet,detection_output.directions,detection_output.valid,&original_tracks,&original_enhanced_shared,&original_enhancement,prediction_sink,0);if(rc<0)return rc;
 if(rc)memcpy(original_parameters.weights,original_enhancement.weights,sizeof(original_parameters.weights));else memset(original_parameters.weights,0,sizeof(original_parameters.weights));
 rc=m16_cont_push(&original_continuous,pcm,&original_parameters,source_audio.next_sample,audio_sink,0);return rc<0?rc:0;
}
static int info_equal(const m16_cont_info *a,const m16_cont_info *b){
 return a->epoch==b->epoch && a->window==b->window && a->source_id==b->source_id && a->ordinal==b->ordinal && a->stream_count==b->stream_count &&
  a->current_confirmed==b->current_confirmed && a->final==b->final && a->output_start==b->output_start && a->output_end==b->output_end &&
  a->input_start==b->input_start && a->input_end==b->input_end && a->available_input==b->available_input;
}
static int consume_outputs(void){
 const mic16_cycle_output *tracks;const mic16_pipeline_observation *o;const m16_cont_info *info;const double *a,*b;unsigned n;int rc;
 rc=m16_audio_pipeline_location(&audio_pipeline,&tracks,&o);
 if(rc<0)return rc;
 if(rc==1){
  if(o!=&detection_output || (compare_enabled && (memcmp(tracks,&original_tracks,sizeof(*tracks)) || memcmp(&source_audio.memory,&original_memory,sizeof(original_memory)))))return -1103;
  location_updates++;rc=m16_audio_pipeline_location_release(&audio_pipeline,source_audio.epoch,(uint32_t)(source_audio.first_sample/40960u));if(rc)return rc;
 }
 rc=m16_audio_pipeline_output(&audio_pipeline,&info,&a,&b,&n);if(rc<0)return rc;
 if(rc==1){
  if(compare_enabled){
   if(consumed_audio>=expected_count || expected_size[consumed_audio]!=n || !info_equal(info,expected_info+consumed_audio))return -1104;
   if(memcmp(a,expected_audio[consumed_audio][0],n*8u) || memcmp(b,expected_audio[consumed_audio][1],n*8u))return -1105;
   compared_audio_values+=n*2u;
  }
  consumed_audio++;return m16_audio_pipeline_output_release(&audio_pipeline,info->epoch,info->window,info->source_id);
 }
 return 0;
}
int ap_run(unsigned target,unsigned until_coarse){
 unsigned n=0;int rc;
 for(;;){
  if(target && audio_pipeline.phase==target)return 1;
  if(until_coarse && cs_metric(0)==M16_CS_DEVICE)return 1;
  if(audio_pipeline.phase==M16_AP_SOURCE_AUDIO && !audio_pipeline.finishing && !expected_built){
   expected_built=1;if(compare_enabled){rc=build_reference();if(rc)return rc;}
  }
  rc=consume_outputs();if(rc)return rc;
  rc=dt_service(1);if(rc<0)return rc;
  rc=m16_audio_pipeline_step(&audio_pipeline);
  if(rc!=1){if(!rc && compare_enabled && consumed_audio!=expected_count)return -1106;return rc;}
  if(++n>20000000u)return -999;
 }
}
int ap_finish(void){
 int rc;expected_count=consumed_audio=0;
 if(compare_enabled){rc=m16_cont_finish(&original_continuous,audio_sink,0);if(rc<0)return rc;}
 return m16_audio_pipeline_finish(&audio_pipeline);
}
int ap_stop(void){return m16_audio_pipeline_stop(&audio_pipeline);}
int ap_step(void){return m16_audio_pipeline_step(&audio_pipeline);}
int ap_releasable(void){return m16_audio_pipeline_releasable(&audio_pipeline);}
int ap_reset(unsigned epoch){compare_enabled=0;return m16_audio_pipeline_reset(&audio_pipeline,epoch,"pose:2/ref:1/source:1");}
int ap_equal(void){return !memcmp(&continuous_work,&original_continuous,sizeof(continuous_work)) && !memcmp(&enhancement_work,&original_enhancement,sizeof(enhancement_work)) && !memcmp(&detection_work.extended,&original_enhanced_shared,sizeof(original_enhanced_shared));}
uint64_t ap_metric(unsigned k){
 return k==0?audio_pipeline.phase:k==1?audio_pipeline.steps:k==2?source_audio.next_sample:k==3?compared_detection_values:k==4?compared_audio_values:
  k==5?location_updates:k==6?(unsigned)source_audio.tracks.confirmed_count:k==7?continuous_work.windows:k==8?continuous_work.emitted:k==9?detection.reserved_tag_high:
  k==10?detection.reserved_job_high:k==11?source_audio.closed:k==12?source_audio.epoch:real_coarse_calls;
}
int ap_bad(unsigned k){
 if(k==0)return m16_audio_pipeline_begin(&audio_pipeline,pcm,quiet,source_audio.next_sample+40960u);
 if(k==1)return m16_audio_pipeline_begin(&audio_pipeline,(float *)&source_audio,quiet,source_audio.next_sample);
 return m16_audio_pipeline_begin(&audio_pipeline,pcm,(float *)&audio_pipeline,source_audio.next_sample);
}
