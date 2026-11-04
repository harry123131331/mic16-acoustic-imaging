#include "mic16_continuous_task.h"
#include <string.h>
#include <math.h>
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
/* 以下整窗对照存储仅在电脑测试动态库内，不进入板端任务。 */
static m16_continuous_task task;
static m16_cont_workspace work,original;
static m16_cont_prediction prediction;
static double expected[25][2][16778];
static m16_cont_info info_expected[25];
static unsigned sizes[25],expected_count,consumed,comparison,matched_values,first_difference;
static const float *input;
int ct_setup(unsigned epoch){
 memset(&task,0,sizeof(task));memset(&work,0,sizeof(work));memset(&original,0,sizeof(original));
 matched_values=first_difference=expected_count=consumed=0;comparison=0;
 int rc=m16_cont_init(&original,epoch);return rc?rc:m16_continuous_task_init(&task,&work,epoch);
}
int ct_prepare(const float *pcm,unsigned count,unsigned first_id,unsigned index){
 unsigned i,j;memset(&prediction,0,sizeof(prediction));input=pcm;
 if(count>8u)return -999;
 prediction.count=count;
 for(i=0;i<320u;i++)prediction.weights[i]=1.f/16.f;
 for(i=0;i<count;i++){
  prediction.ids[i]=first_id+count-i;prediction.directions[i][0]=-45.+i*12.+(index%3u)*.5;prediction.directions[i][1]=-20.+i*5.;
  for(j=0;j<67u*64u;j++)prediction.masks[i][j]=(float)(1.+.15*sin(.1*(double)(j+i+index)));
 }
 expected_count=consumed=first_difference=0;comparison=0;return 0;
}
static int sink(void *context,const m16_cont_info *info,const double *a,const double *b,unsigned n){
 unsigned i=expected_count++;(void)context;if(i>=25u || n>16778u)return -1;
 sizes[i]=n;info_expected[i]=*info;memcpy(expected[i][0],a,n*8u);memcpy(expected[i][1],b,n*8u);return 0;
}
int ct_reference_push(void){comparison=1;return m16_cont_push(&original,input,&prediction,(uint64_t)original.windows*40960u,sink,0);}
int ct_push(void){return m16_continuous_task_push(&task,input,&prediction,(uint64_t)work.windows*40960u);}
int ct_reference_finish(void){expected_count=consumed=0;comparison=1;return m16_cont_finish(&original,sink,0);}
int ct_finish(void){return m16_continuous_task_finish(&task);}
static int info_equal(const m16_cont_info *a,const m16_cont_info *b){
 return a->epoch==b->epoch && a->window==b->window && a->source_id==b->source_id && a->ordinal==b->ordinal &&
  a->stream_count==b->stream_count && a->current_confirmed==b->current_confirmed && a->final==b->final &&
  a->output_start==b->output_start && a->output_end==b->output_end && a->input_start==b->input_start && a->input_end==b->input_end && a->available_input==b->available_input;
}
static int consume(void){
 const m16_cont_info *info;const double *a,*b;unsigned n;int rc=m16_continuous_task_output(&task,&info,&a,&b,&n);
 if(rc!=1)return -1000;
 if(comparison){
  if(consumed>=expected_count || sizes[consumed]!=n || !info_equal(info,info_expected+consumed)){first_difference=1;return -1001;}
  if(memcmp(a,expected[consumed][0],n*8u)){first_difference=2;return -1002;}
  if(memcmp(b,expected[consumed][1],n*8u)){first_difference=3;return -1003;}
  matched_values+=n*2u;
 }
 consumed++;return m16_continuous_task_release(&task,info->epoch,info->window,info->source_id);
}
int ct_run(unsigned target){
 unsigned n=0;int rc;
 for(;;){
  if(target && task.phase==target)return 1;
  if(task.phase==M16_CT_OUTPUT_READY){rc=consume();if(rc)return rc;}
  rc=m16_continuous_task_step(&task);
  if(rc!=1){if(!rc && comparison && consumed!=expected_count)return -1004;return rc;}
  if(++n>15000000u)return -999;
 }
}
int ct_step(void){return m16_continuous_task_step(&task);}
int ct_stop(void){return m16_continuous_task_stop(&task);}
int ct_reset(unsigned epoch){comparison=0;return m16_continuous_task_reset(&task,epoch);}
int ct_releasable(void){return m16_continuous_task_releasable(&task);}
int ct_work_equal(void){return !memcmp(&work,&original,sizeof(work));}
uint64_t ct_metric(unsigned k){
 return k==0?task.phase:k==1?task.max_items:k==2?task.steps:k==3?work.windows:k==4?work.emitted:k==5?work.closed:k==6?work.failed:k==7?expected_count:k==8?consumed:k==9?matched_values:k==10?first_difference:k==11?task.id_count:k==12?task.resample.phase:task.fft.phase;
}
int ct_lease_probe(void){
 const m16_cont_info *info;const double *a,*b;unsigned n;int rc=m16_continuous_task_output(&task,&info,&a,&b,&n);if(rc!=1)return -1;
 m16_cont_info copy=*info;
 for(unsigned i=0;i<100u;i++)if(m16_continuous_task_step(&task)!=1)return -2;
 if(m16_continuous_task_release(&task,copy.epoch+1u,copy.window,copy.source_id)!=M16_CT_INVALID || task.phase!=M16_CT_OUTPUT_READY)return -3;
 if(m16_continuous_task_release(&task,copy.epoch,copy.window+1u,copy.source_id)!=M16_CT_INVALID || task.phase!=M16_CT_OUTPUT_READY)return -4;
 if(m16_continuous_task_release(&task,copy.epoch,copy.window,copy.source_id+1u)!=M16_CT_INVALID || task.phase!=M16_CT_OUTPUT_READY)return -5;
 if(comparison && (memcmp(a,expected[consumed][0],n*8u) || memcmp(b,expected[consumed][1],n*8u)))return -6;
 return 0;
}
int ct_bad(unsigned kind){
 if(kind==0)return m16_continuous_task_push(&task,input,&prediction,(uint64_t)work.windows*40960u+1u);
 if(kind==1)prediction.ids[1]=prediction.ids[0];
 if(kind==2)prediction.masks[0][10]=NAN;
 if(kind==3)prediction.weights[0]=2.f;
 if(kind==4)return m16_continuous_task_push(&task,(float *)&work,&prediction,(uint64_t)work.windows*40960u);
 if(kind==5)return m16_continuous_task_push(&task,input,(m16_cont_prediction *)&task,(uint64_t)work.windows*40960u);
 if(kind==6)prediction.count=9u;
 return ct_push();
}
void ct_environment(unsigned mode){
 fesetround(mode==1?FE_DOWNWARD:FE_TONEAREST);
#if defined(__SSE__)
 unsigned x=_mm_getcsr();x&=~0xe040u;if(mode==2)x|=0x8000u;if(mode==3)x|=0x40u;_mm_setcsr(x);
#endif
}
