#include "../../mic16_cooperate.h"
#include "mic16_extended.h"
#include "mic16_resample_tables.h"
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
int mic16_resample_channel(const float *x,unsigned int n,unsigned int stride,unsigned int ch,double *out){
    unsigned int j,i,nout;
    if(!x || !out || n<1 || n>40960 || stride<1 || stride>16 || ch>=stride)return -1;
    for(i=0;i<n;i++)if(!(x[i*stride+ch]>=-1.f && x[i*stride+ch]<=1.f))return -1;
    nout=(n*256+624)/625;
    for(j=0;j<nout;j++){
        if((j&127u)==0)M16_COOPERATE();
        /* 滤波系数前置625个零，按SciPy 1.17.1去除11个输出点；仅计算非零多相抽头，按最旧输入优先。 */
        int time=(int)((j+11)*625)-625;
        int lo=(time-12500+255)/256,hi=time/256,k;double s=0.;
        if(time-12500<0)lo=0;if(lo<0)lo=0;if(hi>=(int)n)hi=(int)n-1;
        for(k=lo;k<=hi;k++){
            int h=time-k*256;
            if(h>=0 && h<=12500)s+=(double)x[(unsigned int)k*stride+ch]*m16_resample_h[h];
        }
        out[j]=s;
    }
    return (int)nout;
}
int mic16_stft512_frame(const double *x,unsigned int n,unsigned int frame,mic16_extended_workspace *w){
    unsigned int i,j=0,bit,len,base,k;
    if(!x || !w || n<512 || n>16778 || frame>=(n+255)/256+1)return -1;
    for(i=0;i<n;i++)if(!finite64(x[i]))return -1;
    for(i=0;i<512;i++){
        int pos=(int)(frame*256+i)-256;
        w->fft_re[i]=(pos>=0 && pos<(int)n)?x[pos]*m16_periodic_hann512[i]:0.;w->fft_im[i]=0.;
    }
    for(i=1;i<512;i++){
        bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double t=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=t;
            t=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=t;}
    }
    for(len=2;len<=512;len<<=1){M16_COOPERATE();unsigned int half=len>>1,step=512/len;
        for(base=0;base<512;base+=len)for(k=0;k<half;k++){
            unsigned int a=base+k,b=a+half,q=k*step;
            double re=m16_twiddle_re[q]*w->fft_re[b]-m16_twiddle_im[q]*w->fft_im[b];
            double im=m16_twiddle_re[q]*w->fft_im[b]+m16_twiddle_im[q]*w->fft_re[b];
            double ar=w->fft_re[a],ai=w->fft_im[a];w->fft_re[a]=ar+re;w->fft_im[a]=ai+im;
            w->fft_re[b]=ar-re;w->fft_im[b]=ai-im;
        }
    }
    for(i=0;i<257;i++){w->spectrum[i*2]=w->fft_re[i];w->spectrum[i*2+1]=w->fft_im[i];}
    return 0;
}
