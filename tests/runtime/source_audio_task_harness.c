#include "continuous_task_harness.c"
#include "mic16_source_audio_task.h"
#include "mic16_enh_prediction.h"
static m16_source_audio_task source_task;
static m16_enh_prediction_task enhancement;
static m16_enh_workspace enh_work,original_enh;
static mic16_extended_workspace shared_enh,original_shared_enh;
static mic16_pipeline_observation observation_input;
static mic16_search_memory original_memory;
static mic16_cycle_output original_tracks;
static m16_cont_prediction original_parameters;
static const float *reference_input;
static unsigned location_checks;
int sa_setup(const float *reference){
 int rc=ct_setup(20261026);if(rc)return rc;
 memset(&source_task,0,sizeof(source_task));memset(&enhancement,0,sizeof(enhancement));memset(&enh_work,0,sizeof(enh_work));memset(&original_enh,0,sizeof(original_enh));
 memset(&shared_enh,0,sizeof(shared_enh));memset(&original_shared_enh,0,sizeof(original_shared_enh));reference_input=reference;mic16_memory_init(&original_memory);location_checks=0;
 memset(&original_parameters,0,sizeof(original_parameters));
 for(unsigned i=0;i<2048u;i++){original_enh.twiddle_re[i]=cos(2.*3.14159265358979323846264338327950288*i/4096.);original_enh.twiddle_im[i]=-sin(2.*3.14159265358979323846264338327950288*i/4096.);}
 rc=m16_enh_prediction_init(&enhancement,&shared_enh,&enh_work);return rc?rc:m16_source_audio_init(&source_task,&enhancement,&task,"pose:1/ref:1/source:1");
}
int sa_prepare(const float *x,unsigned pattern,unsigned index){
 unsigned i;input=x;memset(&observation_input,0,sizeof(observation_input));observation_input.candidate_count=16;
 for(i=0;i<16u;i++){
  observation_input.directions[i*2u]=-60.+i*8.;observation_input.directions[i*2u+1u]=-30.+i*4.;observation_input.valid[i]=1;
  observation_input.scores[i]=pattern==0u?.1:.9;
  if(pattern==2u && index>=3u && index<7u)observation_input.scores[i]=.1;
  if(pattern==3u)observation_input.scores[i]=i==0u?.65:.649999999;
 }
 expected_count=consumed=first_difference=0;comparison=0;return 0;
}
static int prediction_sink(void *context,unsigned id,unsigned candidate,const float *mask){
 unsigned i=original_parameters.count++;(void)context;if(i>=8u)return -1;
 original_parameters.ids[i]=id;original_parameters.directions[i][0]=observation_input.directions[candidate*2u];original_parameters.directions[i][1]=observation_input.directions[candidate*2u+1u];
 memcpy(original_parameters.masks[i],mask,67u*64u*4u);return 0;
}
int sa_reference(void){
 int rc;double end=(double)(((uint64_t)original.windows+1u)*40960u)/39062.5;comparison=1;
 rc=mic16_consume_scored_cycle(&original_memory,observation_input.directions,observation_input.valid,observation_input.scores,end,"pose:1/ref:1/source:1",1,&original_tracks);if(rc)return rc;
 original_parameters.count=0;
 rc=m16_enh_predict_window(input,reference_input,observation_input.directions,observation_input.valid,&original_tracks,&original_shared_enh,&original_enh,prediction_sink,0);if(rc<0)return rc;
 if(rc)memcpy(original_parameters.weights,original_enh.weights,sizeof(original_parameters.weights));
 else memset(original_parameters.weights,0,sizeof(original_parameters.weights));
 return m16_cont_push(&original,input,&original_parameters,(uint64_t)original.windows*40960u,sink,0);
}
int sa_begin(void){return m16_source_audio_begin(&source_task,input,reference_input,&observation_input,source_task.next_sample);}
static int location_consume(void){
 const mic16_cycle_output *tracks;const mic16_pipeline_observation *o;int rc=m16_source_audio_location(&source_task,&tracks,&o);if(rc!=1)return -1010;
 if(comparison && (memcmp(tracks,&original_tracks,sizeof(*tracks)) || memcmp(&source_task.memory,&original_memory,sizeof(original_memory))))return -1011;
 if(o!=&observation_input)return -1012;
 location_checks++;return m16_source_audio_location_release(&source_task,source_task.epoch,(uint32_t)(source_task.first_sample/40960u));
}
static int audio_consume(void){
 const m16_cont_info *info;const double *a,*b;unsigned n;int rc=m16_source_audio_output(&source_task,&info,&a,&b,&n);if(rc!=1)return -1020;
 if(comparison){
  if(consumed>=expected_count || sizes[consumed]!=n || !info_equal(info,info_expected+consumed))return -1021;
  if(memcmp(a,expected[consumed][0],n*8u) || memcmp(b,expected[consumed][1],n*8u))return -1022;
  matched_values+=n*2u;
 }
 consumed++;return m16_source_audio_output_release(&source_task,info->epoch,info->window,info->source_id);
}
int sa_run(unsigned target,unsigned hold_location){
 unsigned n=0;int rc;
 for(;;){
  if(target && source_task.phase==target)return 1;
  if(source_task.location_ready && !hold_location){rc=location_consume();if(rc)return rc;}
  if(task.phase==M16_CT_OUTPUT_READY){rc=audio_consume();if(rc)return rc;}
  rc=m16_source_audio_step(&source_task);
  if(rc!=1){if(!rc && comparison && consumed!=expected_count)return -1023;return rc;}
  if(hold_location && source_task.phase==M16_SA_WAIT_LOCATION)return 1;
  if(++n>15000000u)return -999;
 }
}
int sa_reference_finish(void){expected_count=consumed=0;comparison=1;return m16_cont_finish(&original,sink,0);}
int sa_finish(void){return m16_source_audio_finish(&source_task);}
int sa_stop(void){return m16_source_audio_stop(&source_task);}
int sa_step(void){return m16_source_audio_step(&source_task);}
int sa_reset(unsigned epoch){comparison=0;return m16_source_audio_reset(&source_task,epoch,"pose:2/ref:1/source:1");}
int sa_releasable(void){return m16_source_audio_releasable(&source_task);}
uint64_t sa_metric(unsigned k){return k==0?source_task.phase:k==1?(unsigned)source_task.memory.count:k==2?(unsigned)source_task.tracks.confirmed_count:k==3?source_task.location_ready:k==4?source_task.next_sample:k==5?source_task.memory.next_id:k==6?source_task.steps:k==7?location_checks:k==8?source_task.closed:matched_values;}
int sa_state_equal(void){return !memcmp(&work,&original,sizeof(work)) && !memcmp(&enh_work,&original_enh,sizeof(enh_work)) && !memcmp(&shared_enh,&original_shared_enh,sizeof(shared_enh));}
int sa_location_probe(void){
 const mic16_cycle_output *a,*b;const mic16_pipeline_observation *o;int rc=m16_source_audio_location(&source_task,&a,&o);if(rc!=1)return -1;
 if(m16_source_audio_location_release(&source_task,source_task.epoch+1u,(uint32_t)(source_task.first_sample/40960u))!=M16_SA_INVALID)return -2;
 if(m16_source_audio_location(&source_task,&b,&o)!=1 || a!=b)return -3;
 return location_consume();
}
int sa_bad(unsigned k){
 if(k==0)return m16_source_audio_begin(&source_task,input,reference_input,&observation_input,source_task.next_sample+40960u);
 if(k==1)observation_input.scores[0]=NAN;
 if(k==2)return m16_source_audio_begin(&source_task,input,reference_input,&observation_input,source_task.next_sample+1u);
 if(k==3)return m16_source_audio_begin(&source_task,input,reference_input,(mic16_pipeline_observation *)&shared_enh,source_task.next_sample);
 return sa_begin();
}
