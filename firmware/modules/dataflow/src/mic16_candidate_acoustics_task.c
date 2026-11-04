#include "mic16_candidate_acoustics_task.h"
#define MAGIC 0x4d434143u
static int valid(const m16_candidate_acoustics_task *t){return t && !((uintptr_t)t%_Alignof(m16_candidate_acoustics_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int fail(m16_candidate_acoustics_task *t,int rc){t->phase=M16_CA_FAILED;t->error=rc;return rc;}
int m16_candidate_acoustics_init(m16_candidate_acoustics_task *t,m16_shared_spectrum_task *d,mic16_physical_workspace *w){
 int rc;
 if(!t || !d || !w || (uintptr_t)t%_Alignof(m16_candidate_acoustics_task) || (uintptr_t)d%_Alignof(m16_shared_spectrum_task) ||
  (uintptr_t)w%_Alignof(mic16_physical_workspace) || t->magic || !m16_shared_spectrum_task_available(d) ||
  !apart(t,sizeof(*t),d,sizeof(*d)) || !apart(t,sizeof(*t),d->owner,sizeof(*d->owner)) ||
  !apart(t,sizeof(*t),w,sizeof(*w)) || !apart(w,sizeof(*w),d,sizeof(*d)) || !apart(w,sizeof(*w),d->owner,sizeof(*d->owner)))return M16_CA_INVALID;
 *t=(m16_candidate_acoustics_task){0};
 rc=m16_candidate_beam_init(&t->beam);if(rc)return rc;
 rc=m16_mono_feature_init(&t->mono,d,&w->spectral,w->scratch,w->powers);if(rc)return rc;
 t->magic=MAGIC;t->device=d;t->work=w;return 0;
}
int m16_candidate_acoustics_begin(m16_candidate_acoustics_task *t,const float *pcm,unsigned pn,
 const float *ref,unsigned rn,const double *dir,uint64_t epoch,uint64_t job){
 const void *fixed[4];uintptr_t sizes[4];unsigned i;int rc;
 if(!valid(t) || !pcm || !ref || !dir || pn<40960u*16u || rn<8192u*16u || !epoch || !job || job==UINT64_MAX ||
  (uintptr_t)pcm%4u || (uintptr_t)ref%4u || (uintptr_t)dir%8u)return M16_CA_INVALID;
 if(!m16_candidate_acoustics_releasable(t))return M16_CA_BUSY;
 fixed[0]=t;fixed[1]=t->work;fixed[2]=t->device;fixed[3]=t->device->owner;
 sizes[0]=sizeof(*t);sizes[1]=sizeof(*t->work);sizes[2]=sizeof(*t->device);sizes[3]=sizeof(*t->device->owner);
 for(i=0;i<4;i++)if(!apart(pcm,40960u*16u*4u,fixed[i],sizes[i]) || !apart(ref,8192u*16u*4u,fixed[i],sizes[i]) ||
  !apart(dir,16u,fixed[i],sizes[i]))return M16_CA_INVALID;
 if(!apart(pcm,40960u*16u*4u,ref,8192u*16u*4u))return M16_CA_INVALID;
 rc=m16_candidate_beam_begin(&t->beam,pcm,pn,40960u,dir,t->work->beam,40960u);if(rc)return rc;
 t->pcm=pcm;t->reference=ref;t->direction[0]=dir[0];t->direction[1]=dir[1];t->epoch=epoch;t->first_job=job;
 t->error=0;t->phase=M16_CA_BEAM;return 0;
}
int m16_candidate_acoustics_step(m16_candidate_acoustics_task *t){
 int rc;
 if(!valid(t))return M16_CA_INVALID;
 if(t->phase==M16_CA_COMPLETE)return 0;
 if(t->phase==M16_CA_STOPPED)return 2;
 if(t->phase==M16_CA_FAILED)return t->error;
 if(t->phase==M16_CA_STOPPING){
  rc=m16_mono_feature_step(&t->mono);if(rc==1)return 1;
  if(rc<0)return fail(t,rc);
  t->phase=M16_CA_STOPPED;return 2;
 }
 if(t->phase==M16_CA_BEAM || t->phase==M16_CA_REFERENCE_BEAM){
  rc=m16_candidate_beam_step(&t->beam);if(rc<0)return fail(t,rc);if(rc==1)return 1;
  if(rc==2){t->phase=M16_CA_STOPPED;return 2;}
  if(t->phase==M16_CA_BEAM){
   t->guard=t->beam.guard;
   rc=m16_mono_feature_begin(&t->mono,t->work->beam,40960u,20,0,t->work->bands,640,t->work->beam_level,20,t->epoch,t->first_job);
   if(rc)return fail(t,rc);
   t->phase=M16_CA_MONO;
  }else{
   t->reference_guard=t->beam.guard;
   rc=m16_mono_feature_begin(&t->mono,t->work->quiet_beam,8192u,4,1,0,0,t->work->quiet_level,4,t->epoch,t->first_job);
   if(rc)return fail(t,rc);
   t->phase=M16_CA_REFERENCE_LEVELS;
  }
  return 1;
 }
 if(t->phase==M16_CA_MONO || t->phase==M16_CA_REFERENCE_LEVELS){
  rc=m16_mono_feature_step(&t->mono);if(rc<0)return fail(t,rc);if(rc==1)return 1;
  if(rc==2){t->phase=M16_CA_STOPPED;return 2;}
  if(t->phase==M16_CA_MONO){
   rc=m16_candidate_beam_begin(&t->beam,t->reference,8192u*16u,8192u,t->direction,t->work->quiet_beam,8192u);
   if(rc)return fail(t,rc);
   t->phase=M16_CA_REFERENCE_BEAM;return 1;
  }else{
   double r[4];unsigned i,k;
   for(i=0;i<4;i++)r[i]=t->work->quiet_level[i];
   for(i=1;i<4;i++){double a=r[i];k=i;while(k>0 && r[k-1]>a){r[k]=r[k-1];k--;}r[k]=a;}
   t->reference_floor=(r[1]+r[2])/2.;t->phase=M16_CA_COMPLETE;return 0;
  }
 }
 return M16_CA_INVALID;
}
int m16_candidate_acoustics_stop(m16_candidate_acoustics_task *t){
 int rc;if(!valid(t))return M16_CA_INVALID;
 if(t->phase==M16_CA_BEAM || t->phase==M16_CA_REFERENCE_BEAM){
  rc=m16_candidate_beam_stop(&t->beam);if(rc<0)return rc;t->phase=M16_CA_STOPPED;return 1;
 }
 rc=m16_mono_feature_stop(&t->mono);if(rc<0)return rc;
 t->phase=rc?M16_CA_STOPPED:M16_CA_STOPPING;return rc;
}
int m16_candidate_acoustics_releasable(const m16_candidate_acoustics_task *t){
 return valid(t) && (t->phase==M16_CA_IDLE || t->phase==M16_CA_COMPLETE || t->phase==M16_CA_STOPPED || t->phase==M16_CA_FAILED) &&
  m16_candidate_beam_releasable(&t->beam) && m16_mono_feature_releasable(&t->mono);
}
