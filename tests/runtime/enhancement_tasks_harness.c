/* 增强任务对照夹具，原复数FFT/通道可靠度与原冻结掩码作为独立参考。 */
#include "mic16_complex_fft_task.h"
#include "mic16_channel_weights_task.h"
#include "mic16_enh_mask_task.h"
#include "mic16_continuous.h"
#include <math.h>
#include <string.h>
#include <fenv.h>
#include <xmmintrin.h>
extern void ef_original_fft(m16_enh_workspace *,int,int);
extern void ef_original_weights(const float *,m16_enh_workspace *);
extern void ef_original_cont_fft(m16_cont_workspace *,int);
static m16_complex_fft_task fft_task;
static double real_values[4096],imaginary_values[4096],twiddle_real[2048],twiddle_imaginary[2048];
static m16_enh_workspace fft_reference,weights_work,weights_reference;
static m16_cont_workspace small_reference;
static m16_channel_weights_task weights_task;
static m16_enh_mask_task mask_task;
static m16_enh_network_workspace network,network_reference;
static float mask_values[67*64],mask_reference[67*64];
static const float *pcm,*features;
static unsigned test_n,test_inverse,test_table;
#define PI 3.14159265358979323846264338327950288
void ef_environment(unsigned mode){
 unsigned x=_mm_getcsr();
 if(mode==1)fesetround(FE_DOWNWARD);else if(mode==2)_mm_setcsr(x|0x8000u);else if(mode==3)_mm_setcsr(x|0x40u);
 else{fesetround(FE_TONEAREST);_mm_setcsr(x&~0xe040u);}
}
int ef_fft_setup(const double *r,const double *im,unsigned n,unsigned inverse,unsigned table){
 unsigned i;int rc;ef_environment(0);memset(&fft_task,0,sizeof(fft_task));
 memset(&fft_reference,0,sizeof(fft_reference));memset(&small_reference,0,sizeof(small_reference));
 test_n=n;test_inverse=inverse;test_table=table;
 for(i=0;i<4096u;i++)real_values[i]=imaginary_values[i]=-987.25;
 memcpy(real_values,r,n*8u);memcpy(imaginary_values,im,n*8u);
 memcpy(fft_reference.fft_re,r,n*8u);memcpy(fft_reference.fft_im,im,n*8u);
 if(n==512u){memcpy(small_reference.fft_re,r,n*8u);memcpy(small_reference.fft_im,im,n*8u);}
 for(i=0;i<table/2u;i++){
  twiddle_real[i]=cos(2.*PI*i/(double)table);twiddle_imaginary[i]=-sin(2.*PI*i/(double)table);
  if(table==4096u){fft_reference.twiddle_re[i]=twiddle_real[i];fft_reference.twiddle_im[i]=twiddle_imaginary[i];}
  else{small_reference.twiddle_re[i]=twiddle_real[i];small_reference.twiddle_im[i]=twiddle_imaginary[i];}
 }
 rc=m16_complex_fft_init(&fft_task,real_values,imaginary_values,table,twiddle_real,twiddle_imaginary,table);if(rc)return rc;
 return m16_complex_fft_begin(&fft_task,n,inverse);
}
int ef_fft_reference(void){
 if(test_table==512u)ef_original_cont_fft(&small_reference,(int)test_inverse);else ef_original_fft(&fft_reference,(int)test_n,(int)test_inverse);
 return 0;
}
double *ef_fft_values(unsigned old,unsigned imaginary){
 if(!old)return imaginary?imaginary_values:real_values;
 if(test_table==512u)return imaginary?small_reference.fft_im:small_reference.fft_re;
 return imaginary?fft_reference.fft_im:fft_reference.fft_re;
}
int ef_fft_step(void){return m16_complex_fft_step(&fft_task);}
int ef_fft_run(unsigned phase){unsigned i;int rc=1;for(i=0;i<10000u && rc==1;i++){if(phase && fft_task.phase==phase)return 1;rc=ef_fft_step();}return rc;}
int ef_fft_stop(void){return m16_complex_fft_stop(&fft_task);}
int ef_fft_begin(void){return m16_complex_fft_begin(&fft_task,test_n,test_inverse);}
int ef_fft_releasable(void){return m16_complex_fft_releasable(&fft_task);}
uint64_t ef_fft_metric(unsigned k){switch(k){case 0:return fft_task.phase;case 1:return fft_task.max_items;case 2:return fft_task.steps;default:return 0;}}
void ef_fft_bad(unsigned k,unsigned at){
 union{uint64_t u;double d;}v;v.u=UINT64_C(0x7ff8000000000000);
 if(k==0 && at<4096u)real_values[at]=v.d;
 if(k==1 && at<2048u)twiddle_real[at]=v.d;
}
int ef_weights_setup(const float *x){
 int rc;ef_environment(0);memset(&weights_task,0,sizeof(weights_task));memset(&weights_work,0,sizeof(weights_work));memset(&weights_reference,0,sizeof(weights_reference));
 pcm=x;rc=m16_channel_weights_init(&weights_task,&weights_work);if(rc)return rc;
 return m16_channel_weights_begin(&weights_task,x,40960u*16u);
}
int ef_weights_reference(void){ef_original_weights(pcm,&weights_reference);return 0;}
int ef_weights_step(void){return m16_channel_weights_step(&weights_task);}
int ef_weights_run(unsigned phase){unsigned i;int rc=1;for(i=0;i<3000000u && rc==1;i++){if(phase && weights_task.phase==phase)return 1;rc=ef_weights_step();}return rc;}
int ef_weights_stop(void){return m16_channel_weights_stop(&weights_task);}
int ef_weights_begin(void){return m16_channel_weights_begin(&weights_task,pcm,40960u*16u);}
int ef_weights_releasable(void){return m16_channel_weights_releasable(&weights_task);}
float *ef_weights_values(unsigned old){return old?weights_reference.weights:weights_work.weights;}
int ef_weights_workspace_equal(void){
 return memcmp(weights_work.gcc_spectra,weights_reference.gcc_spectra,sizeof(weights_work.gcc_spectra))==0 &&
  memcmp(weights_work.fft_re,weights_reference.fft_re,sizeof(weights_work.fft_re))==0 &&
  memcmp(weights_work.fft_im,weights_reference.fft_im,sizeof(weights_work.fft_im))==0 &&
  memcmp(weights_work.twiddle_re,weights_reference.twiddle_re,sizeof(weights_work.twiddle_re))==0 &&
  memcmp(weights_work.twiddle_im,weights_reference.twiddle_im,sizeof(weights_work.twiddle_im))==0;
}
uint64_t ef_weights_metric(unsigned k){switch(k){case 0:return weights_task.phase;case 1:return weights_task.max_items;case 2:return weights_task.steps;
 case 3:return weights_task.forward_transforms;case 4:return weights_task.inverse_transforms;case 5:return weights_task.cursor;default:return 0;}}
int ef_mask_setup(const float *x){
 unsigned i;int rc;ef_environment(0);memset(&mask_task,0,sizeof(mask_task));memset(&network,0,sizeof(network));memset(&network_reference,0,sizeof(network_reference));
 for(i=0;i<67u*64u;i++)mask_values[i]=mask_reference[i]=-987.25f;
 features=x;rc=m16_enh_mask_task_init(&mask_task,&network);if(rc)return rc;
 return m16_enh_mask_task_begin(&mask_task,x,67u*64u*10u,mask_values,67u*64u);
}
int ef_mask_reference(void){return m16_enh_mask(features,mask_reference,&network_reference);}
int ef_mask_step(void){return m16_enh_mask_task_step(&mask_task);}
int ef_mask_run(unsigned phase){unsigned i;int rc=1;for(i=0;i<200000u && rc==1;i++){if(phase && mask_task.phase==phase)return 1;rc=ef_mask_step();}return rc;}
int ef_mask_stop(void){return m16_enh_mask_task_stop(&mask_task);}
int ef_mask_begin(void){return m16_enh_mask_task_begin(&mask_task,features,67u*64u*10u,mask_values,67u*64u);}
int ef_mask_releasable(void){return m16_enh_mask_task_releasable(&mask_task);}
float *ef_mask_values(unsigned old){return old?mask_reference:mask_values;}
int ef_mask_workspace_equal(void){return memcmp(&network,&network_reference,sizeof(network))==0;}
uint64_t ef_mask_metric(unsigned k){switch(k){case 0:return mask_task.phase;case 1:return mask_task.max_items;case 2:return mask_task.steps;default:return 0;}}
