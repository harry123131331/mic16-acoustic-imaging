#include "mic16_cooperate.h"
#include "mic16_real_fft.h"
#include "mic16_spectral_front.h"
extern double sin(double);
extern double cos(double);
extern double hypot(double,double);
extern double log10(double);
#define PI 3.14159265358979323846264338327950288
#define READY 0x4d313653u
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static int overlaps(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y ? y-x<na : x-y<nb;
}
static void fft(mic16_spectral_workspace *w){
 m16_real_fft_transform(w->fft_re,w->fft_im,w->twiddle_re,w->twiddle_im,4096,1);
}
int m16_spectral_process_real(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){
    int c,i,b;
    if(!x || !o || !w || w->initialized!=READY)return -1;
    if(overlaps(x,2048ul*16*4,o,sizeof(*o)) || overlaps(x,2048ul*16*4,w,sizeof(*w)) || overlaps(o,sizeof(*o),w,sizeof(*w)))return -1;
    for(i=0;i<2048*16;i++)if(!finite64(x[i]) || x[i]<-1.f || x[i]>1.f)return -1;
    m16_fft_fast_calls[0]++;
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
