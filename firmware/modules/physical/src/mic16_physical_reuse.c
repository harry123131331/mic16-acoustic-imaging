/* 来源：同目录原实现；只添加本次准备态与波束共享，原文件保留。 */
#include "mic16_physical.h"
#include "mic16_mic_positions.h"
#include "mic16_spatial.h"
extern double sin(double);
extern double cos(double);
extern double floor(double);
extern double ceil(double);
#define PI 3.14159265358979323846264338327950288
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
static double sum64(const double *a,int n){
    double s=-0.;int i,j;
    if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
    {double r[8];for(j=0;j<8;j++)r[j]=a[j];
     for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
     s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
     for(;i<n;i++)s+=a[i];return s;}
}
static int local(const double *v,const double *d,float *out,mic16_physical_workspace *w){
    unsigned int i,t,j,frame_bits=0,band_bits=0;
    for(i=0;i<480;i++)w->errors[i]=mic16_spatial_angle(d[0],d[1],v[i*9+4],v[i*9+5]);
    for(t=0;t<20;t++){
        double coh[24],contrast[24],angle[24];int n=0;unsigned int frame_count=0,bands=0;
        for(j=0;j<24;j++){
            i=t*24+j;if(w->errors[i]>10.)continue;
            coh[n]=v[i*9+7]>0.?v[i*9+7]:0.;contrast[n]=clip(v[i*9+8],0.,1.);angle[n]=w->errors[i];n++;
            band_bits|=1u<<(unsigned int)v[i*9+2];
        }
        if(n)frame_bits|=1u<<t;
        for(i=0;i<20;i++)frame_count+=(frame_bits>>i)&1;
        for(i=0;i<3;i++)bands+=(band_bits>>i)&1;
        out[t*88+80]=(float)((double)n/24.);out[t*88+81]=(float)((double)frame_count/(t+1));
        out[t*88+82]=(float)((double)bands/3.);
        out[t*88+83]=n?(float)(sum64(coh,n)/n):0.f;
        out[t*88+84]=n?(float)(sum64(contrast,n)/n):0.f;
        out[t*88+85]=n?(float)(sum64(angle,n)/n/10.):0.f;
    }
    return 0;
}
static void response(const float *lags,float *out,mic16_physical_workspace *w){
    int z,p,b,t;
    for(z=0;z<24;z++){
        int start=z*34,end=start+34,n;if(end>807)end=807;n=end-start;
        for(p=0;p<32;p++)for(b=0;b<n;b++){
            double phase=2.*PI*(double)lags[p]*(double)(32+start+b)/4096.;
            w->signature[(p*34+b)*2]=(float)cos(phase);w->signature[(p*34+b)*2+1]=(float)sin(phase);
        }
        for(t=0;t<20;t++){
            double acc=0.;
            for(p=0;p<32;p++)for(b=0;b<n;b++){
                int q=((t*807+start+b)*32+p)*2,s=(p*34+b)*2;
                acc+=(double)w->signature[s]*w->phat[q]+(double)w->signature[s+1]*w->phat[q+1];
            }
            out[t*88+z]=(float)acc/(float)(32*n);
        }
    }
}
int mic16_physical_features_reused(const float *x,const float *quiet,const double *d,unsigned int count,
                            const double *v,float *out,mic16_physical_workspace *w,
    const mic16_prepared_spectral *prepared,mic16_candidate_beam_sink sink,void *context){
    unsigned int i,t,z,c,k;
    if(!x || !quiet || !d || !v || !out || !w || count<1 || count>16)return -1;
    if(overlap(x,40960ul*16*4,quiet,8192ul*16*4) || overlap(x,40960ul*16*4,w,sizeof(*w)) ||
        overlap(quiet,8192ul*16*4,w,sizeof(*w)) || overlap(out,count*20ul*88*4,w,sizeof(*w)) ||
        overlap(x,40960ul*16*4,out,count*20ul*88*4) || overlap(quiet,8192ul*16*4,out,count*20ul*88*4) ||
        overlap(d,count*2ul*8,w,sizeof(*w)) || overlap(v,480ul*9*8,w,sizeof(*w)) ||
        overlap(d,count*2ul*8,out,count*20ul*88*4) || overlap(v,480ul*9*8,out,count*20ul*88*4))return -1;
    for(i=0;i<40960*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
    for(i=0;i<8192*16;i++)if(!(quiet[i]>=-1.f && quiet[i]<=1.f))return -1;
    for(i=0;i<count*2;i++)if(!finite64(d[i]) || d[i]<=-90. || d[i]>=90.)return -1;
    for(i=0;i<480;i++){
        const double *a=v+i*9;
        for(k=0;k<9;k++)if(!finite64(a[k]))return -1;
        if(a[0]!=(double)(i/24) || a[1]!=(double)(i%24) || a[2]<0. || a[2]>2. || a[2]!=(double)(int)a[2] ||
            a[4]<=-90. || a[4]>=90. || a[5]<=-90. || a[5]>=90. || a[6]<-2000. || a[6]>2000. ||
            a[7]<-2. || a[7]>2. || a[8]<0. || a[8]>4.)return -1;
    }
    if(prepared){
        if(prepared->pcm!=x || prepared->reference!=quiet || prepared->owner!=w ||
           w->spectral.initialized!=0x4d313653u || w->reference.count!=4 || w->reference.finalized!=1)return -1;
        for(i=0;i<20u*807u*32u*2u;i++)if(!finite64(w->phat[i]) || w->phat[i]<-1.000001f || w->phat[i]>1.000001f)return -1;
        for(i=0;i<20u*24u;i++)if(!finite64(w->levels[i]))return -1;
        for(i=0;i<24;i++)if(!finite64(w->reference.floor_db[i]))return -1;
    }else{
    mic16_spectral_init(&w->spectral);mic16_reference_init(&w->reference);
    for(t=0;t<4;t++){
        if(mic16_spectral_process(quiet+t*2048*16,&w->frame,&w->spectral) ||
            mic16_reference_add(&w->reference,w->frame.zone_level_db))return -2;
    }
    if(mic16_reference_finalize(&w->reference))return -2;
    for(t=0;t<20;t++){
        if(mic16_spectral_process(x+t*2048*16,&w->frame,&w->spectral))return -2;
        for(i=0;i<807*32*2;i++)w->phat[t*807*32*2+i]=w->frame.phat[i];
        for(z=0;z<24;z++)w->levels[t*24+z]=w->frame.zone_level_db[z];
    }
    }
    for(c=0;c<count;c++){
        float *o=out+c*20*88,arrival[16],lags[32];unsigned int guard,qguard;double qbands[4*32],r[4],floor;
        if(mic16_candidate_geometry(d+c*2,arrival,lags) ||
            mic16_candidate_beam(x,40960,d+c*2,w->beam,&guard) ||
            mic16_candidate_beam(quiet,8192,d+c*2,w->quiet_beam,&qguard))return -2;
        if(mic16_mono_bands(w->beam,20,w->bands,w->beam_level,w) ||
            mic16_mono_bands(w->quiet_beam,4,qbands,w->quiet_level,w))return -2;
        if(sink && sink(context,c,w->beam,40960))return -2;
        for(i=0;i<4;i++)r[i]=w->quiet_level[i];
        for(i=1;i<4;i++){double a=r[i];k=i;while(k>0 && r[k-1]>a){r[k]=r[k-1];k--;}r[k]=a;}
        floor=(r[1]+r[2])/2.;
        response(lags,o,w);local(v,d+c*2,o,w);
        for(t=0;t<20;t++){
            for(z=0;z<24;z++)o[t*88+24+z]=(float)(clip(w->levels[t*24+z]-w->reference.floor_db[z],-20.,40.)/20.);
            for(k=0;k<32;k++)o[t*88+48+k]=(float)(clip(w->bands[t*32+k],-100.,80.)/40.);
            o[t*88+86]=(float)(clip(w->beam_level[t]-floor,-20.,40.)/20.);
            o[t*88+87]=t==0?(float)((2048.-guard)/2048.):1.f;
        }
    }
    return 0;
}
