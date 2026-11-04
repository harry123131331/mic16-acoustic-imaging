#include "mic16_resample_task.h"
#include "mic16_weighted_beam_task.h"
#include "mic16_enh_spectrum_task.h"
#include "mic16_enhancement.h"
#include "mic16_enh_feature_task.h"
#include "mic16_resample_tables.h"
#include <string.h>
#include <math.h>
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
/* 只用于离线对照的静态工作区，不进入板端。 */
static m16_resample_task rs;
static m16_weighted_beam_task wb;
static m16_enh_spectrum_task es;
static m16_enh_feature_task ft;
static m16_enh_workspace ew,original_ew;
static mic16_extended_workspace shared,original_shared;
static double resampled[18712],reference_resampled[18712],direction[2];
static const float *source32;
static const double *source64;
static unsigned mode,samples,stride,channel,candidate;
static double directions[32];
static unsigned char feature_valid[16];
extern int es_original_resample(const float *,unsigned,unsigned,unsigned,double *);
extern unsigned es_original_resample64(const double *,unsigned,double *);
extern int es_original_weighted_beam(const float *,const double *,m16_enh_workspace *);
extern int es_original_observed(const float *,const float *,mic16_extended_workspace *);
extern void es_original_beam_spectra(const double *,unsigned,mic16_extended_workspace *);
extern int es_original_feature(const double *,const unsigned char *,int,mic16_extended_workspace *);
static int run_rs(unsigned target){int rc;unsigned n=0;for(;;){if(target && rs.phase==target)return 1;rc=m16_resample_task_step(&rs);if(rc!=1)return rc;if(++n>100000u)return -999;}}
int es_rs_setup32(const float *x,unsigned n,unsigned s,unsigned c){
 memset(&rs,0,sizeof(rs));source32=x;source64=0;samples=n;stride=s;channel=c;
 for(unsigned i=0;i<18712u;i++)resampled[i]=reference_resampled[i]=-987.25;
 int rc=m16_resample_task_init(&rs);return rc?rc:m16_resample_task_begin32(&rs,x,n,s,c,resampled,18712);
}
int es_rs_setup64(const double *x,unsigned n){
 memset(&rs,0,sizeof(rs));source32=0;source64=x;samples=n;
 for(unsigned i=0;i<18712u;i++)resampled[i]=reference_resampled[i]=-987.25;
 int rc=m16_resample_task_init(&rs);return rc?rc:m16_resample_task_begin64(&rs,x,n,resampled,18712);
}
int es_rs_reference(void){if(source64){es_original_resample64(source64,samples,reference_resampled);return 0;}return es_original_resample(source32,samples,stride,channel,reference_resampled)<0?-1:0;}
int es_rs_run(unsigned phase){return run_rs(phase);}
int es_rs_step(void){return m16_resample_task_step(&rs);}
int es_rs_stop(void){return m16_resample_task_stop(&rs);}
int es_rs_begin(void){return source64?m16_resample_task_begin64(&rs,source64,samples,resampled,18712):m16_resample_task_begin32(&rs,source32,samples,stride,channel,resampled,18712);}
int es_rs_releasable(void){return m16_resample_task_releasable(&rs);}
double *es_rs_values(unsigned original){return original?reference_resampled:resampled;}
uint64_t es_rs_metric(unsigned k){return k==0?rs.phase:k==1?rs.max_items:rs.steps;}
int es_wb_setup(const float *x,const float *weights,const double *d){
 memset(&wb,0,sizeof(wb));memset(&ew,0,sizeof(ew));memset(&original_ew,0,sizeof(original_ew));
 source32=x;direction[0]=d[0];direction[1]=d[1];memcpy(ew.weights,weights,320u*4u);memcpy(original_ew.weights,weights,320u*4u);
 int rc=m16_weighted_beam_init(&wb);return rc?rc:m16_weighted_beam_begin(&wb,x,ew.weights,direction,ew.beam);
}
int es_wb_reference(void){return es_original_weighted_beam(source32,direction,&original_ew);}
int es_wb_run(unsigned target){int rc;unsigned n=0;for(;;){if(target && wb.phase==target)return 1;rc=m16_weighted_beam_step(&wb);if(rc!=1)return rc;if(++n>150000u)return -999;}}
int es_wb_step(void){return m16_weighted_beam_step(&wb);}
int es_wb_stop(void){return m16_weighted_beam_stop(&wb);}
int es_wb_begin(void){return m16_weighted_beam_begin(&wb,source32,ew.weights,direction,ew.beam);}
int es_wb_releasable(void){return m16_weighted_beam_releasable(&wb);}
double *es_wb_values(unsigned original){return original?original_ew.beam:ew.beam;}
uint64_t es_wb_metric(unsigned k){return k==0?wb.phase:k==1?wb.max_items:wb.steps;}
int es_wb_alias(unsigned k){
 m16_weighted_beam_stop(&wb);
 if(k==0)return m16_weighted_beam_begin(&wb,source32,ew.weights,direction,(double *)source32);
 if(k==1)return m16_weighted_beam_begin(&wb,source32,ew.weights,(double *)&wb,ew.beam);
 return m16_weighted_beam_begin(&wb,source32,ew.weights,direction,(double *)ew.weights);
}
static const float *quiet;
int es_spectrum_setup32(const float *pcm,const float *reference,unsigned which){
 memset(&es,0,sizeof(es));memset(&shared,0,sizeof(shared));memset(&original_shared,0,sizeof(original_shared));
 source32=pcm;quiet=reference;mode=which;int rc=m16_enh_spectrum_init(&es,&shared);
 return rc?rc:m16_enh_spectrum_begin32(&es,which,which?reference:pcm,which?8192u*16u:40960u*16u);
}
int es_spectrum_setup64(const double *beam,unsigned c){
 memset(&es,0,sizeof(es));memset(&shared,0,sizeof(shared));memset(&original_shared,0,sizeof(original_shared));
 source64=beam;mode=2;candidate=c;int rc=m16_enh_spectrum_init(&es,&shared);return rc?rc:m16_enh_spectrum_begin64(&es,beam,c);
}
int es_spectrum_reference(void){
 if(mode==2){es_original_beam_spectra(source64,candidate,&original_shared);return 0;}
 return es_original_observed(source32,quiet,&original_shared);
}
int es_spectrum_run(unsigned target){int rc;unsigned n=0;for(;;){if(target && es.phase==target)return 1;rc=m16_enh_spectrum_step(&es);if(rc!=1)return rc;if(++n>600000u)return -999;}}
int es_spectrum_step(void){return m16_enh_spectrum_step(&es);}
int es_spectrum_stop(void){return m16_enh_spectrum_stop(&es);}
int es_spectrum_begin(void){return mode==2?m16_enh_spectrum_begin64(&es,source64,candidate):m16_enh_spectrum_begin32(&es,mode,mode?quiet:source32,mode?8192u*16u:40960u*16u);}
int es_spectrum_releasable(void){return m16_enh_spectrum_releasable(&es);}
double *es_spectrum_values(unsigned original,unsigned kind){
 mic16_extended_workspace *w=original?&original_shared:&shared;
 return kind==0?w->observed:kind==1?w->channel_power:kind==2?w->quiet_rows:kind==3?w->quiet_power:kind==4?w->beam_power:kind==5?w->resampled:kind==6?w->fft_re:kind==7?w->fft_im:w->spectrum;
}
uint64_t es_spectrum_metric(unsigned k){return k==0?es.phase:k==1?es.max_items:k==2?es.steps:k==3?es.transforms:k==4?es.resample.phase:es.fft.phase;}
int es_spectrum_alias(unsigned k){
 m16_enh_spectrum_stop(&es);
 return k==0?m16_enh_spectrum_begin32(&es,0,(float *)&shared,40960u*16u):m16_enh_spectrum_begin64(&es,(double *)&es,0);
}
int es_feature_setup(const double *observed,const double *channel_power,const double *quiet_power,const double *beam_power,
 const double *d,const unsigned char *bits,unsigned c){
 memset(&ft,0,sizeof(ft));memset(&shared,0,sizeof(shared));memset(&original_shared,0,sizeof(original_shared));
 memcpy(shared.observed,observed,sizeof(shared.observed));memcpy(shared.channel_power,channel_power,sizeof(shared.channel_power));
 memcpy(shared.quiet_power,quiet_power,sizeof(shared.quiet_power));memcpy(shared.beam_power,beam_power,sizeof(shared.beam_power));
 memcpy(&original_shared,&shared,sizeof(shared));memcpy(directions,d,sizeof(directions));memcpy(feature_valid,bits,16);candidate=c;
 int rc=m16_enh_feature_init(&ft,&shared);return rc?rc:m16_enh_feature_begin(&ft,directions,feature_valid,c);
}
int es_feature_reference(void){return es_original_feature(directions,feature_valid,(int)candidate,&original_shared);}
int es_feature_run(unsigned target){int rc;unsigned n=0;for(;;){if(target && ft.phase==target)return 1;rc=m16_enh_feature_step(&ft);if(rc!=1)return rc;if(++n>150000u)return -999;}}
int es_feature_step(void){return m16_enh_feature_step(&ft);}
int es_feature_stop(void){return m16_enh_feature_stop(&ft);}
int es_feature_begin(void){return m16_enh_feature_begin(&ft,directions,feature_valid,candidate);}
int es_feature_releasable(void){return m16_enh_feature_releasable(&ft);}
float *es_feature_values(unsigned original){return original?original_shared.spectral10:shared.spectral10;}
double *es_feature_signature(unsigned original){return original?original_shared.signature:shared.signature;}
uint64_t es_feature_metric(unsigned k){return k==0?ft.phase:k==1?ft.max_items:ft.steps;}
void es_feature_bad(unsigned k){
 /* 第1频点第0通道；DC不参与这条特征定义，不能把未使用DC当故障注入。 */
 if(k==0)shared.observed[32]=NAN;
 if(k==1)shared.channel_power[0]=NAN;
 if(k==2)shared.quiet_power[0]=-1.;
 if(k==3)shared.beam_power[candidate*67u*64u]=-1.;
}
void es_environment(unsigned mode_){
 fesetround(mode_==1?FE_DOWNWARD:FE_TONEAREST);
#if defined(__SSE__)
 unsigned x=_mm_getcsr();x&=~0xe040u;if(mode_==2)x|=0x8000u;if(mode_==3)x|=0x40u;_mm_setcsr(x);
#endif
}
