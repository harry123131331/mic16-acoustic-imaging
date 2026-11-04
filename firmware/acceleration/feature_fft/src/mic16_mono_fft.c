#include "mic16_cooperate.h"
#include "mic16_fft_fast.h"
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
    unsigned int i,j=0,bit;
    for(i=1;i<4096;i++){
        bit=2048;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double q=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=q;
            q=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=q;}
    }
    m16_fft_stages(w->fft_re,w->fft_im,w->twiddle_re,w->twiddle_im,4096,1);

}
/* 按原FP64边界表达式生成的32个连续区间；只去掉重复扫描，求和保持。 */
static const unsigned m16_band_start[32]={0,25,50,76,101,126,151,177,202,227,252,278,303,328,353,378,404,429,454,479,505,530,555,580,606,631,656,681,706,732,757,782};
static const unsigned m16_band_count[32]={25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25,26,25,25,25,26,25,25,25,25,26,25,25,25};
int mic16_mono_bands_fft(const float *beam,unsigned int frames,double *bands,double *levels,mic16_physical_workspace *work){
    unsigned int t,i,k,b;
    mic16_spectral_workspace *w;
    if(!beam || !bands || !levels || !work || (frames!=4 && frames!=20))return -1;
    m16_feature_fast_calls[2]++;m16_fft_fast_calls[1]++;w=&work->spectral;if(w->initialized!=0x4d313653u)return -1;
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
            double power;int start=m16_band_start[k],count=m16_band_count[k];
            power=sum(work->powers+start,count)/count;
            bands[t*32+k]=10.*log10(power>1e-20?power:1e-20);
        }
    }
    return 0;
}
