#ifndef MIC16_FFT_FAST_H
#define MIC16_FFT_FAST_H
#include "mic16_feature_fast.h"
#include "mic16_stft_batch.h"
extern unsigned m16_fft_fast_mode,m16_fft_fast_calls[3];
int m16_fft_fast_select(unsigned mode);
int mic16_spectral_process_fft(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
int mic16_mono_bands_fft(const float *,unsigned,double *,double *,mic16_physical_workspace *);
int mic16_stft512_visit_fft(const double *,unsigned,unsigned,mic16_extended_workspace *,mic16_stft_consumer,void *);
/* 四个蝶形互不共享写地址；每个蝶形保持原来四次乘法及加减次序。
 * 数组来自同一工作区的不同成员，restrict不跨调用或回调持有。 */
static inline __attribute__((always_inline)) void m16_butterfly(double *restrict r,double *restrict im,
 const double *restrict wr,const double *restrict wi,unsigned a,unsigned b,unsigned t){
 double re=wr[t]*r[b]-wi[t]*im[b];
 double ii=wr[t]*im[b]+wi[t]*r[b];
 double ar=r[a],ai=im[a];
 r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;
}
static inline __attribute__((always_inline)) void m16_fft_stages(double *restrict r,double *restrict im,
 const double *restrict wr,const double *restrict wi,unsigned n,unsigned cooperate){
 unsigned len,base,k;
 for(len=2;len<=n;len<<=1){
  unsigned half=len>>1,step=n/len;
  if(cooperate){M16_COOPERATE();}
  for(base=0;base<n;base+=len){
   for(k=0;k+3<half;k+=4){
    unsigned a=base+k,b=a+half,t=k*step;
    m16_butterfly(r,im,wr,wi,a,b,t);
    m16_butterfly(r,im,wr,wi,a+1,b+1,t+step);
    m16_butterfly(r,im,wr,wi,a+2,b+2,t+step*2);
    m16_butterfly(r,im,wr,wi,a+3,b+3,t+step*3);
   }
   for(;k<half;k++)m16_butterfly(r,im,wr,wi,base+k,base+k+half,k*step);
  }
 }
}
#endif
