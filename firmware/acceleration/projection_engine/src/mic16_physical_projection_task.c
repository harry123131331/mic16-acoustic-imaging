#include "mic16_physical_projection_task.h"
#include <string.h>
#include <math.h>
#if !defined(__arm__)
#include <fenv.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#endif

#define MAGIC 0x4d505054u
#define PI 3.14159265358979323846264338327950288
/* 调用冻结几何函数，不另写近似几何或改用网络方向。 */
extern int mic16_candidate_geometry(const double *,float *,float *);
extern int mic16_spatial_signature(unsigned,unsigned,unsigned,float *);

static int overlap(const void *a,uintptr_t an,const void *b,uintptr_t bn){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static int valid(const m16_physical_projection_task *t){return t&&t->magic==MAGIC;}
static int finite64(double x){return x==x&&x<=1.7976931348623157e308&&x>=-1.7976931348623157e308;}
static int float_environment(void){
#if defined(__arm__)
    uint32_t fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr));
    return !(fpscr&((3u<<22)|(1u<<24)));
#else
    if(fegetround()!=FE_TONEAREST)return 0;
#if defined(__SSE__)
    if(_mm_getcsr()&((3u<<13)|(1u<<15)|(1u<<6)))return 0;
#endif
    return 1;
#endif
}
static int fail(m16_physical_projection_task *t,int rc){
    if(t->runtime->arbitrated)(void)m16_projection_runtime_unclaim(t->runtime,t);
    t->reuse_valid=0;t->error=rc;t->phase=M16_PP_FAILED;return rc;
}
static uint64_t packed(float real,float imaginary){
    uint32_t a,b;memcpy(&a,&real,4);memcpy(&b,&imaginary,4);return a|((uint64_t)b<<32);
}
static unsigned aligned(unsigned n){return (n+7u)&~7u;}
static void band_layout(m16_physical_projection_task *t){
    unsigned start=t->zone*34u;
    t->bins=807u-start;if(t->bins>34u)t->bins=34u;
    t->obs=64u;t->coef=aligned(t->obs+t->active_count*32u*t->bins);
    t->out=aligned(t->coef+32u*t->bins*t->count);
    t->receipts=aligned(t->out+t->active_count*t->count);
    t->used_words=aligned(t->receipts+14u);t->cursor=0;
}
static int buffers_overlap(const m16_physical_projection_task *t,const void *a,uintptr_t n){
    return overlap(a,n,t,sizeof(*t))||overlap(a,n,t->runtime,sizeof(*t->runtime))||
           overlap(a,n,t->runtime->arena,(uintptr_t)t->runtime->words*8u);
}
int m16_physical_projection_init(m16_physical_projection_task *t,m16_projection_runtime *r){
    if(!t||!r||((uintptr_t)t%_Alignof(m16_physical_projection_task))||t->magic||
       !m16_projection_runtime_available(r)||r->words<M16_PP_ARENA_WORDS||
       overlap(t,sizeof(*t),r,sizeof(*r))||overlap(t,sizeof(*t),r->arena,(uintptr_t)r->words*8u))return M16_PP_INVALID;
    if(!float_environment())return M16_PP_FLOAT_ENV;
    memset(t,0,sizeof(*t));t->magic=MAGIC;t->runtime=r;t->reserved_tag_high=r->tag_high;return 0;
}
int m16_physical_projection_bind_signatures(m16_physical_projection_task *t,m16_pp_signature_provider provider,void *context){
    if(!valid(t)||!provider||t->phase!=M16_PP_IDLE||t->reuse_active||t->cache_locked||!m16_projection_runtime_available(t->runtime))return M16_PP_INVALID;
    t->signature_provider=provider;t->signature_context=context;return 0;
}
int m16_physical_projection_bind_rows(m16_physical_projection_task *t,m16_pp_row_provider provider,void *context){
    if(!valid(t)||!provider||t->phase!=M16_PP_IDLE||t->reuse_active||t->cache_locked||!m16_projection_runtime_available(t->runtime))return M16_PP_INVALID;
    t->row_provider=provider;t->row_context=context;t->row_valid=0;return 0;
}
void m16_physical_projection_reuse_end(m16_physical_projection_task *t){
    if(valid(t)){t->reuse_active=t->reuse_valid=0;t->reuse_phat=0;t->reuse_scope=0;t->grid_cache=0;}
}
int m16_physical_projection_reuse_begin(m16_physical_projection_task *t,const float *phat,unsigned nv,uint64_t epoch,uint64_t scope){
    if(!valid(t)||!phat||((uintptr_t)phat%_Alignof(float))||nv<M16_PP_PHAT_VALUES||!epoch||!scope||
       scope<=t->reserved_tag_high||(!t->runtime->arbitrated&&scope<=t->runtime->tag_high)||buffers_overlap(t,phat,(uintptr_t)M16_PP_PHAT_VALUES*4u))return M16_PP_INVALID;
    if(t->reuse_active||!m16_projection_runtime_schedulable(t->runtime)||
       (t->phase!=M16_PP_IDLE&&t->phase!=M16_PP_COMPLETE&&t->phase!=M16_PP_STOPPED&&t->phase!=M16_PP_FAILED))return M16_PP_BUSY;
    t->reuse_active=1;t->reuse_valid=0;t->reuse_phat=phat;t->reuse_epoch=epoch;t->reuse_scope=scope;
    t->observation_words=t->observation_reuses=0;return 0;
}
static int begin_common(m16_physical_projection_task *t,const float *phat,unsigned nv,
    unsigned count,float *features,unsigned nf,unsigned needed,uint64_t epoch,uint64_t tag){
    if(!valid(t))return M16_PP_INVALID;
    if((t->phase!=M16_PP_IDLE&&t->phase!=M16_PP_COMPLETE&&t->phase!=M16_PP_STOPPED&&t->phase!=M16_PP_FAILED)||
       !m16_projection_runtime_schedulable(t->runtime))return M16_PP_BUSY;
    if(!phat||!features||((uintptr_t)phat%_Alignof(float))||((uintptr_t)features%_Alignof(float))||
       count<1||count>16||nv<M16_PP_PHAT_VALUES||nf<needed||!epoch||
       !tag||tag<=t->reserved_tag_high||(!t->runtime->arbitrated&&tag<=t->runtime->tag_high)||tag>UINT64_MAX-167u||
       overlap(phat,(uintptr_t)M16_PP_PHAT_VALUES*4u,features,(uintptr_t)needed*4u)||
       buffers_overlap(t,phat,(uintptr_t)M16_PP_PHAT_VALUES*4u)||
       buffers_overlap(t,features,(uintptr_t)needed*4u))return M16_PP_INVALID;
    if(!float_environment())return M16_PP_FLOAT_ENV;
    t->phat=phat;t->features=features;t->count=count;t->epoch=epoch;t->first_tag=tag;
    t->reserved_tag_high=tag+167u;t->zone=0;t->phase=M16_PP_GEOMETRY;t->cursor=0;
    t->stop_requested=t->last_step_items=t->max_step_items=t->transactions=t->results=0;
    t->active_count=20u;t->active_mask=(1u<<20)-1u;
    for(unsigned i=0;i<20u;i++)t->active_frames[i]=i;
    t->row_valid=0;t->error=0;t->steps=0;return 0;
}
int m16_physical_projection_begin(m16_physical_projection_task *t,const float *phat,unsigned nv,
    const double *directions,unsigned count,float *features,unsigned nf,uint64_t epoch,uint64_t tag){
    unsigned i;int rc;
    if(!valid(t)||!directions||count<1||count>16||((uintptr_t)directions%_Alignof(double))||
       buffers_overlap(t,directions,(uintptr_t)count*2u*8u)||
       overlap(directions,(uintptr_t)count*2u*8u,features,(uintptr_t)count*20u*88u*4u))return M16_PP_INVALID;
    if(t->reuse_active)return M16_PP_BUSY;
    for(i=0;i<count*2u;i++)if(!finite64(directions[i])||directions[i]<=-90.||directions[i]>=90.)return M16_PP_INPUT;
    rc=begin_common(t,phat,nv,count,features,nf,count*M16_PP_FEATURES_PER_CANDIDATE,epoch,tag);if(rc)return rc;
    memcpy(t->directions,directions,count*2u*8u);t->kind=0;t->zone_begin=0;t->zone_end=24;return 0;
}
int m16_physical_projection_begin_grids(m16_physical_projection_task *t,const float *phat,unsigned nv,
    const unsigned *grids,unsigned count,unsigned zone,float *maps,unsigned nm,uint64_t epoch,uint64_t tag){
    unsigned i,j;int rc;
    if(!valid(t)||!grids||count<1||count>16||zone>=24||((uintptr_t)grids%_Alignof(unsigned))||
       buffers_overlap(t,grids,(uintptr_t)count*sizeof(unsigned))||
       overlap(grids,(uintptr_t)count*sizeof(unsigned),maps,20u*24u*325u*4u))return M16_PP_INVALID;
    for(i=0;i<count;i++){
        if(grids[i]>=325)return M16_PP_INPUT;
        for(j=0;j<i;j++)if(grids[i]==grids[j])return M16_PP_INPUT;
    }
    if(t->reuse_active&&(t->reuse_phat!=phat||t->reuse_epoch!=epoch||tag<t->reuse_scope))return M16_PP_INVALID;
    rc=begin_common(t,phat,nv,count,maps,nm,20u*24u*325u,epoch,tag);if(rc)return rc;
    memcpy(t->grids,grids,count*sizeof(unsigned));t->kind=1;t->zone_begin=zone;t->zone_end=zone+1;t->zone=zone;
    if(t->grid_cache){t->active_count=t->active_mask=0;t->cursor=0;t->phase=M16_PP_CACHE;}
    else{
        band_layout(t);t->phase=M16_PP_OBSERVATIONS;
        if(t->reuse_active&&t->reuse_valid&&t->reuse_zone==zone&&t->reuse_frame_mask==t->active_mask&&
           t->reuse_runtime_tag==t->runtime->tag_high&&t->reuse_revision==t->runtime->arena_revision){
            t->phase=M16_PP_COEFFICIENTS;t->observation_reuses++;
        }else t->reuse_valid=0;
    }
    return 0;
}
int m16_physical_projection_use_grid_cache(m16_physical_projection_task *t,m16_pp_grid_cache *c,uint64_t first){
    if(!valid(t)||(M16_PROJECTION_VERSION!=5u&&M16_PROJECTION_VERSION!=6u)||!c||!c->context||!c->lookup||!c->store||
       !t->reuse_active||t->grid_cache||first>UINT64_MAX-19u||
       (c->geometry_owner&&c->geometry_owner!=t)||
       (t->phase!=M16_PP_IDLE&&t->phase!=M16_PP_COMPLETE&&t->phase!=M16_PP_STOPPED))return M16_PP_INVALID;
    c->geometry_owner=t;t->cache_locked=1;t->grid_cache=c;t->cache_first_frame=first;return 0;
}
static int ownership(m16_physical_projection_task *t){
    m16_projection_runtime *r=t->runtime;
    if(t->phase<M16_PP_GEOMETRY||t->phase>M16_PP_SUBMIT)return 0;
    if(!r->arbitrated)return m16_projection_runtime_available(r)?0:fail(t,M16_PP_OWNERSHIP);
    if(t->phase==M16_PP_GEOMETRY)return 0;
    int rc=m16_projection_runtime_claim(r,t,1u);
    if(rc<0)return fail(t,rc);
    if(rc)return 1;
    if(t->reuse_valid&&t->reuse_revision!=r->arena_revision){
        t->reuse_valid=0;t->phase=M16_PP_OBSERVATIONS;t->cursor=0;
    }
    return 0;
}
static int step(m16_physical_projection_task *t){
    m16_projection_runtime *r=t->runtime;unsigned i,end;int rc;
    if(t->phase==M16_PP_FAILED)return t->error;
    if(t->phase==M16_PP_COMPLETE)return 0;
    if(t->phase==M16_PP_STOPPED)return 2;
    if(t->phase==M16_PP_IDLE)return M16_PP_INVALID;
    rc=ownership(t);if(rc<0)return rc;if(rc)return 1;
    if(t->phase==M16_PP_CACHE){
        unsigned f=t->cursor,missing=0;
        if(!float_environment())return fail(t,M16_PP_FLOAT_ENV);
        for(unsigned c=0;c<t->count;c++){
            float value;rc=t->grid_cache->lookup(t->grid_cache->context,t->epoch,t->cache_first_frame+f,t->zone,t->grids[c],&value);
            if(rc<0)return fail(t,rc);
            if(rc){t->features[(f*24u+t->zone)*325u+t->grids[c]]=value;t->cached_results++;}
            else missing=1;
            t->last_step_items++;
        }
        if(missing){t->active_frames[t->active_count++]=f;t->active_mask|=1u<<f;}
        if(++t->cursor==20u){
            if(!t->active_count){t->phase=M16_PP_COMPLETE;return 0;}
            band_layout(t);t->phase=M16_PP_OBSERVATIONS;
            if(t->reuse_active&&t->reuse_valid&&t->reuse_zone==t->zone&&t->reuse_frame_mask==t->active_mask&&
               t->reuse_runtime_tag==r->tag_high&&t->reuse_revision==r->arena_revision){
                t->phase=M16_PP_COEFFICIENTS;t->observation_reuses++;
            }else t->reuse_valid=0;
        }
        return 1;
    }
    if(t->phase==M16_PP_GEOMETRY){
        float arrivals[16];
        if(!float_environment())return fail(t,M16_PP_FLOAT_ENV);
        if(mic16_candidate_geometry(t->directions+t->cursor*2u,arrivals,t->lags+t->cursor*32u))return fail(t,M16_PP_INPUT);
        t->last_step_items=1;if(++t->cursor==t->count){band_layout(t);t->phase=M16_PP_OBSERVATIONS;}
        return 1;
    }
    if(t->phase==M16_PP_OBSERVATIONS){
        end=t->cursor+M16_PP_MAX_STEP_ITEMS;if(end>t->active_count*32u*t->bins)end=t->active_count*32u*t->bins;
        for(i=t->cursor;i<end;i++){
            unsigned b=i%t->bins,p=(i/t->bins)%32u,f=i/(t->bins*32u);
            unsigned q=((t->active_frames[f]*807u+t->zone*34u+b)*32u+p)*2u;
            float re=t->phat[q],im=t->phat[q+1u];
            if(!(re>=-1.000001f&&re<=1.000001f&&im>=-1.000001f&&im<=1.000001f))return fail(t,M16_PP_INPUT);
            r->arena[t->obs+i]=packed(re,im);t->last_step_items++;t->observation_words++;
        }
        t->cursor=end;if(end==t->active_count*32u*t->bins){
            t->cursor=0;t->phase=M16_PP_COEFFICIENTS;
            if(t->reuse_active){t->reuse_valid=1;t->reuse_zone=t->zone;t->reuse_frame_mask=t->active_mask;t->reuse_runtime_tag=r->tag_high;t->reuse_revision=r->arena_revision;}
        }
        return 1;
    }
    if(t->phase==M16_PP_COEFFICIENTS){
        if(!float_environment())return fail(t,M16_PP_FLOAT_ENV);
        end=t->cursor+16u;if(end>32u*t->bins*t->count)end=32u*t->bins*t->count;
        unsigned c=t->cursor%t->count,row=t->cursor/t->count,b=row%t->bins,p=row/t->bins;
        for(i=t->cursor;i<end;i++){
            float sr,si;
            if(t->kind&&t->row_provider){
                if(!t->row_valid||t->row_pair!=p){
                    for(unsigned k=0;k<t->count;k++){
                        if(t->row_provider(t->row_context,t->grids[k],p,&t->rows[k],&t->row_sign[k]))return fail(t,M16_PP_INPUT);
                    }
                    t->row_valid=1;t->row_pair=p;
                }
                r->arena[t->coef+i]=t->rows[c][t->zone*34u+b]^t->row_sign[c];
            }else if(t->kind){
                float signature[2];
                rc=t->signature_provider?t->signature_provider(t->signature_context,t->grids[c],p,32u+t->zone*34u+b,signature):
                    mic16_spatial_signature(t->grids[c],p,32u+t->zone*34u+b,signature);
                if(rc)return fail(t,M16_PP_INPUT);
                sr=signature[0];si=signature[1];r->arena[t->coef+i]=packed(sr,si);
            }else{
                double phase=2.*PI*(double)t->lags[c*32u+p]*(double)(32u+t->zone*34u+b)/4096.;
                sr=(float)cos(phase);si=(float)sin(phase);r->arena[t->coef+i]=packed(sr,si);
            }
            t->last_step_items++;
            if(++c==t->count){c=0;if(++b==t->bins){b=0;p++;}}
        }
        t->cursor=end;if(end==32u*t->bins*t->count){t->cursor=0;t->phase=M16_PP_DESCRIPTORS;}
        return 1;
    }
    if(t->phase==M16_PP_DESCRIPTORS){
#if M16_PROJECTION_VERSION>=4u
        uint64_t *d=r->arena+8u;
        d[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(3)<<48);
        d[1]=t->first_tag+(t->zone-t->zone_begin)*7u+6u;
        d[2]=t->obs|((uint64_t)t->coef<<32);d[3]=t->out|((uint64_t)t->receipts<<32);
        d[4]=t->active_count|((uint64_t)t->bins<<16)|((uint64_t)t->count<<32);
        d[5]=t->active_count*t->count;d[6]=(t->active_count*32u*t->bins)|((uint64_t)(32u*t->bins*t->count)<<32);d[7]=0;
#else
        for(i=0;i<7u;i++){
            unsigned first=i*3u,frames=i==6u?2u:3u;
            uint64_t *d=r->arena+8u+i*8u;
            d[0]=UINT64_C(0x4d313650)|((uint64_t)M16_PROJECTION_VERSION<<32)|(UINT64_C(3)<<48);
            d[1]=t->first_tag+(t->zone-t->zone_begin)*7u+i;
            d[2]=(t->obs+first*32u*t->bins)|((uint64_t)t->coef<<32);
            d[3]=(t->out+first*t->count)|((uint64_t)(t->receipts+i*2u)<<32);
            d[4]=frames|((uint64_t)t->bins<<16)|((uint64_t)t->count<<32);
            d[5]=frames*t->count;d[6]=(frames*32u*t->bins)|((uint64_t)(32u*t->bins*t->count)<<32);d[7]=0;
        }
#endif
        t->last_step_items=8u*M16_PP_JOB_COUNT;t->phase=M16_PP_SUBMIT;return 1;
    }
    if(t->phase==M16_PP_SUBMIT){
        rc=m16_projection_runtime_submit_for(r,t,8u,M16_PP_JOB_COUNT);
        if(rc==M16_PR_BUSY)return 1;
        if(rc)return fail(t,rc);
        t->transactions++;t->phase=M16_PP_WAIT;return 1;
    }
    if(t->phase==M16_PP_WAIT){
        rc=m16_projection_runtime_poll_for(r,t);if(rc<0)return fail(t,rc);if(rc)return 1;
        if(t->stop_requested){
            rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);t->phase=M16_PP_STOPPED;return 2;
        }
        t->cursor=0;t->phase=M16_PP_SCATTER;return 1;
    }
    if(t->phase!=M16_PP_SCATTER||r->phase!=M16_PR_COMPLETE)return fail(t,M16_PP_OWNERSHIP);
    if(!float_environment())return fail(t,M16_PP_FLOAT_ENV);
    end=t->cursor+M16_PP_MAX_STEP_ITEMS;if(end>t->active_count*t->count)end=t->active_count*t->count;
    for(i=t->cursor;i<end;i++){
        double acc;float value;unsigned f=t->active_frames[i/t->count],c=i%t->count;
        uint64_t word=r->arena[t->out+i];memcpy(&acc,&word,8);
        if(!finite64(acc)||acc>2.000005*32.*t->bins||acc<-2.000005*32.*t->bins)return fail(t,M16_PP_RESULT);
        /* 必须先转FP32再执行原FP32除法，不能改成FP64除完再转型。 */
        value=(float)acc/(float)(32u*t->bins);
        if(t->kind){
            t->features[(f*24u+t->zone)*325u+t->grids[c]]=value;
            if(t->grid_cache){
                rc=t->grid_cache->store(t->grid_cache->context,t->epoch,t->cache_first_frame+f,t->zone,t->grids[c],value);
                if(rc<0)return fail(t,rc);
            }
        }
        else t->features[(c*20u+f)*88u+t->zone]=value;
        t->last_step_items++;t->results++;
    }
    t->cursor=end;if(end==t->active_count*t->count){
        rc=m16_projection_runtime_retire_for(r,t);if(rc)return fail(t,rc);
        if(t->reuse_active){t->reuse_runtime_tag=r->tag_high;t->reuse_revision=r->arena_revision;}
        if(++t->zone==t->zone_end){t->phase=M16_PP_COMPLETE;return 0;}
        band_layout(t);t->phase=M16_PP_OBSERVATIONS;
    }
    return 1;
}
int m16_physical_projection_step(m16_physical_projection_task *t){
    int rc;if(!valid(t))return M16_PP_INVALID;t->last_step_items=0;t->steps++;
    rc=step(t);if(t->last_step_items>t->max_step_items)t->max_step_items=t->last_step_items;return rc;
}
int m16_physical_projection_stop(m16_physical_projection_task *t){
    int rc;if(!valid(t))return M16_PP_INVALID;
    if(t->phase==M16_PP_FAILED)return t->error;
    m16_physical_projection_reuse_end(t);t->stop_requested=1;
    if(t->phase==M16_PP_WAIT){
        rc=m16_projection_runtime_cancel_for(t->runtime,t);if(rc<0)return fail(t,rc);if(!rc)return 0;
    }else if(t->phase==M16_PP_SCATTER){
        rc=m16_projection_runtime_retire_for(t->runtime,t);if(rc)return fail(t,rc);
    }else if(!t->runtime->arbitrated&&ownership(t))return t->error;
    if(t->runtime->arbitrated){rc=m16_projection_runtime_unclaim(t->runtime,t);if(rc<0)return fail(t,rc);}
    t->phase=M16_PP_STOPPED;return 1;
}
