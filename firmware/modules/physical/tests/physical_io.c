#include "mic16_physical.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int strcmp(const char *,const char *);
extern int printf(const char *,...);
static float pcm[40960*16],reference[8192*16],features[16*20*88];
static double directions[32],votes[480*9],quiet_bands[4*32];
static mic16_physical_workspace work;
static int selftest(void){
    int errors=0,n=0,i;float arrivals[16],lags[32];unsigned int guard=99;
    double d[2]={0.,0.};features[0]=123.f;
    if(mic16_physical_features(0,reference,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;
    if(mic16_physical_features(pcm,reference,directions,0,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;
    if(mic16_physical_features(pcm,reference,directions,17,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;
    if(mic16_physical_features(pcm,pcm,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;
    pcm[0]=2.f;
    if(mic16_physical_features(pcm,reference,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;pcm[0]=0.f;
    {union{unsigned int u;float f;}q;q.u=0x7fc00000u;reference[0]=q.f;}
    if(mic16_physical_features(pcm,reference,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;reference[0]=0.f;
    directions[0]=90.;
    if(mic16_physical_features(pcm,reference,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;directions[0]=0.;
    if(mic16_physical_features(pcm,reference,directions,16,votes,features,&work)!=-1 || features[0]!=123.f)errors++;n++;
    if(mic16_candidate_beam(pcm,40960,d,pcm,&guard)!=-1)errors++;n++;
    if(mic16_candidate_beam(pcm,64,d,work.beam,&guard)!=-1)errors++;n++;
    if(mic16_candidate_geometry(d,arrivals,lags))errors++;
    for(i=0;i<16;i++)if(arrivals[i]!=0.f)errors++;
    for(i=0;i<32;i++)if(lags[i]!=0.f)errors++;n++;
    if(mic16_candidate_beam(pcm,40960,d,work.beam,&guard) || guard!=12)errors++;n++;
    if(mic16_mono_bands(work.beam,20,work.bands,work.beam_level,&work)!=-1)errors++;n++;
    printf("PHYSICAL_SELFTEST checks=%d errors=%d workspace_bytes=%u\n",n,errors,(unsigned int)sizeof(work));
    return errors;
}
int main(int argc,char **argv){
    void *in,*out;int c;unsigned int guard,qguard;float arrival[16],lags[32];
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=4 || strcmp(argv[1],"features"))return 10;
    in=fopen(argv[2],"rb");out=fopen(argv[3],"wb");if(!in || !out)return 11;
    if(fread(directions,8,32,in)!=32 || fread(votes,8,480*9,in)!=480*9 ||
        fread(pcm,4,40960*16,in)!=40960*16 || fread(reference,4,8192*16,in)!=8192*16)return 12;
    if(mic16_physical_features(pcm,reference,directions,16,votes,features,&work))return 13;
    if(fwrite(features,4,16*20*88,out)!=16*20*88)return 14;
    for(c=0;c<16;c++){
        if(mic16_candidate_geometry(directions+c*2,arrival,lags) ||
            mic16_candidate_beam(pcm,40960,directions+c*2,work.beam,&guard) ||
            mic16_candidate_beam(reference,8192,directions+c*2,work.quiet_beam,&qguard))return 15;
        if(mic16_mono_bands(work.beam,20,work.bands,work.beam_level,&work) ||
            mic16_mono_bands(work.quiet_beam,4,quiet_bands,work.quiet_level,&work))return 16;
        if(fwrite(arrival,4,16,out)!=16 || fwrite(lags,4,32,out)!=32 || fwrite(&guard,4,1,out)!=1 ||
            fwrite(work.beam,4,40960,out)!=40960 || fwrite(work.quiet_beam,4,8192,out)!=8192 ||
            fwrite(work.bands,8,20*32,out)!=20*32 || fwrite(work.beam_level,8,20,out)!=20 ||
            fwrite(quiet_bands,8,4*32,out)!=4*32 || fwrite(work.quiet_level,8,4,out)!=4)return 17;
    }
    fclose(in);fclose(out);printf("PHYSICAL_FEATURES windows=1 candidates=16 features=88\n");return 0;
}
