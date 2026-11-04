#include "mic16_front_context.h"
#include <string.h>
#define MAGIC 0x4d464354u
static int valid(const m16_front_context_task *t){return t&&t->magic==MAGIC;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int release_reader(m16_front_context_task *t){
 int rc;if(!t->reader)return 0;rc=m16_front_ring_release(t->ring,&t->lease);if(!rc)t->reader=0;return rc;
}
int m16_front_context_init(m16_front_context_task *t,m16_front_ring *r,m16_detection_task *d,float *pcm,unsigned n){
 const void *p[13];uintptr_t z[13];unsigned count=0;
 if(!t||!r||!d||!pcm||t->magic||(uintptr_t)t%_Alignof(m16_front_context_task)||(uintptr_t)pcm%4u||
  n!=40960u*16u||!m16_detection_releasable(d)||!r->storage||!r->work||
  (r->evidence_enabled&&d->front.evidence_cache!=&r->evidence.view))return M16_FC_INVALID;
 p[count]=t;z[count++]=sizeof(*t);p[count]=r;z[count++]=sizeof(*r);
 p[count]=r->storage;z[count++]=sizeof(*r->storage);p[count]=r->work;z[count++]=sizeof(*r->work);
 p[count]=d;z[count++]=sizeof(*d);p[count]=d->work;z[count++]=sizeof(*d->work);
 p[count]=d->coarse;z[count++]=sizeof(*d->coarse);p[count]=d->runtime;z[count++]=sizeof(*d->runtime);
 p[count]=d->runtime->arena;z[count++]=(uintptr_t)d->runtime->words*8u;
 p[count]=d->statistics_storage;z[count++]=sizeof(*d->statistics_storage);
 p[count]=d->spectrum;z[count++]=sizeof(*d->spectrum);
 p[count]=d->spectrum->owner;z[count++]=sizeof(*d->spectrum->owner);p[count]=pcm;z[count++]=(uintptr_t)n*4u;
 for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)if(!apart(p[i],z[i],p[j],z[j]))return M16_FC_INVALID;
 *t=(m16_front_context_task){0};t->magic=MAGIC;t->ring=r;t->detector=d;t->pcm=pcm;return 0;
}
int m16_front_context_begin(m16_front_context_task *t){
 int rc;if(!valid(t))return M16_FC_INVALID;
 if(t->phase!=M16_FC_IDLE||t->detector->front_context||!m16_detection_releasable(t->detector))return M16_FC_BUSY;
 rc=m16_front_ring_acquire(t->ring,&t->lease);if(rc)return rc;
 t->reader=1;
 if(t->lease.first_frame%20u||t->lease.first_frame>UINT64_MAX/2048u){
  rc=release_reader(t);return rc?rc:M16_FC_SEQUENCE;
 }
 t->epoch=t->lease.epoch;t->first_sample=t->lease.first_frame*2048u;t->generation=t->lease.generation;
 t->frame=t->region=t->cursor=t->last_bytes=t->max_bytes=t->submitted=0;t->error=0;t->bytes_copied=t->steps=0;
 t->downstream_owner=0;t->downstream_releasable=0;t->detector->front_context=t;t->phase=M16_FC_COPY;return 0;
}
int m16_front_context_step(m16_front_context_task *t){
 const m16_front_frame_metadata *meta;const float *pcm;const mic16_spectral_frame *s;
 const void *source;void *destination;unsigned values,element,n;int rc;
 if(!valid(t))return M16_FC_INVALID;
 if(t->phase==M16_FC_READY)return 0;
 if(t->phase!=M16_FC_COPY||t->detector->front_context!=t||!t->reader)return M16_FC_LEASE;
 t->steps++;t->last_bytes=0;
 rc=m16_front_ring_frame(t->ring,&t->lease,t->frame,&meta,&pcm,&s);if(rc){t->error=rc;return rc;}
 if(meta->epoch!=t->epoch||meta->first_sample!=t->first_sample+t->frame*2048u)return M16_FC_SEQUENCE;
 if(t->region==0){source=pcm;destination=t->pcm+t->frame*32768u;values=32768u;element=4;}
 else if(t->region==1){source=s->phat;destination=t->detector->work->extended.physical.phat+t->frame*51648u;values=51648u;element=4;}
 else if(t->region==2){source=s->zone_level_db;destination=t->detector->work->extended.physical.levels+t->frame*24u;values=24u;element=8;}
 else{
  const float *maps;rc=m16_spatial_evidence_coarse(&t->ring->evidence,t->epoch,t->lease.first_frame+t->frame,&maps);if(rc)return rc;
  source=maps;destination=t->detector->work->maps+t->frame*24u*325u;values=24u*325u;element=4;
 }
 n=values-t->cursor;if(n>1024u/element)n=1024u/element;
 memcpy((unsigned char *)destination+t->cursor*element,(const unsigned char *)source+t->cursor*element,n*element);
 t->last_bytes=n*element;t->bytes_copied+=t->last_bytes;if(t->last_bytes>t->max_bytes)t->max_bytes=t->last_bytes;
 t->cursor+=n;
 if(t->cursor==values){t->cursor=0;if(++t->region==(t->ring->evidence_enabled?4u:3u)){t->region=0;if(++t->frame==20u){
  rc=release_reader(t);if(rc){t->error=rc;return rc;}t->phase=M16_FC_READY;return 0;
 }}}
 return 1;
}
int m16_front_context_retire(m16_front_context_task *t){
 if(!valid(t)||t->detector->front_context!=t||t->reader)return M16_FC_LEASE;
 if(t->phase!=M16_FC_IN_USE&&t->phase!=M16_FC_STOPPING)return M16_FC_BUSY;
 if(!m16_detection_prepared_releasable(t->detector,t))return M16_FC_BUSY;
 if(t->downstream_owner&&(!t->downstream_releasable||!t->downstream_releasable(t->downstream_owner)))return M16_FC_BUSY;
 t->detector->front_context=0;t->phase=M16_FC_IDLE;t->submitted=0;t->downstream_owner=0;t->downstream_releasable=0;return 0;
}
int m16_front_context_stop(m16_front_context_task *t){
 int rc;if(!valid(t))return M16_FC_INVALID;
 if(t->phase==M16_FC_IDLE)return 1;
 if(t->detector->front_context!=t)return M16_FC_LEASE;
 if(!t->submitted){
  rc=release_reader(t);if(rc)return rc;t->detector->front_context=0;t->phase=M16_FC_IDLE;return 1;
 }
 t->phase=M16_FC_STOPPING;rc=m16_detection_stop(t->detector);if(rc<0)return rc;
 if(!m16_detection_prepared_releasable(t->detector,t))return 0;
 if(t->downstream_owner&&(!t->downstream_releasable||!t->downstream_releasable(t->downstream_owner)))return 0;
 rc=m16_front_context_retire(t);return rc?rc:1;
}
