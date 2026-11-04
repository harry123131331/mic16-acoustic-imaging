#include "mic16_feature_pipeline_task.h"
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif
#define MAGIC 0x4d463133u
static int valid(const m16_feature_pipeline_task *t){return t && !((uintptr_t)t%_Alignof(m16_feature_pipeline_task)) && t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a && b && x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
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
static int draining(m16_feature_pipeline_task *t,int error){
 if(!t->error)t->error=error;
 if(t->phase!=M16_FP_DRAIN){
  /* 先取消统计前端读者；共享FFT由常驻服务排空后才允许归还工作区。 */
  if(!m16_statistics_front_releasable(&t->front))(void)m16_statistics_front_stop(&t->front);
  t->physical_terminal=!t->physical_started;t->statistics_terminal=!t->statistics_started;
  t->physical_stop_sent=t->statistics_stop_sent=0;t->phase=M16_FP_DRAIN;
 }
 return 1;
}
int m16_feature_pipeline_init(m16_feature_pipeline_task *t,m16_shared_spectrum_task *s,m16_projection_runtime *r,
 mic16_extended_workspace *w,m16_statistics_projection_storage *storage){
 const void *p[7];uintptr_t n[7];unsigned i,j;int rc;
 if(!t || !s || !r || !w || !storage || (uintptr_t)t%_Alignof(m16_feature_pipeline_task) ||
  (uintptr_t)s%_Alignof(m16_shared_spectrum_task) || (uintptr_t)r%_Alignof(m16_projection_runtime) ||
  (uintptr_t)w%_Alignof(mic16_extended_workspace) || (uintptr_t)storage%_Alignof(m16_statistics_projection_storage) ||
  t->magic || !m16_shared_spectrum_task_available(s) || !m16_projection_runtime_available(r) || !environment())return M16_FP_INVALID;
 p[0]=t;p[1]=s;p[2]=s->owner;p[3]=r;p[4]=r->arena;p[5]=w;p[6]=storage;
 n[0]=sizeof(*t);n[1]=sizeof(*s);n[2]=sizeof(*s->owner);n[3]=sizeof(*r);n[4]=(uintptr_t)r->words*8u;n[5]=sizeof(*w);n[6]=sizeof(*storage);
 for(i=0;i<7;i++)for(j=i+1;j<7;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_FP_INVALID;
 *t=(m16_feature_pipeline_task){0};
 rc=m16_physical_feature_init(&t->physical,s,r,&w->physical);if(rc)return rc;
 rc=m16_physical_feature_hold_beam(&t->physical,1);if(rc)return rc;
 rc=m16_statistics_front_init(&t->front,w);if(rc)return rc;
 rc=m16_statistics_projection_init(&t->statistics,r,w,storage);if(rc)return rc;
 t->magic=MAGIC;t->work=w;t->storage=storage;t->spectrum=s;t->runtime=r;
 t->last_epoch=s->last_epoch;t->reserved_job_high=s->last_job;t->reserved_tag_high=r->tag_high;return 0;
}
int m16_feature_pipeline_begin(m16_feature_pipeline_task *t,const float *x,unsigned xn,const float *q,unsigned qn,
 const double *d,unsigned count,const double *v,unsigned vn,float *out,unsigned on,double *model,unsigned char *bits,
 const mic16_prepared_spectral *prepared,uint64_t epoch,uint64_t job,uint64_t tag){
 const void *p[7],*fixed[7];uintptr_t n[7],fn[7];unsigned i,j;int rc;
 if(!valid(t) || !x || !q || !d || !v || !out || !model || !bits || !prepared || count<1 || count>16 ||
  xn<40960u*16u || qn<8192u*16u || vn<480u*9u || on<16u*20u*130u ||
  (uintptr_t)x%4u || (uintptr_t)q%4u || (uintptr_t)d%8u || (uintptr_t)v%8u || (uintptr_t)out%4u || (uintptr_t)model%8u ||
  prepared->pcm!=x || prepared->reference!=q || prepared->owner!=&t->work->physical ||
  t->work->physical.spectral.initialized!=0x4d313653u || t->work->physical.reference.count!=4u || t->work->physical.reference.finalized!=1u ||
  !epoch || epoch<t->last_epoch || !job || job>UINT64_MAX-(count*2u-1u) ||
  (epoch==t->last_epoch && job<=t->reserved_job_high) || !tag || tag>UINT64_MAX-303u || tag<=t->reserved_tag_high ||
  (!t->runtime->arbitrated&&tag<=t->runtime->tag_high) || tag<=t->physical.reserved_tag_high || tag<=t->statistics.reserved_tag_high)return M16_FP_INVALID;
 if(!m16_feature_pipeline_releasable(t))return M16_FP_BUSY;
 if(!environment())return M16_FP_FLOAT_ENV;
 p[0]=x;p[1]=q;p[2]=d;p[3]=v;p[4]=out;p[5]=model;p[6]=bits;
 n[0]=40960u*16u*4u;n[1]=8192u*16u*4u;n[2]=count*16u;n[3]=480u*9u*8u;n[4]=16u*20u*130u*4u;n[5]=32u*8u;n[6]=16u;
 fixed[0]=t;fixed[1]=t->spectrum;fixed[2]=t->spectrum->owner;fixed[3]=t->runtime;fixed[4]=t->runtime->arena;fixed[5]=t->work;fixed[6]=t->storage;
 fn[0]=sizeof(*t);fn[1]=sizeof(*t->spectrum);fn[2]=sizeof(*t->spectrum->owner);fn[3]=sizeof(*t->runtime);fn[4]=(uintptr_t)t->runtime->words*8u;fn[5]=sizeof(*t->work);fn[6]=sizeof(*t->storage);
 for(i=0;i<7;i++){
  for(j=0;j<7;j++)if(!apart(p[i],n[i],fixed[j],fn[j]))return M16_FP_INVALID;
  for(j=i+1;j<7;j++)if(!apart(p[i],n[i],p[j],n[j]))return M16_FP_INVALID;
 }
 for(i=0;i<count*2u;i++)if(!finite64(d[i]) || d[i]<=-90. || d[i]>=90.)return M16_FP_INPUT;
 for(i=0;i<count*2u;i++)t->directions[i]=d[i];
 t->pcm=x;t->reference=q;t->votes=v;t->output=out;t->model_directions=model;t->valid_candidates=bits;t->count=count;
 t->epoch=t->last_epoch=epoch;t->first_job=job;t->first_tag=tag;t->reserved_job_high=job+count*2u-1u;t->reserved_tag_high=tag+303u;
 t->phase=M16_FP_QUIET;t->error=0;t->steps=0;t->last_items=t->max_items=t->cursor=t->beam_running=t->beam_completed=0;
 t->physical_started=t->statistics_started=0;
 rc=m16_statistics_front_begin(&t->front,M16_SF_QUIET16,q,8192u*16u,0);
 if(rc){t->phase=M16_FP_FAILED;t->error=rc;return rc;}
 rc=m16_physical_feature_begin(&t->physical,x,xn,q,qn,t->directions,count,v,vn,t->work->physical88,count*20u*88u,prepared,epoch,job,tag);
 if(rc){(void)m16_statistics_front_stop(&t->front);t->phase=M16_FP_FAILED;t->error=rc;return rc;}
 t->physical_started=1;return 0;
}
static int drain(m16_feature_pipeline_task *t){
 int rc;
 if(!t->physical_terminal){
  if(!t->physical_stop_sent){t->physical_stop_sent=1;rc=m16_physical_feature_stop(&t->physical);}
  else rc=m16_physical_feature_step(&t->physical);
  if(rc!=0 && rc!=1)t->physical_terminal=1;
  if(rc==0 && m16_physical_feature_releasable(&t->physical))t->physical_terminal=1;
  if(rc==1 && m16_physical_feature_releasable(&t->physical))t->physical_terminal=1;
  if(rc<0 && !t->error)t->error=rc;
 }
 if(!t->statistics_terminal){
  if(!t->statistics_stop_sent){t->statistics_stop_sent=1;rc=m16_statistics_projection_stop(&t->statistics);}
  else rc=m16_statistics_projection_step(&t->statistics);
  if(rc!=0 && rc!=1)t->statistics_terminal=1;
  if((rc==0 || rc==1) && m16_statistics_projection_releasable(&t->statistics))t->statistics_terminal=1;
  if(rc<0 && !t->error)t->error=rc;
 }
 if(!t->physical_terminal || !t->statistics_terminal || !m16_statistics_front_releasable(&t->front))return 1;
 t->phase=t->error?M16_FP_FAILED:M16_FP_STOPPED;return t->error?t->error:2;
}
static int step(m16_feature_pipeline_task *t){
 unsigned i,end;int rc;mic16_extended_workspace *w=t->work;
 if(t->phase==M16_FP_COMPLETE)return 0;
 if(t->phase==M16_FP_STOPPED)return 2;
 if(t->phase==M16_FP_FAILED)return t->error;
 if(t->phase==M16_FP_IDLE)return M16_FP_INVALID;
 if(t->phase==M16_FP_DRAIN)return drain(t);
 if(!environment())return draining(t,M16_FP_FLOAT_ENV);
 if(t->phase==M16_FP_QUIET || t->phase==M16_FP_OBSERVED){
  rc=m16_statistics_front_step(&t->front);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_FP_INPUT);
  if(t->phase==M16_FP_QUIET){
   rc=m16_statistics_front_begin(&t->front,M16_SF_OBSERVED16,t->pcm,40960u*16u,0);if(rc)return draining(t,rc);
   t->phase=M16_FP_OBSERVED;
  }else t->phase=M16_FP_PHYSICAL;
  return 1;
 }
 if(t->phase==M16_FP_PHYSICAL){
  rc=m16_physical_feature_step(&t->physical);if(rc<0)return draining(t,rc);if(rc==2)return draining(t,M16_FP_INPUT);
  if(t->beam_running){
   int sr=m16_statistics_front_step(&t->front);if(sr<0)return draining(t,sr);if(sr==2)return draining(t,M16_FP_INPUT);
   if(sr==0){
    sr=m16_physical_feature_release_beam(&t->physical,t->epoch,t->beam_completed);if(sr)return draining(t,sr);
    t->beam_running=0;t->beam_completed++;
   }
  }else if(t->beam_completed<t->count){
   const float *beam=m16_physical_feature_beam(&t->physical,t->epoch,t->beam_completed);
   if(beam){int sr=m16_statistics_front_begin(&t->front,M16_SF_BEAM1,beam,40960,t->beam_completed);if(sr)return draining(t,sr);t->beam_running=1;}
  }
  if(rc==0){
   if(t->beam_running || t->beam_completed!=t->count || !m16_physical_feature_releasable(&t->physical))return draining(t,M16_FP_OWNERSHIP);
   rc=m16_statistics_projection_begin(&t->statistics,t->directions,t->count,w->statistics18,t->count*360u,t->epoch,t->first_tag+168u);
   if(rc)return draining(t,rc);
   t->statistics_started=1;t->phase=M16_FP_STATISTICS;
  }return 1;
 }
 if(t->phase==M16_FP_STATISTICS){
  rc=m16_statistics_projection_step(&t->statistics);if(rc<0)return draining(t,rc);if(rc==1)return 1;if(rc==2)return draining(t,M16_FP_INPUT);
  t->cursor=0;t->phase=M16_FP_MODEL;return 1;
 }
 if(t->phase==M16_FP_MODEL){
  i=t->cursor;t->valid_candidates[i]=(unsigned char)(i<t->count);
  t->model_directions[i*2u]=i<t->count?(double)(float)t->directions[i*2u]:0.;
  t->model_directions[i*2u+1u]=i<t->count?(double)(float)t->directions[i*2u+1u]:0.;
  t->last_items=3;if(++t->cursor==16u){t->cursor=0;t->phase=M16_FP_COLLATE106;}return 1;
 }
 if(t->phase==M16_FP_COLLATE106){
  end=t->cursor+64u;if(end>16u*20u*106u)end=16u*20u*106u;
  for(i=t->cursor;i<end;i++){
   unsigned c=i/(20u*106u),f=i%106u,frame=(i/106u)%20u;float value=0.f;
   if(c<t->count)value=f<88u?w->physical88[(c*20u+frame)*88u+f]:w->statistics18[(c*20u+frame)*18u+f-88u];
   if(!finite64(value))return draining(t,M16_FP_OUTPUT_ERROR);
   w->features106[i]=value;t->last_items++;
  }
  t->cursor=end;if(end==16u*20u*106u){t->cursor=0;t->phase=M16_FP_UNITS;}return 1;
 }
 if(t->phase==M16_FP_UNITS){
  double a=t->model_directions[t->cursor*2u]*0.017453292519943295,e=t->model_directions[t->cursor*2u+1u]*0.017453292519943295;
  double *u=t->units+t->cursor*3u;
  if(t->valid_candidates[t->cursor]>1u || !finite64(a) || !finite64(e) ||
   t->model_directions[t->cursor*2u]<=-90. || t->model_directions[t->cursor*2u]>=90. ||
   t->model_directions[t->cursor*2u+1u]<=-90. || t->model_directions[t->cursor*2u+1u]>=90.)return draining(t,M16_FP_INPUT);
  u[0]=cos(e)*sin(a);u[1]=sin(e);u[2]=cos(e)*cos(a);
  t->last_items=3;if(++t->cursor==16u){t->cursor=0;t->phase=M16_FP_PEERS;}return 1;
 }
 if(t->phase==M16_FP_PEERS){
  end=t->cursor+4u;if(end>256u)end=256u;
  for(i=t->cursor;i<end;i++){
   unsigned c=i/16u,j=i%16u;const double *a=t->units+c*3u,*b=t->units+j*3u;
   double dot=fma(a[2],b[2],fma(a[1],b[1],a[0]*b[0]));
   double angle=acos(dot>1.?1.:(dot< -1.?-1.:dot))*57.295779513082323;
   w->peers[i]=(unsigned char)(t->valid_candidates[c] && t->valid_candidates[j] && angle>15.);t->last_items++;
  }
  t->cursor=end;if(end==256u){t->cursor=0;t->phase=M16_FP_CONTRAST;}return 1;
 }
 if(t->phase==M16_FP_CONTRAST){
  end=t->cursor+4u;if(end>16u*20u*24u)end=16u*20u*24u;
  for(i=t->cursor;i<end;i++){
   unsigned c=i/(20u*24u),frame=(i/24u)%20u,z=i%24u,j;float best=-3.402823466e38f;int exists=0;
   for(j=0;j<16u;j++){
    if(w->peers[c*16u+j]){float value=w->features106[(j*20u+frame)*106u+z];if(!exists || value>best)best=value;exists=1;}
    t->last_items++;
   }
   w->contrasts24[i]=exists?w->features106[(c*20u+frame)*106u+z]-best:0.f;
  }
  t->cursor=end;if(end==16u*20u*24u){t->cursor=0;t->phase=M16_FP_OUTPUT;}return 1;
 }
 if(t->phase==M16_FP_OUTPUT){
  end=t->cursor+64u;if(end>16u*20u*130u)end=16u*20u*130u;
  for(i=t->cursor;i<end;i++){
   unsigned row=i/130u,f=i%130u;float value=f<106u?w->features106[row*106u+f]:w->contrasts24[row*24u+f-106u];
   if(!finite64(value))return draining(t,M16_FP_OUTPUT_ERROR);
   t->output[i]=value;t->last_items++;
  }
  t->cursor=end;if(end==16u*20u*130u){t->phase=M16_FP_COMPLETE;return 0;}return 1;
 }
 return draining(t,M16_FP_INVALID);
}
int m16_feature_pipeline_step(m16_feature_pipeline_task *t){
 int rc;if(!valid(t))return M16_FP_INVALID;t->last_items=0;t->steps++;rc=step(t);
 if(t->last_items>t->max_items)t->max_items=t->last_items;
 return rc;
}
int m16_feature_pipeline_releasable(const m16_feature_pipeline_task *t){
 return valid(t) && (t->phase==M16_FP_IDLE || t->phase==M16_FP_COMPLETE || t->phase==M16_FP_STOPPED || t->phase==M16_FP_FAILED) &&
  m16_statistics_front_releasable(&t->front) && m16_physical_feature_releasable(&t->physical) && m16_statistics_projection_releasable(&t->statistics);
}
int m16_feature_pipeline_stop(m16_feature_pipeline_task *t){
 if(!valid(t))return M16_FP_INVALID;
 if(m16_feature_pipeline_releasable(t)){t->phase=M16_FP_STOPPED;return 1;}
 if(t->phase==M16_FP_FAILED)return t->error;
 (void)draining(t,0);return 0;
}
