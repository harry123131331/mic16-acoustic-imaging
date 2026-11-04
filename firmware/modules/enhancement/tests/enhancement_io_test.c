#include "mic16_enhancement.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int printf(const char *,...);
extern int strcmp(const char *,const char *);
static float pcm[40960*16],quiet[8192*16],features[67*64*10],mask[67*64];
static double directions[32];
static unsigned char valid[16];
static mic16_cycle_output tracks;
static mic16_extended_workspace shared;
static m16_enh_workspace workspace;
static int emit(void *ctx,unsigned int id,unsigned int candidate,const float *x,const float *m,
    const double *s,const double *wdas,const double *enhanced,unsigned int samples){
    void *out=ctx;
    if(samples!=16778)return -1;
    if(fwrite(&id,4,1,out)!=1 || fwrite(&candidate,4,1,out)!=1 ||
       fwrite(x,4,67*64*10,out)!=67*64*10 || fwrite(m,4,67*64,out)!=67*64 ||
       fwrite(s,8,67*257*2,out)!=67*257*2 || fwrite(wdas,8,samples,out)!=samples ||
       fwrite(enhanced,8,samples,out)!=samples)return -1;
    return 0;
}
static int ignored(void *ctx,unsigned int id,unsigned int c,const float *x,const float *m,
    const double *s,const double *a,const double *b,unsigned int n){
    (void)ctx;(void)id;(void)c;(void)x;(void)m;(void)s;(void)a;(void)b;(void)n;
    return 1;
}
static int selftest(void){
    int n=0,fail=0,rc;unsigned int i;union{unsigned int u;float f;}bits;
    mask[0]=123.f;
    rc=m16_enh_mask(0,mask,&workspace.network);if(rc!=-1 || mask[0]!=123.f)fail++;n++;
    rc=m16_enh_mask(features,features,&workspace.network);if(rc!=-1)fail++;n++;
    bits.u=0x7fc00000u;features[0]=bits.f;
    rc=m16_enh_mask(features,mask,&workspace.network);if(rc!=-1 || mask[0]!=123.f)fail++;n++;features[0]=0.f;
    features[0]=65505.f;rc=m16_enh_mask(features,mask,&workspace.network);if(rc!=-1)fail++;n++;features[0]=0.f;
    rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=0)fail++;n++;
    rc=m16_enh_window(pcm,pcm,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;
    tracks.count=9;rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;tracks.count=0;
    valid[3]=2;rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;valid[3]=0;
    tracks.count=2;tracks.tracks[0].source_id=7;tracks.tracks[1].source_id=7;
    rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;
    tracks.count=1;tracks.tracks[0].confirmed_this_cycle=1;tracks.tracks[0].score=.9;tracks.confirmed_count=1;
    rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;
    valid[0]=1;tracks.tracks[0].score=.64;
    rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=-1)fail++;n++;
    tracks.tracks[0].score=.9;tracks.tracks[0].confirmed_this_cycle=0;tracks.confirmed_count=0;
    for(i=1;i<8;i++){tracks.tracks[i].source_id=i+8;tracks.tracks[i].confirmed_this_cycle=0;}
    tracks.count=8;rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,ignored,0);if(rc!=0)fail++;n++;
    printf("ENHANCEMENT_SELFTEST checks=%d errors=%d workspace_bytes=%u shared_bytes=%u\n",n,fail,
        (unsigned int)sizeof(workspace),(unsigned int)sizeof(shared));
    return fail;
}
int main(int argc,char **argv){
    void *in,*out;int i,rc,n=0;unsigned int count;
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=4)return 9;
    in=fopen(argv[2],"rb");if(!in)return 8;
    out=fopen(argv[3],"wb");if(!out){fclose(in);return 8;}
    if(!strcmp(argv[1],"half")){
        float v;
        while(fread(&v,4,1,in)==1){v=m16_enh_half_roundtrip(v);if(fwrite(&v,4,1,out)!=1)return 7;n++;}
        rc=0;
    }else if(!strcmp(argv[1],"mask")){
        __SIZE_TYPE__ got;
        while((got=fread(features,4,67*64*10,in))!=0){
            if(got!=67*64*10 || m16_enh_mask(features,mask,&workspace.network))return 6;
            if(fwrite(mask,4,67*64,out)!=67*64)return 7;n++;
        }
        rc=0;
    }else if(!strcmp(argv[1],"window")){
        if(fread(pcm,4,40960*16,in)!=40960*16 || fread(quiet,4,8192*16,in)!=8192*16 ||
           fread(directions,8,32,in)!=32 || fread(valid,1,16,in)!=16 || fread(&count,4,1,in)!=1 || count>8)return 6;
        tracks.count=(int)count;tracks.confirmed_count=0;
        for(i=0;i<(int)count;i++){
            unsigned int flag;
            if(fread(&tracks.tracks[i].source_id,4,1,in)!=1 || fread(&flag,4,1,in)!=1 ||
               fread(&tracks.tracks[i].azimuth,8,1,in)!=1 || fread(&tracks.tracks[i].elevation,8,1,in)!=1 ||
               fread(&tracks.tracks[i].score,8,1,in)!=1)return 6;
            if(flag>1)return 6;
            tracks.tracks[i].confirmed_this_cycle=(int)flag;tracks.confirmed_count+=(int)flag;
        }
        rc=m16_enh_window(pcm,quiet,directions,valid,&tracks,&shared,&workspace,emit,out);n=rc;
        if(rc<0)return 5;
        if(fwrite(workspace.weights,4,320,out)!=320 || fwrite(&rc,4,1,out)!=1)return 7;
    }else return 9;
    if(fclose(in) || fclose(out))return 7;
    printf("ENHANCEMENT_IO mode=%s cases_or_sources=%d rc=%d\n",argv[1],n,rc);
    return 0;
}
