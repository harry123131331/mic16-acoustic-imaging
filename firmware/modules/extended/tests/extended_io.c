#include "mic16_extended.h"
#include "mic16_frozen.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int strcmp(const char *,const char *);
extern int printf(const char *,...);
static float pcm[40960*16],quiet[8192*16],features[16*20*130],logits[16],impulse[64];
static double directions[32],model_directions[32],votes[480*9],scores[16];
static unsigned char valid[16],peers[256];
static mic16_extended_workspace work;
static mic16_score_workspace scorer;
static mic16_search_memory memory;
static mic16_cycle_output event;
static int selftest(void){
    int n=0,e=0;unsigned char bad[16]={0};double d[32]={0};float *x=work.features106;
    if(mic16_resample_channel(0,64,1,0,work.resampled)!=-1)e++;n++;
    if(mic16_resample_channel(pcm,0,1,0,work.resampled)!=-1)e++;n++;
    if(mic16_resample_channel(pcm,64,0,0,work.resampled)!=-1)e++;n++;
    if(mic16_resample_channel(pcm,64,16,16,work.resampled)!=-1)e++;n++;
    pcm[0]=2.f;if(mic16_resample_channel(pcm,64,16,0,work.resampled)!=-1)e++;n++;pcm[0]=0.f;
    if(mic16_stft512_frame(work.resampled,511,0,&work)!=-1)e++;n++;
    if(mic16_stft512_frame(work.resampled,16778,67,&work)!=-1)e++;n++;
    {union{unsigned long long u;double d;}a;a.u=0x7ff8000000000000ull;work.resampled[0]=a.d;}
    if(mic16_stft512_frame(work.resampled,16778,0,&work)!=-1)e++;n++;work.resampled[0]=0.;
    bad[0]=2;if(mic16_contrast24(x,d,bad,work.contrasts24,peers)!=-1)e++;n++;bad[0]=0;
    d[0]=90.;if(mic16_contrast24(x,d,bad,work.contrasts24,peers)!=-1)e++;n++;d[0]=0.;
    if(mic16_features130(pcm,quiet,d,0,votes,features,model_directions,valid,&work)!=-1)e++;n++;
    if(mic16_features130(pcm,quiet,d,17,votes,features,model_directions,valid,&work)!=-1)e++;n++;
    if(mic16_features130(pcm,quiet,d,1,votes,0,model_directions,valid,&work)!=-1)e++;n++;
    printf("EXTENDED_SELFTEST checks=%d errors=%d workspace_bytes=%u\n",n,e,(unsigned int)sizeof(work));
    return e;
}
static int events(void){
    int cycle,i;double empty[16]={0};
    mic16_memory_init(&memory);
    for(cycle=0;cycle<5;cycle++){
        if(mic16_consume_scored_cycle(&memory,model_directions,valid,cycle<2?scores:empty,
            (cycle+1)*1.048576,"fixed_numerical_reference",1,&event))return -1;
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
    void *in,*out;int i,t,b;unsigned int count;
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=4)return 9;
    in=fopen(argv[2],"rb");out=fopen(argv[3],"wb");if(!in || !out)return 10;
    if(!strcmp(argv[1],"contrast")){
        if(fread(work.features106,4,16*20*106,in)!=16*20*106 || fread(model_directions,8,32,in)!=32)return 11;
        for(i=0;i<4;i++){
            if(fread(valid,1,16,in)!=16 || mic16_contrast24(work.features106,model_directions,valid,work.contrasts24,work.peers))return 12;
            if(fwrite(work.contrasts24,4,16*20*24,out)!=16*20*24 || fwrite(work.peers,1,256,out)!=256)return 13;
        }
    }else if(!strcmp(argv[1],"features")){
        if(fread(&count,4,1,in)!=1 || fread(directions,8,32,in)!=32 || fread(votes,8,480*9,in)!=480*9 || fread(pcm,4,40960*16,in)!=40960*16 ||
            fread(quiet,4,8192*16,in)!=8192*16)return 14;
        if(mic16_features130(pcm,quiet,directions,count,votes,features,model_directions,valid,&work))return 15;
        if(mic16_frozen_score(features,model_directions,valid,logits,&scorer))return 16;
        for(i=0;i<16;i++)scores[i]=mic16_frozen_probability(logits[i]);
        if(fwrite(features,4,16*20*130,out)!=16*20*130 || fwrite(model_directions,8,32,out)!=32 || fwrite(valid,1,16,out)!=16 ||
            fwrite(work.peers,1,256,out)!=256 || fwrite(work.spectral10,4,67*64*10,out)!=67*64*10 ||
            fwrite(logits,4,16,out)!=16 || fwrite(scores,8,16,out)!=16)return 17;
        /* 使用已经观测的STFT通道0，不重新计算。 */
        for(t=0;t<67;t++)for(b=0;b<257;b++)if(fwrite(work.observed+((t*257+b)*16)*2,8,2,out)!=2)return 18;
        if(mic16_resample_channel(pcm,40960,16,0,work.resampled)!=16778 || fwrite(work.resampled,8,16778,out)!=16778)return 19;
        if(mic16_resample_channel(quiet,8192,16,0,work.resampled)!=3356 || fwrite(work.resampled,8,3356,out)!=3356)return 20;
        impulse[0]=.25f;impulse[63]=-.125f;
        if(mic16_resample_channel(impulse,64,1,0,work.resampled)!=27 || fwrite(work.resampled,8,27,out)!=27)return 21;
        if(events())return 22;
    }else return 23;
    fclose(in);fclose(out);return 0;
}
