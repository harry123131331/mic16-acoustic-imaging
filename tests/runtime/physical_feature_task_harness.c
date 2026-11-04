/* 两个真实驱动/空间核C模型与候选生产者联合；FFT设备采用已知复数控制注入。 */
#include "mic16_physical_feature_task.h"
#include "mic16_shared_spectrum_batch.h"
#include "mic16_feature_fast.h"
#include <string.h>
extern m16_shared_spectrum_task *ss_task_bridge(void);
extern int ss_reset(unsigned),ss_device_input_copy(uint64_t *,unsigned);
extern void ss_complete(unsigned,unsigned);
extern uint64_t ss_metric(unsigned);
extern void *pp_runtime_bridge(unsigned);
extern int m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int m16_physical_features_quiet(const float *,const float *,const double *,unsigned,const double *,float *,mic16_physical_workspace *,const mic16_prepared_spectral *,mic16_candidate_beam_sink,void *);
static m16_physical_feature_task task;
static mic16_physical_workspace current,reference;
static struct {float before[16],values[16*20*88],after[16];} output;
static float expected[16*20*88];
static uint64_t arena[M16_SHARED_ARENA_WORDS] __attribute__((aligned(64)));
static uint64_t inputs[32][16384],actual[16384];
static unsigned active[32],calls,ready,served,base,checks,count;
static const float *pcm,*quiet;
static const double *votes;
static double directions[32];
int mic16_candidate_beam_indexed(const float *x,unsigned n,const double *d,float *b,unsigned *g){return mic16_candidate_beam_phase_fast(x,n,d,b,g);}
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
int pf_setup(const float *x,const float *q,const float *phat,const double *levels,const double *floor,
 const double *d,unsigned c,const double *v,unsigned overlap_physical){
 unsigned i;int rc=ss_reset(0);m16_projection_runtime *r;
 if(rc)return rc;
 r=(m16_projection_runtime *)pp_runtime_bridge(overlap_physical?0x04000000u:0x02000000u);if(!r)return -999;
 memset(&task,0,sizeof(task));memset(&current,0,sizeof(current));memset(&reference,0,sizeof(reference));
 mic16_spectral_init(&current.spectral);mic16_spectral_init(&reference.spectral);
 current.reference.count=reference.reference.count=4;current.reference.finalized=reference.reference.finalized=1;
 memcpy(current.phat,phat,sizeof(current.phat));memcpy(reference.phat,phat,sizeof(reference.phat));
 memcpy(current.levels,levels,sizeof(current.levels));memcpy(reference.levels,levels,sizeof(reference.levels));
 memcpy(current.reference.floor_db,floor,24u*8u);memcpy(reference.reference.floor_db,floor,24u*8u);
 for(i=0;i<16;i++)output.before[i]=output.after[i]=-1234.75f;
 for(i=0;i<16u*20u*88u;i++)output.values[i]=expected[i]=-987.25f;
 if(c<1 || c>16)return -999;
 pcm=x;quiet=q;votes=v;count=c;memcpy(directions,d,c*16u);calls=ready=served=base=checks=0;
 rc=m16_physical_feature_init(&task,ss_task_bridge(),r,&current);if(rc)return rc;
 {mic16_prepared_spectral p={x,q,&current};
 return m16_physical_feature_begin(&task,x,40960u*16u,q,8192u*16u,d,c,v,480u*9u,output.values,c*20u*88u,&p,12,1,1000);}
}
int pf_reference(void){
 mic16_prepared_spectral p={pcm,quiet,&reference};int rc;
 rc=m16_physical_features_quiet(pcm,quiet,directions,count,votes,expected,&reference,&p,0,0);ready=rc==0;return rc;
}
int pf_step(void){return m16_physical_feature_step(&task);}
int pf_stop(void){return m16_physical_feature_stop(&task);}
int pf_releasable(void){return m16_physical_feature_releasable(&task);}
int pf_fft_service(unsigned kind){
 unsigned i,n,j=served-base;
 if(ss_metric(0)!=M16_SS_DEVICE || ss_metric(3)==served)return 0;
 if(ss_metric(3)!=served+1u)return -999;
 n=ss_task_bridge()->active;if(ss_device_input_copy(actual,n*1024u))return -998;
 if(ready){
  if(j>=calls || active[j]!=n)return -997;
  for(i=0;i<n*1024u;i++){if(actual[i]!=inputs[j][i])return -996;checks++;}
 }
 served++;ss_complete(kind,1);return 1;
}
int pf_copy(unsigned old,float *out){if(!out)return -999;memcpy(out,old?expected:output.values,count*20u*88u*4u);return 0;}
int pf_guards(void){
 unsigned i;for(i=0;i<16;i++)if(output.before[i]!=-1234.75f || output.after[i]!=-1234.75f)return -1;
 for(i=count*20u*88u;i<16u*20u*88u;i++)if(output.values[i]!=-987.25f)return -2;
 return 0;
}
int pf_begin(uint64_t epoch,uint64_t job,uint64_t tag){
 mic16_prepared_spectral p={pcm,quiet,&current};
 int rc=m16_physical_feature_begin(&task,pcm,40960u*16u,quiet,8192u*16u,directions,count,votes,480u*9u,
  output.values,count*20u*88u,&p,epoch,job,tag);
 if(!rc)base=served;
 return rc;
}
uint64_t pf_metric(unsigned k){
 switch(k){case 0:return task.phase;case 1:return task.own_phase;case 2:return task.candidate;
 case 3:return task.cursor;case 4:return task.frame;case 5:return task.projection.phase;case 6:return task.acoustics.phase;
 case 7:return task.projection_owned;case 8:return task.projection_terminal;case 9:return task.acoustics_terminal;
 case 10:return task.steps;case 11:return task.CPU_steps_while_projection;case 12:return (uint64_t)(int64_t)task.error;
 case 13:return task.projection.results;case 14:return task.projection.transactions;case 15:return task.last_items;
 case 16:return task.max_items;case 17:return task.acoustics.mono.max_items;case 18:return task.acoustics.beam.max_items;
 case 19:return task.reserved_job_high;case 20:return task.reserved_tag_high;case 21:return task.projection.out;
 case 22:return task.projection.receipts;case 23:return checks;case 24:return task.projection_done;
 case 25:return sizeof(task);case 26:return task.beam_leases;case 27:return task.beam_releases;default:return UINT64_MAX;}
}
int pf_hold_beam(unsigned enabled){return m16_physical_feature_hold_beam(&task,enabled);}
const float *pf_beam(uint64_t epoch,unsigned candidate){return m16_physical_feature_beam(&task,epoch,candidate);}
int pf_release_beam(uint64_t epoch,unsigned candidate){return m16_physical_feature_release_beam(&task,epoch,candidate);}
int pf_beam_reference(unsigned candidate,float *out){
 unsigned guard;if(candidate>=count || !out)return -999;
 return mic16_candidate_beam_phase_fast(pcm,40960,directions+candidate*2u,out,&guard);
}
void pf_corrupt(unsigned k,unsigned index){
 union{uint64_t u;double d;}d;union{uint32_t u;float f;}f;d.u=UINT64_C(0x7ff8000000000000);f.u=0x7fc00000u;
 if(k==0 && index<M16_PP_PHAT_VALUES)current.phat[index]=f.f;
 if(k==1 && index<480)current.levels[index]=d.d;
 if(k==2 && index<24)current.reference.floor_db[index]=d.d;
 if(k==3 && index<480u*9u)memcpy((double *)votes+index,&d.d,8);
 if(k==4 && index<count*20u*88u)output.values[index]=f.f;
}
int pf_bad_begin(unsigned k){
 mic16_prepared_spectral p={pcm,quiet,&current};const float *x=pcm,*q=quiet;
 const double *d=directions,*v=votes;float *out=output.values;
 unsigned xn=40960u*16u,qn=8192u*16u,vn=480u*9u,on=count*20u*88u;uint64_t epoch=12,job=100,tag=2000;
 switch(k){case 0:p.owner=&reference;break;case 1:p.pcm=quiet;break;case 2:x=current.beam;break;case 3:q=pcm;break;
 case 4:out=(float *)votes;break;case 5:xn--;break;case 6:qn--;break;case 7:vn--;break;case 8:on--;break;
 case 9:job=UINT64_MAX;break;case 10:tag=UINT64_MAX;break;case 11:epoch=0;break;case 12:job=1;break;
 case 13:tag=1000;break;default:return -999;}
 return m16_physical_feature_begin(&task,x,xn,q,qn,d,count,v,vn,out,on,&p,epoch,job,tag);
}
