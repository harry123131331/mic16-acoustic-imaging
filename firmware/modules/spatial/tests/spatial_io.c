#include "mic16_spatial.h"
#include "mic16_tf_zone_streaming_core.h"
extern void *fopen(const char *,const char *);
extern __SIZE_TYPE__ fread(void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern __SIZE_TYPE__ fwrite(const void *,__SIZE_TYPE__,__SIZE_TYPE__,void *);
extern int fclose(void *);
extern int printf(const char *,...);
extern int strcmp(const char *,const char *);
extern double cos(double);
extern double sin(double);
static float phat[M16_SP_PHAT_FLOATS],maps[M16_SP_MAP_FLOATS],old_maps[24*325];
static double levels[480],floors[24],votes[480*9];
static unsigned short best[480];
static mic16_spatial_workspace work;
static mic16_pool pool;
static mic16_complex_f32_t initial[325*32],rotate[325*32],old_work[325*32];
static int save_pool(void *f){
    unsigned int i;
    if(fwrite(&pool.count,4,1,f)!=1)return -1;
    for(i=0;i<pool.count;i++){
        const mic16_cluster *c=&pool.items[i];
        if(fwrite(c->direction,8,2,f)!=2 || fwrite(&c->weight_sum,8,1,f)!=1 ||
            fwrite(&c->seed_grid,4,1,f)!=1 || fwrite(&c->votes,4,1,f)!=1 ||
            fwrite(&c->frames,4,1,f)!=1 || fwrite(&c->bands,4,1,f)!=1)return -1;
    }
    return 0;
}
static int old_scan(void *f){
    const float *lags=mic16_spatial_lags();unsigned short offsets[25];int i;
    for(i=0;i<=24;i++)offsets[i]=(unsigned short)(i==24?807:i*34);
    for(i=0;i<325*32;i++){
        double p=2.*3.14159265358979323846264338327950288*(double)lags[i]/4096.;
        initial[i].re=(float)cos(p*32.);initial[i].im=(float)sin(p*32.);
        rotate[i].re=(float)cos(p);rotate[i].im=(float)sin(p);
    }
    if(mic16_tf_zone_streaming_scores((const mic16_complex_f32_t *)phat,initial,rotate,
        325,32,807,offsets,24,old_work,old_maps))return -1;
    return fwrite(old_maps,sizeof(float),24*325,f)==24*325?0:-1;
}
static int selftest(void){
    int n=0,err=0;float ri[2];mic16_cluster clusters[3]={0};
    if(mic16_spatial_project(0,maps,&work)!=-1)err++;n++;
    phat[0]=2.f;maps[0]=123.f;
    if(mic16_spatial_project(phat,maps,&work)!=-1 || maps[0]!=123.f)err++;n++;phat[0]=0;
    {union{unsigned int u;float f;}a;a.u=0x7fc00000u;phat[0]=a.f;}
    if(mic16_spatial_project(phat,maps,&work)!=-1 || maps[0]!=123.f)err++;n++;phat[0]=0;
    if(mic16_spatial_project(phat,phat,&work)!=-1)err++;n++;
    if(mic16_spatial_signature(325,0,32,ri)!=-1 || mic16_spatial_signature(0,32,32,ri)!=-1 ||
        mic16_spatial_signature(0,0,31,ri)!=-1)err++;n++;
    maps[0]=3.f;votes[0]=123.;
    if(mic16_spatial_votes(maps,levels,floors,votes,best)!=-1 || votes[0]!=123.)err++;n++;maps[0]=0;
    levels[0]=1e100;
    if(mic16_spatial_votes(maps,levels,floors,votes,best)!=-1 || votes[0]!=123.)err++;n++;levels[0]=0;
    if(mic16_spatial_candidates(votes,&pool,&work)!=-1)err++;n++;
    clusters[0].direction[0]=0.;clusters[1].direction[0]=10.-1e-7;clusters[2].direction[0]=10.+1e-7;
    clusters[0].seed_grid=0;clusters[1].seed_grid=1;clusters[2].seed_grid=2;
    if(mic16_spatial_pool_sorted(clusters,3,&pool) || pool.count!=2 || pool.items[1].seed_grid!=2)err++;n++;
    if(mic16_spatial_pool_sorted(clusters,326,&pool)!=-1)err++;n++;
    printf("SPATIAL_SELFTEST checks=%d errors=%d workspace_bytes=%u cluster_bytes=%u pool_bytes=%u\n",
        n,err,(unsigned int)sizeof(work),(unsigned int)sizeof(mic16_cluster),(unsigned int)sizeof(pool));
    return err;
}
int main(int argc,char **argv){
    void *in,*out;int rc=0,i;
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=4)return 20;
    in=fopen(argv[2],"rb");out=fopen(argv[3],"wb");if(!in || !out)return 21;
    if(!strcmp(argv[1],"project")){
        if(fread(phat,4,M16_SP_PHAT_FLOATS,in)!=M16_SP_PHAT_FLOATS || mic16_spatial_project(phat,maps,&work))return 22;
        if(fwrite(maps,4,M16_SP_MAP_FLOATS,out)!=M16_SP_MAP_FLOATS || old_scan(out))return 23;
    }else if(!strcmp(argv[1],"select")){
        if(fread(maps,4,M16_SP_MAP_FLOATS,in)!=M16_SP_MAP_FLOATS || fread(levels,8,480,in)!=480 || fread(floors,8,24,in)!=24)return 24;
        if(mic16_spatial_votes(maps,levels,floors,votes,best) || mic16_spatial_candidates(votes,&pool,&work))return 25;
        if(fwrite(votes,8,480*9,out)!=480*9 || fwrite(best,2,480,out)!=480 || save_pool(out))return 26;
    }else if(!strcmp(argv[1],"signature")){
        unsigned int indices[3];float ri[2];
        for(i=0;i<64;i++){
            if(fread(indices,4,3,in)!=3 || mic16_spatial_signature(indices[0],indices[1],indices[2],ri))return 27;
            if(fwrite(ri,4,2,out)!=2)return 28;
        }
    }else if(!strcmp(argv[1],"angles")){
        double a[4],result;
        for(i=0;i<12;i++){
            if(fread(a,8,4,in)!=4)return 27;
            result=mic16_spatial_angle(a[0],a[1],a[2],a[3]);
            if(fwrite(&result,8,1,out)!=1)return 28;
        }
    }else return 29;
    fclose(in);fclose(out);return rc;
}
