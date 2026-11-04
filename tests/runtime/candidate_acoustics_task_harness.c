/* PCM至频带/参考能量联合对照；FFT设备仍只注入已知复数控制样本。 */
#include "mic16_candidate_acoustics_task.h"
#include "mic16_shared_spectrum_batch.h"
#include "mic16_feature_fast.h"
#include <string.h>
extern m16_shared_spectrum_task *ss_task_bridge(void);
extern int ss_reset(unsigned),ss_device_input_copy(uint64_t *,unsigned);
extern void ss_complete(unsigned,unsigned);
extern uint64_t ss_metric(unsigned);
extern int m16_mono_bands_real(const float *,unsigned,double *,double *,mic16_physical_workspace *);
extern int m16_quiet_levels_reference(const float *,double *,mic16_physical_workspace *);
static m16_candidate_acoustics_task task;
static m16_candidate_beam_task beam_task;
static mic16_physical_workspace current,reference;
static float direct_beam[40960];
static uint64_t arena[M16_SHARED_ARENA_WORDS] __attribute__((aligned(64)));
static uint64_t inputs[2][16384],actual[16384];
static unsigned calls,active[2],ready,served,base,checks;
static const float *pcm,*quiet;
static double direction[2];
static unsigned original_guard,quiet_guard,reference_beam_errors;
int m16_original_candidate_beam_indexed(const float *x,unsigned n,const double *d,float *o,unsigned *g){
 return mic16_candidate_beam(x,n,d,o,g);
}
static int execute(void *unused,uint64_t *p,unsigned n){
 unsigned i;(void)unused;if(calls>=2)return -1;active[calls]=n;memcpy(inputs[calls],p,n*8192u);calls++;
 for(i=0;i<n*807u;i++)p[16384u+i]=(uint64_t)(0x3d800000u+(i&255u))|((uint64_t)(0xbf000000u+(i&127u))<<32);
 return 0;
}
int ca_setup(const float *x,const float *ref,const double *d){
 int rc=ss_reset(0);if(rc)return rc;
 memset(&task,0,sizeof(task));memset(&current,0,sizeof(current));memset(&reference,0,sizeof(reference));
 mic16_spectral_init(&current.spectral);mic16_spectral_init(&reference.spectral);
 calls=ready=served=base=checks=reference_beam_errors=0;pcm=x;quiet=ref;direction[0]=d[0];direction[1]=d[1];
 rc=m16_candidate_acoustics_init(&task,ss_task_bridge(),&current);if(rc)return rc;
 return m16_candidate_acoustics_begin(&task,x,40960u*16u,ref,8192u*16u,d,12,1);
}
int ca_reference(void){
 unsigned g;int rc;
 rc=mic16_candidate_beam_phase_fast(pcm,40960,direction,reference.beam,&original_guard);if(rc)return rc;
 rc=mic16_candidate_beam(pcm,40960,direction,direct_beam,&g);if(rc)return rc;
 if(g!=original_guard || memcmp(direct_beam,reference.beam,40960u*4u))reference_beam_errors++;
 rc=m16_shared_mono_bands(reference.beam,20,reference.bands,reference.beam_level,&reference,arena,M16_SHARED_ARENA_WORDS,execute,0);
 if(rc==1)rc=m16_mono_bands_real(reference.beam,20,reference.bands,reference.beam_level,&reference);
 if(rc)return rc;
 rc=mic16_candidate_beam_phase_fast(quiet,8192,direction,reference.quiet_beam,&quiet_guard);if(rc)return rc;
 rc=mic16_candidate_beam(quiet,8192,direction,direct_beam,&g);if(rc)return rc;
 if(g!=quiet_guard || memcmp(direct_beam,reference.quiet_beam,8192u*4u))reference_beam_errors++;
 rc=m16_quiet_levels_reference(reference.quiet_beam,reference.quiet_level,&reference);ready=rc==0;return rc;
}
int ca_step(void){return m16_candidate_acoustics_step(&task);}
int ca_stop(void){return m16_candidate_acoustics_stop(&task);}
int ca_releasable(void){return m16_candidate_acoustics_releasable(&task);}
int ca_service(unsigned kind){
 unsigned n,i,j=served-base;
 if(ss_metric(0)!=M16_SS_DEVICE || ss_metric(3)==served)return 0;
 if(ss_metric(3)!=served+1u)return -997;
 n=ss_task_bridge()->active;if(ss_device_input_copy(actual,n*1024u))return -996;
 if(ready){
  if(j>=calls || active[j]!=n)return -995;
  for(i=0;i<n*1024u;i++){if(actual[i]!=inputs[j][i])return -994;checks++;}
 }
 served++;ss_complete(kind,1);return 1;
}
int ca_copy_f32(unsigned old,unsigned ref,float *out){
 mic16_physical_workspace *w=old?&reference:&current;
 if(!out)return -999;
 memcpy(out,ref?w->quiet_beam:w->beam,(ref?8192u:40960u)*4u);return 0;
}
int ca_copy_f64(unsigned old,double *bands,double *levels,double *ref){
 mic16_physical_workspace *w=old?&reference:&current;
 memcpy(bands,w->bands,640u*8u);memcpy(levels,w->beam_level,20u*8u);memcpy(ref,w->quiet_level,4u*8u);return 0;
}
double ca_floor(void){return task.reference_floor;}
uint64_t ca_metric(unsigned k){
 switch(k){case 0:return task.phase;case 1:return task.guard;case 2:return original_guard;
 case 3:return task.reference_guard;case 4:return quiet_guard;case 5:return task.beam.max_items;
 case 6:return task.mono.max_items;case 7:return task.mono.cpu_fallback;case 8:return checks;
 case 9:return reference_beam_errors;case 10:return task.beam.phase;case 11:return task.beam.cursor;
 default:return UINT64_MAX;}
}
int ca_restart(uint64_t epoch,uint64_t job){
 int rc=m16_candidate_acoustics_begin(&task,pcm,40960u*16u,quiet,8192u*16u,direction,epoch,job);
 if(!rc)base=served;
 return rc;
}
void ca_bad_input(unsigned ref,unsigned index,uint32_t bits){
 unsigned n=ref?8192u:40960u;if(index<n*16u)memcpy((float *)(ref?quiet:pcm)+index,&bits,4);
}
int ca_bad_begin(unsigned k){
 const float *x=pcm,*ref=quiet;const double *d=direction;unsigned pn=40960u*16u,rn=8192u*16u;uint64_t epoch=12,job=3;
 switch(k){case 0:ref=pcm;break;case 1:x=current.beam;break;case 2:pn--;break;case 3:rn--;break;
 case 4:job=UINT64_MAX;break;case 5:epoch=0;break;case 6:d=0;break;case 7:x=0;break;case 8:ref=0;break;
 default:return -999;}
 return m16_candidate_acoustics_begin(&task,x,pn,ref,rn,d,epoch,job);
}
int bt_setup(const float *x,unsigned n,const double *d){
 unsigned g;int rc;
 memset(&beam_task,0,sizeof(beam_task));rc=m16_candidate_beam_init(&beam_task);if(rc)return rc;
 rc=mic16_candidate_beam_phase_fast(x,n,d,reference.beam,&original_guard);if(rc)return rc;
 rc=mic16_candidate_beam(x,n,d,direct_beam,&g);if(rc)return rc;
 if(g!=original_guard || memcmp(reference.beam,direct_beam,n*4u))return -998;
 return m16_candidate_beam_begin(&beam_task,x,n*16u,n,d,current.beam,n);
}
int bt_step(void){return m16_candidate_beam_step(&beam_task);}
unsigned bt_guard(void){return beam_task.guard;}
unsigned bt_indexed(void){return beam_task.indexed;}
unsigned bt_maximum(void){return beam_task.max_items;}
