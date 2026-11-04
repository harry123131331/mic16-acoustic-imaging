#include "mic16_shared_compute_graph.h"
#include <string.h>
#define MAGIC 0x4d534347u
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int request(void *context,const void *client,const m16_fir_request *q){
 m16_shared_compute_graph *g=context;
 if(!g||g->magic!=MAGIC||!g->bound||g->error)return M16_FIR_INVALID;
 return m16_fir_task_request(&g->fir,client,q);
}
static int poll(void *context,const void *client){return m16_fir_task_poll(&((m16_shared_compute_graph *)context)->fir,client);}
static int cancel(void *context,const void *client){return m16_fir_task_cancel(&((m16_shared_compute_graph *)context)->fir,client);}
static int owns(const void *context,const void *client){return m16_fir_task_owns(&((const m16_shared_compute_graph *)context)->fir,client);}
int m16_shared_compute_graph_init(m16_shared_compute_graph *g,m16_projection_runtime *r,m16_detection_task *d,
 m16_enh_prediction_task *e,m16_continuous_task *c,m16_front_ring *f){
 const void *p[13];uintptr_t size[13];unsigned count=10u,i;int rc;
 if(!g||!r||!d||!e||!c||(uintptr_t)g%_Alignof(m16_shared_compute_graph)||g->magic||
  M16_PROJECTION_VERSION!=6u||M16_PROJECTION_BUILD_ABI!=2026102806u||
  d->features.front.fir_provider||e->spectrum.resample.provider||c->resample.provider)return M16_FIR_INVALID;
 p[0]=r;size[0]=sizeof(*r);p[1]=r->arena;size[1]=(uintptr_t)r->words*8u;
 p[2]=d;size[2]=sizeof(*d);p[3]=d->work;size[3]=sizeof(*d->work);
 p[4]=e;size[4]=sizeof(*e);p[5]=e->work;size[5]=sizeof(*e->work);
 p[6]=c;size[6]=sizeof(*c);p[7]=c->work;size[7]=sizeof(*c->work);
 p[8]=d->statistics_storage;size[8]=sizeof(*d->statistics_storage);p[9]=d->spectrum;size[9]=sizeof(*d->spectrum);
 if(f){p[count]=f;size[count++]=sizeof(*f);p[count]=f->storage;size[count++]=sizeof(*f->storage);p[count]=f->work;size[count++]=sizeof(*f->work);}
 for(i=0;i<count;i++)if(!apart(g,sizeof(*g),p[i],size[i]))return M16_FIR_INVALID;
 memset(g,0,sizeof(*g));g->magic=MAGIC;
 rc=m16_fft64_graph_init(&g->fft,r,d,e,c,f);if(rc)goto failed;
 rc=m16_fir_task_init(&g->fir,r,M16_PROJECTION_BUILD_ABI,1u);if(rc)goto failed;
 g->fir_provider=(m16_fir_provider){g,request,poll,cancel,owns};
 rc=m16_statistics_front_bind_fir(&d->features.front,&g->fir_provider);if(rc)goto failed;
 rc=m16_resample_task_bind(&e->spectrum.resample,&g->fir_provider);if(rc)goto failed;
 rc=m16_resample_task_bind(&c->resample,&g->fir_provider);if(rc)goto failed;
 g->fir_clients=3;g->bound=1;return 0;
failed:
 g->error=rc<0?rc:M16_FIR_INVALID;return g->error;
}
int m16_shared_compute_graph_service(void *context){
 m16_shared_compute_graph *g=context;int fft_rc,fir_rc;
 if(!g||g->magic!=MAGIC||!g->bound)return M16_FIR_INVALID;
 if(g->service_steps!=UINT64_MAX)g->service_steps++;
 /* 任一支路报错仍服务另一支路，使已提交任务有机会排空；不再接收新请求。 */
 fft_rc=m16_fft64_graph_service(&g->fft);
 fir_rc=m16_fir_task_service(&g->fir);
 if(!g->error){if(fft_rc<0)g->error=fft_rc;else if(fir_rc<0)g->error=fir_rc;}
 if(g->error)return g->error;
 return fft_rc||fir_rc?1:0;
}
