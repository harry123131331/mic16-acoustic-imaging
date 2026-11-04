#include "mic16_stft_batch.h"
/* 索引优化显式分支，来源及唯一改动见beam_index安装记录。 */
#include "mic16_beam_index.h"
/* 显式优化入口；每窗先准备全新公共数据，不跨窗复用。 */
#include "mic16_pipeline.h"
static int finite32(float x){return x==x && x>=-1.f && x<=1.f;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
static unsigned long long profile_read(const mic16_profile_clock *clock,mic16_pipeline_profile *r){
    unsigned long long t=clock->read(clock->context);
    if(r->reads && t<r->last_tick)r->clock_ok=0;
    r->last_tick=t;r->reads++;return t;
}
static unsigned long long phase_begin(const mic16_profile_clock *clock,mic16_pipeline_profile *r,unsigned stage){
    if(!r)return 0;
    r->attempted_mask|=1u<<stage;
    return profile_read(clock,r);
}
static int phase_end(const mic16_profile_clock *clock,mic16_pipeline_profile *r,unsigned stage,
    unsigned long long start,int rc){
    if(r){
        unsigned long long end=profile_read(clock,r);
        if(end>=start)r->ticks[stage]=end-start;else r->clock_ok=0;
        if(!rc)r->completed_mask|=1u<<stage;
        r->pipeline_rc=rc;
    }
    return rc;
}
int mic16_pipeline_observe_batched_timed(const float *pcm,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w,
    const mic16_profile_clock *clock,mic16_pipeline_profile *profile){
    unsigned int i,t,z;
    unsigned long long start;
    mic16_physical_workspace *p;mic16_prepared_spectral prepared;
    if(!pcm || !reference || !o || !w)return -1;
    if((clock && (!clock->read || !profile)) || (!clock && profile))return -1;
    {
        const void *a[4]={pcm,reference,o,w};
        unsigned long n[4]={40960ul*16*4,8192ul*16*4,sizeof(*o),sizeof(*w)};
        unsigned int j;
        for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(overlap(a[i],n[i],a[j],n[j]))return -1;
        if(profile){
            for(i=0;i<4;i++)if(overlap(a[i],n[i],profile,sizeof(*profile)))return -1;
            if(overlap(clock,sizeof(*clock),profile,sizeof(*profile)))return -1;
        }
    }
    for(i=0;i<40960*16;i++)if(!finite32(pcm[i]))return -1;
    for(i=0;i<8192*16;i++)if(!finite32(reference[i]))return -1;
    if(profile){
        for(i=0;i<MIC16_PROFILE_STAGES;i++)profile->ticks[i]=0;
        profile->last_tick=0;profile->reads=0;profile->attempted_mask=0;
        profile->completed_mask=0;profile->clock_ok=1;profile->pipeline_rc=0;
    }
    p=&w->extended.physical;
    start=phase_begin(clock,profile,MIC16_PROFILE_REFERENCE);
    mic16_spectral_init(&p->spectral);
    mic16_reference_init(&p->reference);
    for(t=0;t<4;t++){
        if(mic16_spectral_process(reference+t*2048*16,&p->frame,&p->spectral))return phase_end(clock,profile,MIC16_PROFILE_REFERENCE,start,-2);
        if(mic16_reference_add(&p->reference,p->frame.zone_level_db))return phase_end(clock,profile,MIC16_PROFILE_REFERENCE,start,-2);
    }
    if(mic16_reference_finalize(&p->reference))return phase_end(clock,profile,MIC16_PROFILE_REFERENCE,start,-2);
    phase_end(clock,profile,MIC16_PROFILE_REFERENCE,start,0);
    start=phase_begin(clock,profile,MIC16_PROFILE_SPECTRAL);
    for(t=0;t<20;t++){
        if(mic16_spectral_process(pcm+t*2048*16,&p->frame,&p->spectral))return phase_end(clock,profile,MIC16_PROFILE_SPECTRAL,start,-3);
        for(i=0;i<807*32*2;i++)p->phat[t*807*32*2+i]=p->frame.phat[i];
        for(z=0;z<24;z++)p->levels[t*24+z]=p->frame.zone_level_db[z];
    }
    phase_end(clock,profile,MIC16_PROFILE_SPECTRAL,start,0);
    start=phase_begin(clock,profile,MIC16_PROFILE_SPATIAL);
    if(mic16_spatial_project(p->phat,w->maps,&w->spatial))return phase_end(clock,profile,MIC16_PROFILE_SPATIAL,start,-4);
    phase_end(clock,profile,MIC16_PROFILE_SPATIAL,start,0);
    start=phase_begin(clock,profile,MIC16_PROFILE_POOL);
    if(mic16_spatial_votes(w->maps,p->levels,p->reference.floor_db,w->votes,w->best_indices))return phase_end(clock,profile,MIC16_PROFILE_POOL,start,-4);
    if(mic16_spatial_candidates(w->votes,&w->pool,&w->spatial))return phase_end(clock,profile,MIC16_PROFILE_POOL,start,-4);
    if(w->pool.count<1 || w->pool.count>16)return phase_end(clock,profile,MIC16_PROFILE_POOL,start,-4);
    for(i=0;i<32;i++)w->physical_directions[i]=0.;
    for(i=0;i<w->pool.count;i++){
        w->physical_directions[i*2]=w->pool.items[i].direction[0];
        w->physical_directions[i*2+1]=w->pool.items[i].direction[1];
    }
    phase_end(clock,profile,MIC16_PROFILE_POOL,start,0);
    start=phase_begin(clock,profile,MIC16_PROFILE_FEATURES);
    prepared.pcm=pcm;prepared.reference=reference;prepared.owner=p;
    if(mic16_features130_batched(pcm,reference,w->physical_directions,w->pool.count,w->votes,
        o->features,o->directions,o->valid,&w->extended,&prepared,M16_REUSE_ALL,0))return phase_end(clock,profile,MIC16_PROFILE_FEATURES,start,-5);
    phase_end(clock,profile,MIC16_PROFILE_FEATURES,start,0);
    start=phase_begin(clock,profile,MIC16_PROFILE_NEURAL);
    if(mic16_frozen_score(o->features,o->directions,o->valid,o->logits,&w->score))return phase_end(clock,profile,MIC16_PROFILE_NEURAL,start,-6);
    for(i=0;i<16;i++)o->scores[i]=mic16_frozen_probability(o->logits[i]);
    o->candidate_count=w->pool.count;
    return phase_end(clock,profile,MIC16_PROFILE_NEURAL,start,0);
}
int mic16_pipeline_observe_batched(const float *pcm,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
    return mic16_pipeline_observe_batched_timed(pcm,reference,o,w,0,0);
}
