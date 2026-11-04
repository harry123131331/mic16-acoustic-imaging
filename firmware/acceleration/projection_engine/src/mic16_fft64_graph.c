#include "mic16_fft64_graph.h"
#include <string.h>
#define MAGIC 0x4d464752u
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int request(void *p,const void *client,double *re,double *im,unsigned n,
 const double *wr,const double *wi,unsigned table,unsigned inverse){
 m16_fft64_graph *g=p;if(!g||g->magic!=MAGIC||!g->bound||g->error)return M16_F64_INVALID;
 return m16_fft64_request(&g->task,client,re,im,n,wr,wi,table,inverse);
}
static int poll(void *p,const void *client){return m16_fft64_poll(&((m16_fft64_graph *)p)->task,client);}
static int cancel(void *p,const void *client){return m16_fft64_cancel(&((m16_fft64_graph *)p)->task,client);}
static int owns(const void *p,const void *client){return m16_fft64_owns(&((const m16_fft64_graph *)p)->task,client);}
static int register_public(m16_fft64_graph *g,m16_public_spectrum_task *t){
 return m16_fft64_register_twiddles(&g->task,t->work->twiddle_re,t->work->twiddle_im,2048u,4096u);
}
static int register_complex(m16_fft64_graph *g,m16_complex_fft_task *t){
 return m16_fft64_register_twiddles(&g->task,t->twiddle_real,t->twiddle_imaginary,t->capacity,t->table_length);
}
int m16_fft64_graph_init(m16_fft64_graph *g,m16_projection_runtime *r,m16_detection_task *d,
 m16_enh_prediction_task *e,m16_continuous_task *c,m16_front_ring *f){
 const void *p[13];uintptr_t bytes[13];unsigned count=10u,i;int rc;
 if(!g||!r||!d||!e||!c||(uintptr_t)g%_Alignof(m16_fft64_graph)||g->magic||
  (M16_PROJECTION_VERSION!=5u&&M16_PROJECTION_VERSION!=6u)||M16_PROJECTION_BUILD_ABI!=M16_PROJECTION_EXPECTED_ABI||d->runtime!=r||
  !m16_projection_runtime_available(r)||!m16_detection_releasable(d)||
  !m16_enh_prediction_releasable(e)||!m16_continuous_task_releasable(c)||
  d->front.spectrum.provider||d->features.front.provider||d->features.physical.acoustics.mono.provider||
  e->weights.fft.provider||e->spectrum.fft.provider||c->fft.provider||
  (f&&(!m16_front_ring_releasable(f)||f->front.provider)))return M16_F64_INVALID;
 p[0]=r;bytes[0]=sizeof(*r);p[1]=r->arena;bytes[1]=(uintptr_t)r->words*8u;
 p[2]=d;bytes[2]=sizeof(*d);p[3]=d->work;bytes[3]=sizeof(*d->work);
 p[4]=e;bytes[4]=sizeof(*e);p[5]=e->work;bytes[5]=sizeof(*e->work);
 p[6]=c;bytes[6]=sizeof(*c);p[7]=c->work;bytes[7]=sizeof(*c->work);
 p[8]=d->statistics_storage;bytes[8]=sizeof(*d->statistics_storage);
 p[9]=d->spectrum;bytes[9]=sizeof(*d->spectrum);
 if(f){p[count]=f;bytes[count++]=sizeof(*f);p[count]=f->storage;bytes[count++]=sizeof(*f->storage);p[count]=f->work;bytes[count++]=sizeof(*f->work);}
 for(i=0;i<count;i++)if(!apart(g,sizeof(*g),p[i],bytes[i]))return M16_F64_INVALID;
 memset(g,0,sizeof(*g));g->magic=MAGIC;
 rc=m16_fft64_init(&g->task,r,M16_PROJECTION_BUILD_ABI);if(rc)goto failed;
 /* 所有表先登记，任何不支持的表在启动前暴露；运行时极值仍走原精确回退。 */
 rc=register_public(g,&d->front.spectrum);if(rc)goto failed;
 if(f){rc=register_public(g,&f->front);if(rc)goto failed;}
 rc=m16_fft64_register_twiddles(&g->task,d->features.physical.acoustics.mono.work->twiddle_re,
  d->features.physical.acoustics.mono.work->twiddle_im,2048u,4096u);if(rc)goto failed;
 rc=m16_fft64_register_twiddles(&g->task,d->features.front.twiddle_real,d->features.front.twiddle_imaginary,256u,512u);if(rc)goto failed;
 rc=register_complex(g,&e->weights.fft);if(rc)goto failed;
 rc=register_complex(g,&e->spectrum.fft);if(rc)goto failed;
 rc=register_complex(g,&c->fft);if(rc)goto failed;
 rc=m16_projection_runtime_enable_arbiter(r);if(rc)goto failed;
 g->provider=(m16_fft_provider){g,request,poll,cancel,owns};
 rc=m16_public_spectrum_bind(&d->front.spectrum,&g->provider);if(rc)goto failed;
 if(f){rc=m16_public_spectrum_bind(&f->front,&g->provider);if(rc)goto failed;}
 rc=m16_mono_feature_bind_fft64(&d->features.physical.acoustics.mono,&g->provider);if(rc)goto failed;
 rc=m16_statistics_front_bind_fft64(&d->features.front,&g->provider);if(rc)goto failed;
 rc=m16_complex_fft_bind(&e->weights.fft,&g->provider);if(rc)goto failed;
 rc=m16_complex_fft_bind(&e->spectrum.fft,&g->provider);if(rc)goto failed;
 rc=m16_complex_fft_bind(&c->fft,&g->provider);if(rc)goto failed;
 g->clients=f?7u:6u;g->bound=1;return 0;
failed:
 g->error=rc<0?rc:M16_F64_INVALID;return g->error;
}
int m16_fft64_graph_service(void *context){
 m16_fft64_graph *g=context;int rc;
 if(!g||g->magic!=MAGIC||!g->bound)return M16_F64_INVALID;
 if(g->service_steps!=UINT64_MAX)g->service_steps++;
 rc=m16_fft64_service(&g->task);if(rc<0&&!g->error)g->error=rc;
 return g->error?g->error:rc;
}
