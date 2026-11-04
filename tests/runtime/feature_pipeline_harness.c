/* 完整特征父图与保留prepared入口对照；FFT设备仍为已知复数控制注入。 */
#include "mic16_feature_pipeline_task.h"
#include "mic16_shared_spectrum_batch.h"
#include "mic16_feature_fast.h"
#include "mic16_simd_fft.h"
#include "mic16_stft_batch.h"
#include "mic16_statistics_neon.h"
#include <string.h>
#include <fenv.h>
#include <xmmintrin.h>
extern m16_shared_spectrum_task *ss_task_bridge(void);
extern int ss_reset(unsigned),ss_device_input_copy(uint64_t *,unsigned);
extern void ss_complete(unsigned,unsigned);
extern uint64_t ss_metric(unsigned),pp_metric(unsigned);
extern void *pp_runtime_bridge(unsigned);
extern int fp_hardware(unsigned);
extern int m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int m16_physical_features_quiet(const float *,const float *,const double *,unsigned,const double *,float *,mic16_physical_workspace *,const mic16_prepared_spectral *,mic16_candidate_beam_sink,void *);
extern int mic16_statistics18_batched(const float *,const float *,const double *,unsigned,float *,mic16_extended_workspace *,unsigned,unsigned,mic16_feature_reuse_report *);
extern int mic16_statistics_beam_power_batched(const float *,unsigned,mic16_extended_workspace *);
extern int m16_statistics_neon(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
extern int sp_fp64_reference(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
extern int m16_stft512_visit_real(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
unsigned m16_feature_fast_calls[3],m16_fft_fast_calls[3];
static m16_feature_pipeline_task task;
static mic16_extended_workspace current,reference;
static m16_statistics_projection_storage storage;
static m16_projection_runtime *runtime;
static struct{float before[16],values[16*20*130],after[16];} output;
static float expected[16*20*130];
static double model[32],expected_model[32],directions[32];
static unsigned char bits[16],expected_bits[16];
static uint64_t arena[M16_SHARED_ARENA_WORDS] __attribute__((aligned(64)));
static uint64_t inputs[32][16384],actual[16384];
static unsigned active[32],calls,ready,served,base,checks,count;
static unsigned projection_starts,device_age;
static const float *pcm,*quiet;
static const double *votes;
int mic16_candidate_beam_indexed(const float *x,unsigned n,const double *d,float *b,unsigned *g){return mic16_candidate_beam_phase_fast(x,n,d,b,g);}
int m16_original_candidate_beam_indexed(const float *x,unsigned n,const double *d,float *b,unsigned *g){return mic16_candidate_beam(x,n,d,b,g);}
static int execute(void *unused,uint64_t *p,unsigned n){
 unsigned i;(void)unused;if(calls>=32u)return -999;
 active[calls]=n;memcpy(inputs[calls],p,n*8192u);calls++;
 for(i=0;i<n*807u;i++)p[16384u+i]=(uint64_t)(0x3d800000u+(i&255u))|((uint64_t)(0xbf000000u+(i&127u))<<32);
 return 0;
}
int mic16_mono_bands(const float *b,unsigned n,double *bands,double *levels,mic16_physical_workspace *w){
 int rc=m16_shared_mono_bands(b,n,bands,levels,w,arena,M16_SHARED_ARENA_WORDS,execute,0);
 return rc==1?m16_mono_bands_real(b,n,bands,levels,w):rc;
}
int mic16_physical_features_reused(const float *x,const float *q,const double *d,unsigned c,const double *v,float *o,mic16_physical_workspace *w,
 const mic16_prepared_spectral *p,mic16_candidate_beam_sink sink,void *context){return m16_physical_features_quiet(x,q,d,c,v,o,w,p,sink,context);}
int mic16_statistics18_reused(const float *x,const float *q,const double *d,unsigned c,float *o,mic16_extended_workspace *w,unsigned reuse,unsigned beams,mic16_feature_reuse_report *r){
 return mic16_statistics18_batched(x,q,d,c,o,w,reuse,beams,r);
}
int mic16_statistics_beam_power(const float *b,unsigned c,mic16_extended_workspace *w){return mic16_statistics_beam_power_batched(b,c,w);}
int m16_original_statistics18_pair_cache(const double *d,unsigned c,float *o,mic16_extended_workspace *w,mic16_feature_reuse_report *r){return sp_fp64_reference(d,c,o,w,r);}
int mic16_stft512_visit(const double *x,unsigned n,unsigned frames,mic16_extended_workspace *w,mic16_stft_consumer f,void *p){return m16_stft512_visit_real(x,n,frames,w,f,p);}
void *fp_runtime(void){return runtime;}
int fp_begin(uint64_t epoch,uint64_t job,uint64_t tag){
 mic16_prepared_spectral p={pcm,quiet,&current.physical};
 int rc=m16_feature_pipeline_begin(&task,pcm,40960u*16u,quiet,8192u*16u,directions,count,votes,480u*9u,
  output.values,16u*20u*130u,model,bits,&p,epoch,job,tag);
 if(!rc)base=served;
 return rc;
}
int fp_setup(const float *x,const float *q,const float *phat,const double *levels,const double *floor,const double *d,unsigned c,const double *v){
 unsigned i;int rc=ss_reset(0);if(rc)return rc;
 runtime=(m16_projection_runtime *)pp_runtime_bridge(0x02000000u);if(!runtime)return -999;
 memset(&task,0,sizeof(task));memset(&current,0,sizeof(current));memset(&reference,0,sizeof(reference));memset(&storage,0,sizeof(storage));
 mic16_spectral_init(&current.physical.spectral);mic16_spectral_init(&reference.physical.spectral);
 current.physical.reference.count=reference.physical.reference.count=4;
 current.physical.reference.finalized=reference.physical.reference.finalized=1;
 memcpy(current.physical.phat,phat,sizeof(current.physical.phat));memcpy(reference.physical.phat,phat,sizeof(reference.physical.phat));
 memcpy(current.physical.levels,levels,sizeof(current.physical.levels));memcpy(reference.physical.levels,levels,sizeof(reference.physical.levels));
 memcpy(current.physical.reference.floor_db,floor,24u*8u);memcpy(reference.physical.reference.floor_db,floor,24u*8u);
 for(i=0;i<16u;i++){output.before[i]=output.after[i]=-1234.75f;bits[i]=expected_bits[i]=255u;}
 for(i=0;i<16u*20u*130u;i++)output.values[i]=expected[i]=-987.25f;
 for(i=0;i<32u;i++)model[i]=expected_model[i]=-987.25;
 if(c<1 || c>16)return -999;
 pcm=x;quiet=q;votes=v;count=c;memcpy(directions,d,c*16u);
 calls=ready=served=base=checks=projection_starts=device_age=0;
 rc=m16_feature_pipeline_init(&task,ss_task_bridge(),runtime,&current,&storage);if(rc)return rc;
 return fp_begin(12,1,1000);
}
int fp_reference(void){
 mic16_prepared_spectral p={pcm,quiet,&reference.physical};int rc;
 m16_simd_fft_select(2);m16_simd_fft_clear_tables();m16_statistics_neon_select(1);
 rc=mic16_features130_reused(pcm,quiet,directions,count,votes,expected,expected_model,expected_bits,&reference,&p,M16_REUSE_ALL,0);
 ready=rc==0;return rc;
}
int fp_step(void){return m16_feature_pipeline_step(&task);}
int fp_stop(void){return m16_feature_pipeline_stop(&task);}
int fp_releasable(void){return m16_feature_pipeline_releasable(&task);}
int fp_service(unsigned fft_kind,unsigned projection_kind){
 unsigned i,n,j=served-base;
 if(runtime->phase==M16_PR_DEVICE && pp_metric(26)==0){
  unsigned starts=(unsigned)pp_metric(2);
  if(starts!=projection_starts){projection_starts=starts;device_age=0;}
  if(++device_age>=8u){int rc=fp_hardware(projection_kind);if(rc!=(int)runtime->jobs)return -997;}
 }
 if(ss_metric(0)!=M16_SS_DEVICE || ss_metric(3)==served)return 0;
 if(ss_metric(3)!=served+1u)return -996;
 n=ss_task_bridge()->active;if(ss_device_input_copy(actual,n*1024u))return -995;
 if(ready){
  if(j>=calls || active[j]!=n)return -994;
  for(i=0;i<n*1024u;i++){if(actual[i]!=inputs[j][i])return -993;checks++;}
 }
 served++;ss_complete(fft_kind,1);return 1;
}
int fp_run(unsigned limit,unsigned fft_kind,unsigned projection_kind){
 unsigned i;int rc=1;
 for(i=0;i<limit && rc==1;i++){int s=fp_service(fft_kind,projection_kind);if(s<0)return s;rc=fp_step();}
 return rc;
}
int fp_run_to(unsigned phase,unsigned beam_active,unsigned limit){
 unsigned i;
 for(i=0;i<limit;i++){
  int rc;
  if(task.phase==phase && (!beam_active || task.beam_running))return 1;
  rc=fp_service(0,0);if(rc<0)return rc;
  rc=fp_step();if(rc!=1)return rc;
 }
 return -999;
}
int fp_until_stat_device(unsigned limit){
 unsigned i;
 for(i=0;i<limit;i++){
  int rc;
  if(task.phase==M16_FP_STATISTICS && runtime->phase==M16_PR_DEVICE && pp_metric(26)==0)return 1;
  rc=fp_service(0,0);if(rc<0)return rc;
  rc=fp_step();if(rc!=1)return rc;
 }
 return -999;
}
float *fp_output(unsigned old){return old?expected:output.values;}
double *fp_model(unsigned old){return old?expected_model:model;}
unsigned char *fp_bits(unsigned old){return old?expected_bits:bits;}
unsigned char *fp_peers(unsigned old){return old?reference.peers:current.peers;}
double *fp_spectra(unsigned old,unsigned which){
 mic16_extended_workspace *w=old?&reference:&current;
 if(which==0)return w->observed;
 if(which==1)return w->quiet_power;
 if(which==2)return w->channel_power;
 return w->beam_power;
}
int fp_guards(void){unsigned i;for(i=0;i<16;i++)if(output.before[i]!=-1234.75f || output.after[i]!=-1234.75f)return -1;return 0;}
uint64_t fp_metric(unsigned k){
 switch(k){case 0:return task.phase;case 1:return task.physical.phase;case 2:return task.front.phase;case 3:return task.statistics.phase;
 case 4:return task.beam_running;case 5:return task.beam_completed;case 6:return task.physical.beam_leases;case 7:return task.physical.beam_releases;
 case 8:return task.statistics.transactions;case 9:return task.steps;case 10:return task.max_items;case 11:return task.physical.max_items;
 case 12:return task.front.max_items;case 13:return task.statistics.max_items;case 14:return checks;case 15:return sizeof(task);
 case 16:return task.physical.candidate;case 17:return task.cursor;case 18:return task.statistics.owned;case 19:return task.statistics.out;
 case 20:return task.statistics.receipt;case 21:return task.reserved_tag_high;case 22:return task.reserved_job_high;
 case 23:return task.physical.own_phase;default:return 0;}
}
void fp_environment(unsigned mode){if(mode==1)fesetround(FE_DOWNWARD);else{fesetround(FE_TONEAREST);_mm_setcsr(_mm_getcsr()&~0xe040u);}}
void fp_restore_phat(const float *p){memcpy(current.physical.phat,p,sizeof(current.physical.phat));}
void fp_corrupt(unsigned region,unsigned i){
 union{uint64_t u;double d;}d;union{uint32_t u;float f;}x;d.u=UINT64_C(0x7ff8000000000000);x.u=0x7fc00000u;
 if(region==0 && i<20u*807u*32u*2u)current.physical.phat[i]=x.f;
 if(region==1 && i<67u*257u*16u*2u)current.observed[i]=d.d;
 if(region==2 && i<count*67u*64u)current.beam_power[i]=d.d;
 if(region==3 && i<32u)model[i]=d.d;
 if(region==4 && i<16u)bits[i]=255;
}
int fp_bad_begin(unsigned k){
 mic16_prepared_spectral p={pcm,quiet,&current.physical};
 const float *x=pcm,*q=quiet;const double *d=directions,*v=votes;float *o=output.values;double *m=model;unsigned char *b=bits;
 unsigned xn=40960u*16u,qn=8192u*16u,vn=480u*9u,on=16u*20u*130u;uint64_t epoch=13,job=1,tag=2000;
 switch(k){case 0:p.owner=&reference.physical;break;case 1:p.pcm=q;break;case 2:x=current.physical.beam;break;
 case 3:q=pcm;break;case 4:o=(float *)votes;break;case 5:xn--;break;case 6:qn--;break;case 7:vn--;break;case 8:on--;break;
 case 9:job=UINT64_MAX;break;case 10:tag=UINT64_MAX;break;case 11:epoch=0;break;case 12:epoch=12;break;
 case 13:tag=1303;break;case 14:m=(double *)&task;break;case 15:b=(unsigned char *)o;break;default:return -999;}
 return m16_feature_pipeline_begin(&task,x,xn,q,qn,d,count,v,vn,o,on,m,b,&p,epoch,job,tag);
}
