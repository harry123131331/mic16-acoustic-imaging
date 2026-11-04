#include "../../mic16_cooperate.h"
#include "mic16_physical.h"
extern double log10(double);
/* 单通道版本沿用独立核验过的基2FFT参考；使用同样由调用方提供的Hann窗和旋转因子，原参考源码保留。 */
static double sum(const double *a,int n){
    int i,j;double s=-0.;
    if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
    if(n<=128){double r[8];for(j=0;j<8;j++)r[j]=a[j];
        for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
        s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
        for(;i<n;i++)s+=a[i];return s;}
    i=n/2/8*8;return sum(a,i)+sum(a+i,n-i);
}
static void fft(mic16_spectral_workspace *w){
    unsigned int i,j=0,bit,len,base,k;
    for(i=1;i<4096;i++){
        bit=2048;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double q=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=q;
            q=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=q;}
    }
    for(len=2;len<=4096;len<<=1){M16_COOPERATE();unsigned int half=len>>1,step=4096/len;
        for(base=0;base<4096;base+=len)for(k=0;k<half;k++){
            unsigned int a=base+k,b=a+half,t=k*step;
            double re=w->twiddle_re[t]*w->fft_re[b]-w->twiddle_im[t]*w->fft_im[b];
            double im=w->twiddle_re[t]*w->fft_im[b]+w->twiddle_im[t]*w->fft_re[b];
            double ar=w->fft_re[a],ai=w->fft_im[a];
            w->fft_re[a]=ar+re;w->fft_im[a]=ai+im;w->fft_re[b]=ar-re;w->fft_im[b]=ai-im;
        }
    }
}
int mic16_mono_bands(const float *beam,unsigned int frames,double *bands,double *levels,mic16_physical_workspace *work){
    unsigned int t,i,k,b;
    mic16_spectral_workspace *w;
    if(!beam || !bands || !levels || !work || (frames!=4 && frames!=20))return -1;
    w=&work->spectral;if(w->initialized!=0x4d313653u)return -1;
    for(i=0;i<frames*2048;i++)if(!(beam[i]>=-1.f && beam[i]<=1.f))return -1;
    for(t=0;t<frames;t++){M16_COOPERATE();
        double mean,energy;
        for(i=0;i<2048;i++){double x=beam[t*2048+i];w->fft_re[i]=x;work->scratch[i]=x*x;}
        mean=sum(w->fft_re,2048)/2048.;energy=sum(work->scratch,2048)/2048.;
        levels[t]=10.*log10(energy>1e-20?energy:1e-20);
        for(i=0;i<2048;i++){w->fft_re[i]=(w->fft_re[i]-mean)*w->hann[i];w->fft_im[i]=0.;}
        for(i=2048;i<4096;i++){w->fft_re[i]=0.;w->fft_im[i]=0.;}
        fft(w);
        for(b=0;b<807;b++){double re=w->fft_re[b+32],im=w->fft_im[b+32];work->powers[b]=re*re+im*im;}
        for(k=0;k<32;k++){
            double lo=300.+240.625*k,hi=300.+240.625*(k+1),power;int start=-1,count=0;
            for(b=0;b<807;b++){double f=(b+32)*(39062.5/4096.);
                if(f>=lo && (k==31?f<=hi:f<hi)){if(start<0)start=(int)b;count++;}}
            if(start<0 || count==0)return -2;
            power=sum(work->powers+start,count)/count;
            bands[t*32+k]=10.*log10(power>1e-20?power:1e-20);
        }
    }
    return 0;
}
