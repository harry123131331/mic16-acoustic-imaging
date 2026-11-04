/* 对照真实既有FP64实FFT前端，不用新任务函数生成参考。 */
#include "mic16_public_spectrum_task.h"
#include <string.h>
#include <fenv.h>
extern int m16_spectral_process_real(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
unsigned m16_fft_fast_mode,m16_fft_fast_calls[3];
static m16_public_spectrum_task task;
static mic16_spectral_frame output,reference;
static mic16_spectral_workspace work,reference_work;
static int reference_rc;
int ps_reset(const float *pcm,unsigned values,uint64_t epoch,uint64_t frame){
 memset(&task,0,sizeof(task));memset(&output,0xa5,sizeof(output));memset(&reference,0xa5,sizeof(reference));
 mic16_spectral_init(&work);mic16_spectral_init(&reference_work);
 int rc=m16_public_spectrum_init(&task,&output,&work);
 if(!rc)rc=m16_public_spectrum_begin(&task,pcm,values,epoch,frame);
 return rc;
}
int ps_begin(const float *pcm,unsigned values,uint64_t epoch,uint64_t frame){return m16_public_spectrum_begin(&task,pcm,values,epoch,frame);}
int ps_reference(const float *pcm){reference_rc=m16_spectral_process_real(pcm,&reference,&reference_work);return reference_rc;}
int ps_step(void){return m16_public_spectrum_step(&task);}
int ps_stop(void){return m16_public_spectrum_stop(&task);}
int ps_copy(unsigned ref,double *spectrum,double *power,double *zones,float *phat){
 const mic16_spectral_frame *f=ref?&reference:&output;
 memcpy(spectrum,f->spectrum,sizeof(f->spectrum));memcpy(power,f->mean_channel_power,sizeof(f->mean_channel_power));
 memcpy(zones,f->zone_level_db,sizeof(f->zone_level_db));memcpy(phat,f->phat,sizeof(f->phat));return 0;
}
void ps_damage(unsigned which){if(which==0)work.hann[1024]=0./0.;else if(which==1)work.twiddle_re[64]=0./0.;else work.initialized=0;}
int ps_begin_alias(unsigned which){
 const float *p=which?(const float *)&work:(const float *)&output;
 return m16_public_spectrum_begin(&task,p,2048u*16u,13,24);
}
int ps_round_down(unsigned active){return fesetround(active?FE_DOWNWARD:FE_TONEAREST);}
uint64_t ps_metric(unsigned i){
 switch(i){case 0:return task.phase;case 1:return task.last_items;case 2:return task.max_items;
 case 3:return task.channel;case 4:return task.cursor;case 5:return task.steps;case 6:return task.epoch;case 7:return task.frame;
 case 8:return (uint64_t)(int64_t)task.error;case 9:return memcmp(work.hann,reference_work.hann,sizeof(work.hann))==0;
 case 10:return memcmp(work.twiddle_re,reference_work.twiddle_re,sizeof(work.twiddle_re))==0&&memcmp(work.twiddle_im,reference_work.twiddle_im,sizeof(work.twiddle_im))==0;
 case 11:return sizeof(task);default:return 0;}
}
