#include "../../mic16_cooperate.h"
#include "mic16_spectral_front.h"
extern double sin(double);
extern double cos(double);
extern double hypot(double,double);
extern double log10(double);
#define PI 3.14159265358979323846264338327950288
#define READY 0x4d313653u
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static int overlaps(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y ? y-x<na : x-y<nb;
}
static int pair_a(int p){return p<16?p:(p<24?p-16:p-24);}
static int pair_b(int p){return p<16?(p+1)%16:(p<24?p-8:p-20);}

void mic16_spectral_init(mic16_spectral_workspace *w){
    int i;
    if(!w)return;
    w->initialized=0;
    for(i=0;i<2048;i++){
        w->hann[i]=.5-.5*cos(2.*PI*i/2047.);
        w->twiddle_re[i]=cos(2.*PI*i/4096.);
        w->twiddle_im[i]=-sin(2.*PI*i/4096.);
    }
    w->initialized=READY;
}
static void fft(mic16_spectral_workspace *w){
    unsigned int i,j=0,bit,len,base,k;
    for(i=1;i<4096;i++){
        bit=2048;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j){double z=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=z;
            z=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=z;}
    }
    for(len=2;len<=4096;len<<=1){M16_COOPERATE();
        unsigned int half=len>>1,step=4096/len;
        for(base=0;base<4096;base+=len)for(k=0;k<half;k++){
            unsigned int a=base+k,b=a+half,t=k*step;
            double re=w->twiddle_re[t]*w->fft_re[b]-w->twiddle_im[t]*w->fft_im[b];
            double im=w->twiddle_re[t]*w->fft_im[b]+w->twiddle_im[t]*w->fft_re[b];
            double ar=w->fft_re[a],ai=w->fft_im[a];
            w->fft_re[a]=ar+re;w->fft_im[a]=ai+im;
            w->fft_re[b]=ar-re;w->fft_im[b]=ai-im;
        }
    }
}
int mic16_spectral_observe(mic16_spectral_frame *o){
    int b,c,p,z;
    if(!o)return -1;
    for(b=0;b<807*16*2;b++)if(!finite64(o->spectrum[b]) || o->spectrum[b]<-4096. || o->spectrum[b]>4096.)return -1;
    for(b=0;b<807;b++){
        double power=0;
        for(c=0;c<16;c++){
            double re=o->spectrum[(b*16+c)*2],im=o->spectrum[(b*16+c)*2+1];
            power+=re*re+im*im;
        }
        o->mean_channel_power[b]=power/16.;
        for(p=0;p<32;p++){
            int a=(b*16+pair_a(p))*2,bb=(b*16+pair_b(p))*2;
            double ar=o->spectrum[a],ai=o->spectrum[a+1],br=o->spectrum[bb],bi=o->spectrum[bb+1];
            double re=ar*br+ai*bi,im=ai*br-ar*bi,mag=hypot(re,im);
            o->phat[(b*32+p)*2]=mag>1e-8?(float)(re/mag):0.f;
            o->phat[(b*32+p)*2+1]=mag>1e-8?(float)(im/mag):0.f;
        }
    }
    for(z=0;z<24;z++){
        int start=z*34,end=start+34;double power=0;
        if(end>807)end=807;
        for(b=start;b<end;b++)power+=o->mean_channel_power[b];
        power/=(end-start);
        o->zone_level_db[z]=10.*log10(power>1e-20?power:1e-20);
    }
    return 0;
}
int mic16_spectral_process(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){
    int c,i,b;
    if(!x || !o || !w || w->initialized!=READY)return -1;
    if(overlaps(x,2048ul*16*4,o,sizeof(*o)) || overlaps(x,2048ul*16*4,w,sizeof(*w)) || overlaps(o,sizeof(*o),w,sizeof(*w)))return -1;
    for(i=0;i<2048*16;i++)if(!finite64(x[i]) || x[i]<-1.f || x[i]>1.f)return -1;
    for(c=0;c<16;c++){
        double mean=0;
        for(i=0;i<2048;i++)mean+=(double)x[i*16+c];
        mean/=2048.;
        for(i=0;i<2048;i++){w->fft_re[i]=((double)x[i*16+c]-mean)*w->hann[i];w->fft_im[i]=0;}
        for(i=2048;i<4096;i++){w->fft_re[i]=0;w->fft_im[i]=0;}
        fft(w);
        for(b=0;b<807;b++){
            o->spectrum[(b*16+c)*2]=w->fft_re[b+32];
            o->spectrum[(b*16+c)*2+1]=w->fft_im[b+32];
        }
    }
    return mic16_spectral_observe(o);
}
void mic16_reference_init(mic16_spectral_reference *r){
    int i,j;if(!r)return;
    r->count=0;r->finalized=0;
    for(j=0;j<24;j++){r->floor_db[j]=0;for(i=0;i<4;i++)r->level_db[i][j]=0;}
}
int mic16_reference_add(mic16_spectral_reference *r,const double *levels){
    int j;
    if(!r || !levels)return -1;
    if(r->count>=4 || r->finalized)return -2;
    for(j=0;j<24;j++)if(!finite64(levels[j]))return -1;
    for(j=0;j<24;j++)r->level_db[r->count][j]=levels[j];
    r->count++;
    return 0;
}
int mic16_reference_finalize(mic16_spectral_reference *r){
    int i,j,k;
    if(!r)return -1;
    if(r->count!=4 || r->finalized)return -2;
    for(j=0;j<24;j++){
        double a[4];for(i=0;i<4;i++)a[i]=r->level_db[i][j];
        for(i=1;i<4;i++){double v=a[i];k=i;while(k>0 && a[k-1]>v){a[k]=a[k-1];k--;}a[k]=v;}
        r->floor_db[j]=(a[1]+a[2])/2.;
    }
    r->finalized=1;
    return 0;
}
int mic16_spectral_quality(const double *levels,const mic16_spectral_reference *r,float *q){
    int j;
    if(!levels || !r || !q)return -1;
    if(r->count!=4 || !r->finalized)return -2;
    for(j=0;j<24;j++)if(!finite64(levels[j]) || !finite64(r->floor_db[j]))return -1;
    for(j=0;j<24;j++){
        q[j]=(float)(clip(levels[j]-r->floor_db[j],-20.,40.)/20.);
        q[j+24]=(float)(clip(levels[j],-100.,80.)/40.);
    }
    return 0;
}
int mic16_decode_q24_complex(unsigned long long u,float *ri){
    int re,im;
    if(!ri || (u>>52)!=0)return -1;
    re=(int)(u&0x3ffffffull);im=(int)((u>>26)&0x3ffffffull);
    if(re&0x2000000)re-=0x4000000;
    if(im&0x2000000)im-=0x4000000;
    ri[0]=(float)re/16777216.f;ri[1]=(float)im/16777216.f;
    return 0;
}
