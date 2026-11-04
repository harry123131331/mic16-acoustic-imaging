#include "mic16_enh_prediction_task.h"
#include "mic16_enh_prediction.h"
#include <string.h>
static m16_enh_prediction_task task;
static mic16_extended_workspace shared,original_shared;
static m16_enh_workspace work,original_work;
static m16_cont_prediction prediction,original_prediction;
static mic16_cycle_output tracks;
static double directions[32];
static unsigned char valid[16];
static const float *pcm,*reference;
int ep_setup(const float *x,const float *quiet,unsigned sources,unsigned candidates,unsigned held){
 unsigned i;int rc;
 memset(&task,0,sizeof(task));memset(&shared,0,sizeof(shared));memset(&original_shared,0,sizeof(original_shared));
 memset(&work,0,sizeof(work));memset(&original_work,0,sizeof(original_work));memset(&prediction,0,sizeof(prediction));memset(&original_prediction,0,sizeof(original_prediction));memset(&tracks,0,sizeof(tracks));
 if(sources+held>8u || candidates>16u || sources>candidates)return -999;
 pcm=x;reference=quiet;
 for(i=0;i<16u;i++){directions[i*2u]=-60.+(double)i*8.;directions[i*2u+1u]=-30.+(double)i*4.;valid[i]=(unsigned char)(i<candidates);}
 tracks.count=(int)(sources+held);tracks.confirmed_count=(int)sources;
 for(i=0;i<sources+held;i++){
  unsigned c=sources>1u?i*(candidates-1u)/(sources-1u):0u;if(c>=16u)c=0;
  tracks.tracks[i].source_id=80u-i;tracks.tracks[i].score=.9;tracks.tracks[i].confirmed_this_cycle=(int)(i<sources);
  tracks.tracks[i].azimuth=directions[c*2u];tracks.tracks[i].elevation=directions[c*2u+1u];
 }
 rc=m16_enh_prediction_init(&task,&shared,&work);return rc?rc:m16_enh_prediction_begin(&task,pcm,reference,directions,valid,&tracks,&prediction);
}
static int sink(void *context,unsigned id,unsigned candidate,const float *mask){
 unsigned i=original_prediction.count++;(void)context;if(i>=8u)return -1;
 original_prediction.ids[i]=id;original_prediction.directions[i][0]=directions[candidate*2u];original_prediction.directions[i][1]=directions[candidate*2u+1u];
 memcpy(original_prediction.masks[i],mask,67u*64u*4u);return 0;
}
int ep_reference(void){
 int rc=m16_enh_predict_window(pcm,reference,directions,valid,&tracks,&original_shared,&original_work,sink,0);
 if(rc>0)memcpy(original_prediction.weights,original_work.weights,320u*4u);
 return rc;
}
int ep_run(unsigned target){int rc;unsigned n=0;for(;;){if(target && task.phase==target)return 1;rc=m16_enh_prediction_step(&task);if(rc!=1)return rc;if(++n>7000000u)return -999;}}
int ep_step(void){return m16_enh_prediction_step(&task);}
int ep_stop(void){return m16_enh_prediction_stop(&task);}
int ep_begin(void){return m16_enh_prediction_begin(&task,pcm,reference,directions,valid,&tracks,&prediction);}
int ep_releasable(void){return m16_enh_prediction_releasable(&task);}
uint64_t ep_metric(unsigned k){return k==0?task.phase:k==1?task.max_items:k==2?task.steps:k==3?(unsigned)task.prediction_valid:k==4?prediction.count:k==5?task.candidate:task.selected_count;}
int ep_equal(unsigned kind){
 if(kind==0){
  unsigned n=prediction.count;if(n!=original_prediction.count || memcmp(prediction.weights,original_prediction.weights,sizeof(prediction.weights)))return 0;
  return !memcmp(prediction.ids,original_prediction.ids,n*4u) && !memcmp(prediction.directions,original_prediction.directions,n*16u) && !memcmp(prediction.masks,original_prediction.masks,n*67u*64u*4u);
 }
 if(kind==1)return !memcmp(&shared,&original_shared,sizeof(shared));
 if(kind==2)return !memcmp(shared.signature,original_shared.signature,sizeof(shared.signature)) && !memcmp(shared.spectral10,original_shared.spectral10,sizeof(shared.spectral10));
 return !memcmp(&work,&original_work,sizeof(work));
}
int ep_bad_begin(unsigned k){
 m16_enh_prediction_stop(&task);
 if(k==0)tracks.tracks[1].source_id=tracks.tracks[0].source_id;
 if(k==1)tracks.tracks[0].score=.649999999;
 if(k==2){tracks.tracks[1].azimuth=tracks.tracks[0].azimuth;tracks.tracks[1].elevation=tracks.tracks[0].elevation;}
 if(k==3)tracks.confirmed_count=1;
 if(k==4)valid[0]=2;
 if(k==5)return m16_enh_prediction_begin(&task,pcm,reference,directions,valid,&tracks,(m16_cont_prediction *)pcm);
 return ep_begin();
}
