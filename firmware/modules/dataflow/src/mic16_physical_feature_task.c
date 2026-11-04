#include "mic16_physical_feature_task.h"
#include "mic16_spatial.h"
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d504638u
static int valid(const m16_physical_feature_task *t){return t && !((uintptr_t)t%_Alignof(m16_physical_feature_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static int environment(void){
#if defined(__arm__)
 unsigned x;__asm__ volatile("vmrs %0,fpscr":"=r"(x));return !(x&0x01c00000u);
#else
 if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
 if(_mm_getcsr()&0xe040u)return 0;
#endif
 return 1;
#endif
}
static double sum64(const double *a,unsigned n){
 double r[8],s=-0.;unsigned i,j;
 if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
 for(j=0;j<8;j++)r[j]=a[j];
 for(i=8;i<n-n%8u;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
 s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
 for(;i<n;i++)s+=a[i];
 return s;
}
static int vote_valid(const double *a,unsigned i){
 unsigned k;for(k=0;k<9;k++)if(!finite64(a[k]))return 0;
 return a[0]==(double)(i/24u) && a[1]==(double)(i%24u) && a[2]>=0. && a[2]<=2. && a[2]==(double)(int)a[2] &&
  a[4]>-90. && a[4]<90. && a[5]>-90. && a[5]<90. && a[6]>=-2000. && a[6]<=2000. &&
  a[7]>=-2. && a[7]<=2. && a[8]>=0. && a[8]<=4.;
}
static int draining(m16_physical_feature_task *t,int error){
 if(!t->error)t->error=error;
 if(t->phase!=M16_PF_DRAIN){
  t->projection_terminal=!t->projection_begun || t->projection_done;
  t->acoustics_terminal=!t->acoustics_begun || t->acoustics.phase==M16_CA_COMPLETE || t->acoustics.phase==M16_CA_STOPPED || t->acoustics.phase==M16_CA_FAILED;
  t->projection_stop_sent=t->acoustics_stop_sent=0;t->phase=M16_PF_DRAIN;
 }
 return 1;
}
int m16_physical_feature_init(m16_physical_feature_task *t,m16_shared_spectrum_task *s,m16_projection_runtime *r,mic16_physical_workspace *w){
 const void *p[6];uintptr_t bytes[6];unsigned i,j;int rc;uint64_t sa,se,pa,pe;
 if(!t || !s || !r || !w || (uintptr_t)t%_Alignof(m16_physical_feature_task) ||
  (uintptr_t)s%_Alignof(m16_shared_spectrum_task) || (uintptr_t)r%_Alignof(m16_projection_runtime) ||
  (uintptr_t)w%_Alignof(mic16_physical_workspace) || t->magic || !m16_shared_spectrum_task_available(s) ||
  !m16_projection_runtime_available(r) || !environment())return M16_PF_INVALID;
 p[0]=t;p[1]=s;p[2]=s->owner;p[3]=r;p[4]=r->arena;p[5]=w;
 bytes[0]=sizeof(*t);bytes[1]=sizeof(*s);bytes[2]=sizeof(*s->owner);bytes[3]=sizeof(*r);bytes[4]=(uintptr_t)r->words*8u;bytes[5]=sizeof(*w);
 for(i=0;i<6;i++)for(j=i+1;j<6;j++)if(!apart(p[i],bytes[i],p[j],bytes[j]))return M16_PF_INVALID;
 /* 两份宿主机指针分离不代表物理DDR地址分离；保护区也必须计入。 */
 sa=(uint64_t)s->owner->io.arena_address-64u;se=sa+M16_SHARED_ARENA_WORDS*8u+128u;
 pa=r->io.arena_address;pe=pa+(uint64_t)r->words*8u;
 if(sa<pe && pa<se)return M16_PF_INVALID;
 *t=(m16_physical_feature_task){0};
 rc=m16_physical_projection_init(&t->projection,r);if(rc)return rc;
 rc=m16_candidate_acoustics_init(&t->acoustics,s,w);if(rc)return rc;
 t->magic=MAGIC;t->spectrum=s;t->runtime=r;t->work=w;t->last_epoch=s->last_epoch;t->reserved_job_high=s->last_job;t->reserved_tag_high=r->tag_high;return 0;
}
int m16_physical_feature_begin(m16_physical_feature_task *t,const float *x,unsigned xn,const float *ref,unsigned rn,
 const double *d,unsigned count,const double *v,unsigned vn,float *out,unsigned on,const mic16_prepared_spectral *prepared,
 uint64_t epoch,uint64_t job,uint64_t tag){
 const void *io[5],*fixed[6];uintptr_t n[5],fn[6];unsigned i,j;
 if(!valid(t) || !x || !ref || !d || !v || !out || !prepared || count<1 || count>16 || xn<40960u*16u || rn<8192u*16u ||
  vn<480u*9u || on<count*20u*88u || (uintptr_t)x%4u || (uintptr_t)ref%4u || (uintptr_t)d%8u || (uintptr_t)v%8u || (uintptr_t)out%4u ||
  prepared->pcm!=x || prepared->reference!=ref || prepared->owner!=t->work || t->work->spectral.initialized!=0x4d313653u ||
  t->work->reference.count!=4u || t->work->reference.finalized!=1u || !epoch || !job || !tag || job>UINT64_MAX-(count*2u-1u) ||
  tag>UINT64_MAX-167u || tag<=t->reserved_tag_high || (!t->runtime->arbitrated&&tag<=t->runtime->tag_high) || tag<=t->projection.reserved_tag_high ||
  epoch<t->last_epoch || (epoch==t->last_epoch && job<=t->reserved_job_high) ||
  epoch<t->spectrum->last_epoch || (epoch==t->spectrum->last_epoch && job<=t->spectrum->last_job))return M16_PF_INVALID;
 if(!m16_physical_feature_releasable(t) || !m16_shared_spectrum_task_available(t->spectrum))return M16_PF_BUSY;
 if(!environment())return M16_PF_FLOAT_ENV;
 io[0]=x;io[1]=ref;io[2]=d;io[3]=v;io[4]=out;
 n[0]=40960u*16u*4u;n[1]=8192u*16u*4u;n[2]=count*16u;n[3]=480u*9u*8u;n[4]=count*20u*88u*4u;
 fixed[0]=t;fixed[1]=t->spectrum;fixed[2]=t->spectrum->owner;fixed[3]=t->runtime;fixed[4]=t->runtime->arena;fixed[5]=t->work;
 fn[0]=sizeof(*t);fn[1]=sizeof(*t->spectrum);fn[2]=sizeof(*t->spectrum->owner);fn[3]=sizeof(*t->runtime);fn[4]=(uintptr_t)t->runtime->words*8u;fn[5]=sizeof(*t->work);
 for(i=0;i<5;i++){
  for(j=0;j<6;j++)if(!apart(io[i],n[i],fixed[j],fn[j]))return M16_PF_INVALID;
  for(j=i+1;j<5;j++)if(!apart(io[i],n[i],io[j],n[j]))return M16_PF_INVALID;
 }
 for(i=0;i<count*2u;i++)if(!finite64(d[i]) || d[i]<=-90. || d[i]>=90.)return M16_PF_INPUT;
 for(i=0;i<count*2u;i++)t->directions[i]=d[i];
 t->pcm=x;t->reference=ref;t->votes=v;t->features=out;t->count=count;t->epoch=epoch;t->first_job=job;t->first_tag=tag;
 t->last_epoch=epoch;t->reserved_job_high=job+count*2u-1u;t->reserved_tag_high=tag+167u;
 t->phase=M16_PF_PREPARED;t->own_phase=M16_PF_ACOUSTICS;t->candidate=t->cursor=t->frame=t->local_count=t->frame_bits=t->band_bits=0;
 t->projection_begun=t->acoustics_begun=t->projection_done=t->projection_owned=t->projection_terminal=t->acoustics_terminal=0;
 t->error=0;t->steps=t->CPU_steps_while_projection=0;t->last_items=t->max_items=t->beam_leases=t->beam_releases=0;return 0;
}
/* 一侧失败时继续排空另一侧；只有本节点实际提交的空间事务才允许直接收取/归还。 */
static void drain_projection(m16_physical_feature_task *t){
 int rc;m16_projection_runtime *r=t->runtime;
 if(t->projection_terminal)return;
 if(t->projection.phase!=M16_PP_FAILED){
  if(!t->projection_stop_sent){
   t->projection_stop_sent=1;rc=m16_physical_projection_stop(&t->projection);
   if(rc==1){t->projection_owned=0;t->projection_terminal=1;}else if(rc<0 && !t->error)t->error=rc;
   return;
  }
  rc=m16_physical_projection_step(&t->projection);
  if(rc==2 || rc==0){t->projection_owned=0;t->projection_terminal=1;}else if(rc<0 && !t->error)t->error=rc;
  return;
 }
 if(!t->projection_owned || r->phase==M16_PR_QUARANTINE){t->projection_terminal=1;return;}
 if(r->phase==M16_PR_COMPLETE){
  rc=m16_projection_runtime_retire(r);if(rc && !t->error)t->error=rc;
  if(!rc)t->projection_owned=0;
  t->projection_terminal=1;return;
 }
 if(r->phase==M16_PR_PREPARE){
  rc=m16_projection_runtime_cancel(r);
  if(rc==1){t->projection_owned=0;t->projection_terminal=1;}else if(rc<0){if(!t->error)t->error=rc;t->projection_terminal=1;}
  return;
 }
 if(r->phase==M16_PR_IDLE){t->projection_owned=0;t->projection_terminal=1;return;}
 rc=m16_projection_runtime_poll(r);if(rc<0){if(!t->error)t->error=rc;t->projection_terminal=1;}
}
static int drain(m16_physical_feature_task *t){
 int rc;drain_projection(t);
 if(!t->acoustics_terminal){
  if(!t->acoustics_stop_sent){
   t->acoustics_stop_sent=1;rc=m16_candidate_acoustics_stop(&t->acoustics);
   if(rc==1)t->acoustics_terminal=1;else if(rc<0){if(!t->error)t->error=rc;t->acoustics_terminal=1;}
  }else{
   rc=m16_candidate_acoustics_step(&t->acoustics);
   if(rc!=1){t->acoustics_terminal=1;if(rc<0 && !t->error)t->error=rc;}
  }
 }
 if(!t->projection_terminal || !t->acoustics_terminal)return 1;
 t->phase=t->error?M16_PF_FAILED:M16_PF_STOPPED;return t->error?t->error:2;
}
static int own_step(m16_physical_feature_task *t){
 unsigned i,j,end;int rc;mic16_physical_workspace *w=t->work;float *o=t->features+t->candidate*20u*88u;
 if(t->own_phase==M16_PF_OWN_DONE)return 1;
 if(t->own_phase==M16_PF_BEAM_LEASE)return 1;
 if(t->own_phase==M16_PF_ACOUSTICS){
  rc=m16_candidate_acoustics_step(&t->acoustics);if(rc<0)return draining(t,rc);if(rc==1)return 1;
  if(rc==2)return draining(t,M16_PF_INPUT);
  t->cursor=0;t->own_phase=t->beam_hold_enabled?M16_PF_BEAM_LEASE:M16_PF_ANGLES;
  if(t->beam_hold_enabled)t->beam_leases++;
  return 1;
 }
 if(t->own_phase==M16_PF_ANGLES){
  end=t->cursor+8u;if(end>480u)end=480u;
  for(i=t->cursor;i<end;i++){
   const double *v=t->votes+i*9u;double a;
   if(!vote_valid(v,i))return draining(t,M16_PF_INPUT);
   a=mic16_spatial_angle(t->directions[t->candidate*2u],t->directions[t->candidate*2u+1u],v[4],v[5]);
   if(!finite64(a))return draining(t,M16_PF_OUTPUT);
   w->errors[i]=a;t->last_items++;
  }
  t->cursor=end;if(end==480u){t->cursor=t->frame=t->local_count=t->frame_bits=t->band_bits=0;t->own_phase=M16_PF_GATHER;}return 1;
 }
 if(t->own_phase==M16_PF_GATHER){
  end=t->cursor+4u;if(end>24u)end=24u;
  for(j=t->cursor;j<end;j++){
   const double *v;i=t->frame*24u+j;t->last_items++;
   if(w->errors[i]>10.)continue;
   v=t->votes+i*9u;if(!vote_valid(v,i))return draining(t,M16_PF_INPUT);
   t->coherence[t->local_count]=v[7]>0.?v[7]:0.;t->contrast[t->local_count]=clip(v[8],0.,1.);
   t->angles[t->local_count]=w->errors[i];t->local_count++;t->band_bits|=1u<<(unsigned)v[2];
  }
  t->cursor=end;if(end==24u)t->own_phase=M16_PF_LOCAL;return 1;
 }
 if(t->own_phase==M16_PF_LOCAL){
  unsigned frames=0,bands=0,n=t->local_count;
  if(n)t->frame_bits|=1u<<t->frame;
  for(i=0;i<20u;i++)frames+=(t->frame_bits>>i)&1u;
  for(i=0;i<3u;i++)bands+=(t->band_bits>>i)&1u;
  o[t->frame*88u+80u]=(float)((double)n/24.);o[t->frame*88u+81u]=(float)((double)frames/(t->frame+1u));
  o[t->frame*88u+82u]=(float)((double)bands/3.);
  o[t->frame*88u+83u]=n?(float)(sum64(t->coherence,n)/n):0.f;
  o[t->frame*88u+84u]=n?(float)(sum64(t->contrast,n)/n):0.f;
  o[t->frame*88u+85u]=n?(float)(sum64(t->angles,n)/n/10.):0.f;t->last_items=6;
  t->cursor=t->local_count=0;if(++t->frame==20u){t->frame=0;t->own_phase=M16_PF_COLLATE;}else t->own_phase=M16_PF_GATHER;return 1;
 }
 if(t->own_phase==M16_PF_COLLATE){
  unsigned f=t->frame;
  for(i=0;i<24u;i++){
   double a=w->levels[f*24u+i],b=w->reference.floor_db[i];if(!finite64(a) || !finite64(b))return draining(t,M16_PF_INPUT);
   o[f*88u+24u+i]=(float)(clip(a-b,-20.,40.)/20.);t->last_items++;
  }
  for(i=0;i<32u;i++){
   double b=w->bands[f*32u+i];if(!finite64(b))return draining(t,M16_PF_OUTPUT);
   o[f*88u+48u+i]=(float)(clip(b,-100.,80.)/40.);t->last_items++;
  }
  if(!finite64(w->beam_level[f]) || !finite64(t->acoustics.reference_floor))return draining(t,M16_PF_OUTPUT);
  o[f*88u+86u]=(float)(clip(w->beam_level[f]-t->acoustics.reference_floor,-20.,40.)/20.);
  o[f*88u+87u]=f==0?(float)((2048.-t->acoustics.guard)/2048.):1.f;t->last_items+=2;
  if(++t->frame==20u){
   if(++t->candidate==t->count){t->own_phase=M16_PF_OWN_DONE;return 1;}
   rc=m16_candidate_acoustics_begin(&t->acoustics,t->pcm,40960u*16u,t->reference,8192u*16u,t->directions+t->candidate*2u,
    t->epoch,t->first_job+t->candidate*2u);if(rc)return draining(t,rc);
   t->own_phase=M16_PF_ACOUSTICS;
  }return 1;
 }
 return draining(t,M16_PF_INVALID);
}
static int step(m16_physical_feature_task *t){
 unsigned i,end;int rc;
 if(t->phase==M16_PF_COMPLETE)return 0;
 if(t->phase==M16_PF_STOPPED)return 2;
 if(t->phase==M16_PF_FAILED)return t->error;
 if(t->phase==M16_PF_IDLE)return M16_PF_INVALID;
 if(t->phase==M16_PF_DRAIN)return drain(t);
 if(!environment())return draining(t,M16_PF_FLOAT_ENV);
 if(t->phase==M16_PF_PREPARED){
  end=t->cursor+64u;if(end>504u)end=504u;
  for(i=t->cursor;i<end;i++){
   double v=i<480u?t->work->levels[i]:t->work->reference.floor_db[i-480u];if(!finite64(v))return draining(t,M16_PF_INPUT);t->last_items++;
  }
  t->cursor=end;if(end==504u){t->cursor=0;t->phase=M16_PF_VOTES;}return 1;
 }
 if(t->phase==M16_PF_VOTES){
  end=t->cursor+7u;if(end>480u)end=480u;
  for(i=t->cursor;i<end;i++){if(!vote_valid(t->votes+i*9u,i))return draining(t,M16_PF_INPUT);t->last_items+=9;}
  t->cursor=end;if(end==480u){t->cursor=0;t->phase=M16_PF_PHAT;}return 1;
 }
 if(t->phase==M16_PF_PHAT){
  end=t->cursor+64u;if(end>M16_PP_PHAT_VALUES)end=M16_PP_PHAT_VALUES;
  for(i=t->cursor;i<end;i++){float v=t->work->phat[i];if(!(v>=-1.000001f && v<=1.000001f))return draining(t,M16_PF_INPUT);t->last_items++;}
  t->cursor=end;if(end==M16_PP_PHAT_VALUES){t->cursor=0;t->phase=M16_PF_LAUNCH;}return 1;
 }
 if(t->phase==M16_PF_LAUNCH){
  rc=m16_physical_projection_begin(&t->projection,t->work->phat,M16_PP_PHAT_VALUES,t->directions,t->count,t->features,t->count*20u*88u,t->epoch,t->first_tag);
  if(rc)return draining(t,rc);
  t->projection_begun=1;
  rc=m16_candidate_acoustics_begin(&t->acoustics,t->pcm,40960u*16u,t->reference,8192u*16u,t->directions,t->epoch,t->first_job);
  if(rc)return draining(t,rc);
  t->acoustics_begun=1;t->phase=M16_PF_RUN;return 1;
 }
 if(t->phase==M16_PF_RUN){
  if(!t->projection_done){
   unsigned before=t->projection.phase;
   rc=m16_physical_projection_step(&t->projection);
   if(m16_projection_runtime_client_released(t->runtime,&t->projection))t->projection_owned=0;
   else if(before==M16_PP_WAIT || before==M16_PP_SCATTER || t->projection.phase==M16_PP_WAIT || t->projection.phase==M16_PP_SCATTER)t->projection_owned=1;
   if(rc<0)return draining(t,rc);
   if(rc==2)return draining(t,M16_PF_INPUT);
   if(rc==0)t->projection_done=1;
  }
  if(t->runtime->phase==M16_PR_DEVICE && t->own_phase!=M16_PF_OWN_DONE && t->own_phase!=M16_PF_BEAM_LEASE)t->CPU_steps_while_projection++;
  rc=own_step(t);if(t->phase==M16_PF_DRAIN)return rc;
  if(t->projection_done && t->own_phase==M16_PF_OWN_DONE){t->cursor=0;t->phase=M16_PF_CHECK;}return 1;
 }
 if(t->phase==M16_PF_CHECK){
  end=t->cursor+64u;if(end>t->count*20u*88u)end=t->count*20u*88u;
  for(i=t->cursor;i<end;i++){if(!finite64(t->features[i]))return draining(t,M16_PF_OUTPUT);t->last_items++;}
  t->cursor=end;if(end==t->count*20u*88u){t->phase=M16_PF_COMPLETE;return 0;}return 1;
 }
 return draining(t,M16_PF_INVALID);
}
int m16_physical_feature_step(m16_physical_feature_task *t){
 int rc;if(!valid(t))return M16_PF_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_physical_feature_stop(m16_physical_feature_task *t){
 if(!valid(t))return M16_PF_INVALID;
 if(m16_physical_feature_releasable(t)){t->phase=M16_PF_STOPPED;return 1;}
 if(t->phase==M16_PF_FAILED)return t->error;
 (void)draining(t,0);
 if(t->projection_terminal && t->acoustics_terminal){t->phase=t->error?M16_PF_FAILED:M16_PF_STOPPED;return t->error?t->error:1;}
 return 0;
}
int m16_physical_feature_releasable(const m16_physical_feature_task *t){
 return valid(t) && (t->phase==M16_PF_IDLE || t->phase==M16_PF_COMPLETE || t->phase==M16_PF_STOPPED || t->phase==M16_PF_FAILED) &&
  !t->projection_owned && m16_projection_runtime_client_released(t->runtime,&t->projection) && m16_candidate_acoustics_releasable(&t->acoustics);
}
int m16_physical_feature_hold_beam(m16_physical_feature_task *t,unsigned enabled){
 if(!valid(t) || enabled>1u)return M16_PF_INVALID;
 if(!m16_physical_feature_releasable(t))return M16_PF_BUSY;
 t->beam_hold_enabled=enabled;return 0;
}
const float *m16_physical_feature_beam(const m16_physical_feature_task *t,uint64_t epoch,unsigned candidate){
 if(!valid(t) || !t->beam_hold_enabled || t->phase!=M16_PF_RUN || t->own_phase!=M16_PF_BEAM_LEASE ||
  t->epoch!=epoch || t->candidate!=candidate || candidate>=t->count)return 0;
 return t->work->beam;
}
int m16_physical_feature_release_beam(m16_physical_feature_task *t,uint64_t epoch,unsigned candidate){
 if(!m16_physical_feature_beam(t,epoch,candidate))return M16_PF_INVALID;
 t->beam_releases++;t->own_phase=M16_PF_ANGLES;return 0;
}
