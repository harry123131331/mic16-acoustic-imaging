#include "mic16_spectral_front.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int printf(const char *,...);
extern int strcmp(const char *,const char *);
static float input[2048*16];
static mic16_spectral_workspace workspace;
static mic16_spectral_frame output;
static mic16_spectral_reference reference;
static unsigned int output_hash(void){
    const unsigned char *p=(const unsigned char *)&output;
    unsigned int h=2166136261u;__SIZE_TYPE__ i;
    for(i=0;i<sizeof(output);i++)h=(h^p[i])*16777619u;
    return h;
}
static int checks(void){
    int i,j,fail=0,n=0;unsigned int old=output_hash();double lev[24];float ri[2],quality[48];
    mic16_spectral_workspace uninitialized;uninitialized.initialized=0;
    if(mic16_spectral_process(input,&output,&uninitialized)!=-1 || output_hash()!=old)fail++;n++;
    mic16_spectral_init(&workspace);
    if(mic16_spectral_process(0,&output,&workspace)!=-1 || output_hash()!=old)fail++;n++;
    if(mic16_spectral_process(input,0,&workspace)!=-1)fail++;n++;
    input[0]=1.01f;
    if(mic16_spectral_process(input,&output,&workspace)!=-1 || output_hash()!=old)fail++;n++;
    {union{unsigned int u;float f;}x;x.u=0x7fc00000u;input[0]=x.f;}
    if(mic16_spectral_process(input,&output,&workspace)!=-1 || output_hash()!=old)fail++;n++;input[0]=0;
    if(mic16_spectral_process((const float *)&output,&output,&workspace)!=-1 || output_hash()!=old)fail++;n++;
    mic16_reference_init(&reference);
    if(mic16_reference_finalize(&reference)!=-2 || reference.count!=0 || reference.finalized)fail++;n++;
    for(i=0;i<4;i++){
        for(j=0;j<24;j++)lev[j]=(double)(i==0?9:i==1?1:i==2?5:3)+j;
        if(mic16_reference_add(&reference,lev))fail++;
    }
    if(mic16_reference_finalize(&reference))fail++;
    for(j=0;j<24;j++)if(reference.floor_db[j]!=4.+j)fail++;n++;
    if(mic16_reference_add(&reference,lev)!=-2 || reference.count!=4)fail++;n++;
    if(mic16_reference_finalize(&reference)!=-2)fail++;n++;
    if(mic16_spectral_quality(lev,&reference,quality))fail++;
    for(j=0;j<24;j++)if(quality[j]!=-.05f || quality[j+24]!=(float)((3.+j)/40.))fail++;n++;
    for(i=0;i<7;i++){
        static const int values[7]={0,1,-1,16777216,-16777216,33554431,-33554432};
        int a=values[i],b=values[6-i];unsigned long long word=((unsigned long long)(a&0x3ffffff))|((unsigned long long)(b&0x3ffffff)<<26);
        if(mic16_decode_q24_complex(word,ri) || ri[0]!=(float)a/16777216.f || ri[1]!=(float)b/16777216.f)fail++;
    }
    ri[0]=123;ri[1]=456;
    if(mic16_decode_q24_complex(1ull<<60,ri)!=-1 || ri[0]!=123 || ri[1]!=456)fail++;n++;
    output.spectrum[0]=1e300;old=output_hash();
    if(mic16_spectral_observe(&output)!=-1 || output_hash()!=old)fail++;n++;
    {union{unsigned long long u;double d;}x;x.u=0x7ff8000000000000ull;output.spectrum[0]=x.d;}
    old=output_hash();
    if(mic16_spectral_observe(&output)!=-1 || output_hash()!=old)fail++;n++;
    output.spectrum[0]=0;
    printf("SPECTRAL_SELFTEST checks=%d Q24_cases=7 errors=%d frame_bytes=%u workspace_bytes=%u reference_bytes=%u\n",
        n,fail,(unsigned int)sizeof(output),(unsigned int)sizeof(workspace),(unsigned int)sizeof(reference));
    return fail;
}
int main(int argc,char **argv){
    void *in,*out;int n=0,rc;
    if(argc==2 && !strcmp(argv[1],"selftest"))return checks();
    if(argc!=4)return 9;
    in=fopen(argv[2],"rb");out=fopen(argv[3],"wb");if(!in || !out)return 8;
    mic16_spectral_init(&workspace);
    if(!strcmp(argv[1],"frames")){
        __SIZE_TYPE__ got;
        while((got=fread(input,sizeof(float),2048*16,in))!=0){
            if(got!=2048*16)return 7;
            rc=mic16_spectral_process(input,&output,&workspace);if(rc)return 6;
            if(fwrite(&output,sizeof(output),1,out)!=1)return 5;n++;
        }
    }else if(!strcmp(argv[1],"quality")){
        double levels[24];float quality[48];int i;
        mic16_reference_init(&reference);
        for(i=0;i<4;i++){
            if(fread(levels,sizeof(double),24,in)!=24 || mic16_reference_add(&reference,levels))return 7;
        }
        if(mic16_reference_finalize(&reference) || fwrite(reference.floor_db,sizeof(double),24,out)!=24)return 6;
        for(i=0;i<20;i++){
            if(fread(levels,sizeof(double),24,in)!=24 || mic16_spectral_quality(levels,&reference,quality))return 5;
            if(fwrite(quality,sizeof(float),48,out)!=48)return 4;n++;
        }
    }else if(!strcmp(argv[1],"spectrum")){
        if(fread(output.spectrum,sizeof(double),807*16*2,in)!=807*16*2)return 4;
        if(mic16_spectral_observe(&output))return 3;
        if(fwrite(&output,sizeof(output),1,out)!=1)return 2;n++;
    }else return 1;
    fclose(in);fclose(out);
    printf("SPECTRAL_STREAM frames=%d\n",n);
    return 0;
}
