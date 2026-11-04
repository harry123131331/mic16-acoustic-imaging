#include "mic16_pipeline.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int strcmp(const char *,const char *);
extern int printf(const char *,...);
static float pcm[40960*16],quiet[8192*16];
static mic16_pipeline_workspace work;
static mic16_pipeline_observation observation;
static mic16_search_memory memory;
static mic16_cycle_output event;
static int selftest(void){
    int n=0,e=0;
    observation.candidate_count=77;
    if(mic16_pipeline_observe(0,quiet,&observation,&work)!=-1)e++;n++;
    if(mic16_pipeline_observe(pcm,0,&observation,&work)!=-1)e++;n++;
    if(mic16_pipeline_observe(pcm,quiet,0,&work)!=-1)e++;n++;
    if(mic16_pipeline_observe(pcm,quiet,&observation,0)!=-1)e++;n++;
    if(mic16_pipeline_observe(pcm,pcm,&observation,&work)!=-1)e++;n++;
    pcm[0]=2.f;if(mic16_pipeline_observe(pcm,quiet,&observation,&work)!=-1)e++;n++;pcm[0]=0.f;
    quiet[0]=-2.f;if(mic16_pipeline_observe(pcm,quiet,&observation,&work)!=-1)e++;n++;quiet[0]=0.f;
    {union{unsigned int u;float f;}a;a.u=0x7fc00000u;pcm[0]=a.f;}
    if(mic16_pipeline_observe(pcm,quiet,&observation,&work)!=-1)e++;n++;pcm[0]=0.f;
    if(observation.candidate_count!=77)e++;n++;
    printf("PIPELINE_SELFTEST checks=%d errors=%d workspace_bytes=%u observation_bytes=%u\n",n,e,
        (unsigned int)sizeof(work),(unsigned int)sizeof(observation));
    return e;
}
static int events(void){
    int cycle,i;double empty[16]={0};mic16_memory_init(&memory);
    for(cycle=0;cycle<5;cycle++){
        if(mic16_consume_scored_cycle(&memory,observation.directions,observation.valid,
            cycle<2?observation.scores:empty,(cycle+1)*1.048576,"fixed_numerical_reference",1,&event))return -1;
        printf("EVENT {\"cycle\":%d,\"count\":%d,\"confirmed\":%d,\"new_id\":%u,\"expired\":[",
            event.cycle,event.count,event.confirmed_count,event.new_source_valid?event.new_source.source_id:0u);
        for(i=0;i<event.expired_count;i++)printf("%s%u",i?",":"",event.expired_ids[i]);
        printf("],\"tracks\":[");
        for(i=0;i<event.count;i++){
            mic16_track *t=&event.tracks[i];
            printf("%s{\"id\":%u,\"a\":%.17g,\"e\":%.17g,\"score\":%.17g,\"first\":%d,\"last\":%d,\"current\":%d}",
                i?",":"",t->source_id,t->azimuth,t->elevation,t->score,t->first_seen_cycle,t->last_seen_cycle,t->confirmed_this_cycle);
        }
        printf("]}\n");
    }
    return 0;
}
int main(int argc,char **argv){
    void *in,*out;unsigned int i;
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=3)return 9;
    in=fopen(argv[1],"rb");if(!in)return 10;
    if(fread(pcm,4,40960*16,in)!=40960*16 || fread(quiet,4,8192*16,in)!=8192*16)return 11;
    fclose(in);
    i=(unsigned int)mic16_pipeline_observe(pcm,quiet,&observation,&work);
    if(i){printf("PIPELINE_OBSERVE_ERROR code=%d\n",(int)i);return 12;}
    out=fopen(argv[2],"wb");if(!out)return 13;
    if(fwrite(&observation.candidate_count,4,1,out)!=1 || fwrite(work.physical_directions,8,32,out)!=32 ||
        fwrite(work.votes,8,480*9,out)!=480*9 || fwrite(work.best_indices,2,480,out)!=480 ||
        fwrite(work.maps,4,M16_SP_MAP_FLOATS,out)!=M16_SP_MAP_FLOATS ||
        fwrite(observation.features,4,16*20*130,out)!=16*20*130 || fwrite(observation.directions,8,32,out)!=32 ||
        fwrite(observation.valid,1,16,out)!=16 || fwrite(observation.logits,4,16,out)!=16 || fwrite(observation.scores,8,16,out)!=16)return 14;
    fclose(out);
    printf("POOL [");
    for(i=0;i<work.pool.count;i++){
        mic16_cluster *c=&work.pool.items[i];
        printf("%s{\"seed_grid\":%u,\"votes\":%u,\"frames\":%u,\"bands\":%u,\"weight\":%.17g}",
            i?",":"",c->seed_grid,c->votes,c->frames,c->bands,c->weight_sum);
    }
    printf("]\n");
    return events()?15:0;
}
