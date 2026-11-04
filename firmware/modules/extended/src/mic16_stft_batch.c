#include "../../mic16_cooperate.h"
/* FFT计算语句直接取自保留实现；只有同步批次入口改变校验位置。 */
#include "mic16_stft_batch.h"
#include "mic16_resample_tables.h"
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static int transform_validated(const double *x,unsigned int n,unsigned int frame,mic16_extended_workspace *w){
    unsigned int i,j=0,bit,len,base,k;
    for(i=0;i<512;i++){
        int pos=(int)(frame*256+i)-256;
        w->fft_re[i]=(pos>=0 && pos<(int)n)?x[pos]*m16_periodic_hann512[i]:0.;w->fft_im[i]=0.;
    }
    for(i=1;i<512;i++){
        bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double t=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=t;
            t=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=t;}
    }
    for(len=2;len<=512;len<<=1){unsigned int half=len>>1,step=512/len;
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

int mic16_stft512_visit(const double *x,unsigned n,unsigned frames,mic16_extended_workspace *w,
    mic16_stft_consumer consume,void *context){
    unsigned i,t;int rc;
    if(!w || x!=w->resampled || !consume || n<512 || n>16778 || !frames || frames>(n+255)/256+1)return -1;
    /* 所有有限性检查先完成；失败不调用消费者、不改FFT输出。 */
    for(i=0;i<n;i++)if(!finite64(x[i]))return -1;
    for(t=0;t<frames;t++){M16_COOPERATE();
        transform_validated(x,n,t,w);
        rc=consume(t,w->spectrum,context);if(rc)return rc;
    }
    return 0;
}
