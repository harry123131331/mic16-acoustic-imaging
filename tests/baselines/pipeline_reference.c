#include "mic16_pipeline.h"
static int finite32(float x){return x==x && x>=-1.f && x<=1.f;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
int mic16_pipeline_observe(const float *pcm,const float *reference,
    mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
    unsigned int i,t,z;
    mic16_physical_workspace *p;
    if(!pcm || !reference || !o || !w)return -1;
    {
        const void *a[4]={pcm,reference,o,w};
        unsigned long n[4]={40960ul*16*4,8192ul*16*4,sizeof(*o),sizeof(*w)};
        unsigned int j;
        for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(overlap(a[i],n[i],a[j],n[j]))return -1;
    }
    for(i=0;i<40960*16;i++)if(!finite32(pcm[i]))return -1;
    for(i=0;i<8192*16;i++)if(!finite32(reference[i]))return -1;
    p=&w->extended.physical;
    mic16_spectral_init(&p->spectral);
    mic16_reference_init(&p->reference);
    for(t=0;t<4;t++){
        if(mic16_spectral_process(reference+t*2048*16,&p->frame,&p->spectral))return -2;
        if(mic16_reference_add(&p->reference,p->frame.zone_level_db))return -2;
    }
    if(mic16_reference_finalize(&p->reference))return -2;
    for(t=0;t<20;t++){
        if(mic16_spectral_process(pcm+t*2048*16,&p->frame,&p->spectral))return -3;
        for(i=0;i<807*32*2;i++)p->phat[t*807*32*2+i]=p->frame.phat[i];
        for(z=0;z<24;z++)p->levels[t*24+z]=p->frame.zone_level_db[z];
    }
    if(mic16_spatial_project(p->phat,w->maps,&w->spatial))return -4;
    if(mic16_spatial_votes(w->maps,p->levels,p->reference.floor_db,w->votes,w->best_indices))return -4;
    if(mic16_spatial_candidates(w->votes,&w->pool,&w->spatial))return -4;
    if(w->pool.count<1 || w->pool.count>16)return -4;
    for(i=0;i<32;i++)w->physical_directions[i]=0.;
    for(i=0;i<w->pool.count;i++){
        w->physical_directions[i*2]=w->pool.items[i].direction[0];
        w->physical_directions[i*2+1]=w->pool.items[i].direction[1];
    }
    if(mic16_features130(pcm,reference,w->physical_directions,w->pool.count,w->votes,
        o->features,o->directions,o->valid,&w->extended))return -5;
    if(mic16_frozen_score(o->features,o->directions,o->valid,o->logits,&w->score))return -6;
    for(i=0;i<16;i++)o->scores[i]=mic16_frozen_probability(o->logits[i]);
    o->candidate_count=w->pool.count;
    return 0;
}
