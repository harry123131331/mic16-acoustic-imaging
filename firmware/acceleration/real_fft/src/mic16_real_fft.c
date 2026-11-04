#include "mic16_real_fft.h"
unsigned m16_real_fft_mode,m16_real_fft_calls[4];
int m16_real_fft_select(unsigned mode){unsigned i;if(mode>3)return -1;m16_real_fft_mode=mode;for(i=0;i<4;i++)m16_real_fft_calls[i]=0;return 0;}
/* 将偶数样本装入实部、奇数样本装入虚部，前向原位压缩不会覆盖未读输入。 */
void m16_real_fft_transform(double *r,double *im,const double *wr,const double *wi,unsigned n,unsigned cooperate){
 unsigned i,j=0,bit,len,base,k,h=n/2;
 for(i=0;i<h;i++){double a=r[2*i],b=r[2*i+1];r[i]=a;im[i]=b;}
 for(i=1;i<h;i++){
  bit=h/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;
  if(i<j){double a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}
 }
 for(len=2;len<=h;len<<=1){unsigned half=len/2,step=n/len;
  if(cooperate){M16_COOPERATE();}
  for(base=0;base<h;base+=len){
   for(k=0;k+3<half;k+=4){unsigned a=base+k,b=a+half,t=k*step;
    m16_butterfly(r,im,wr,wi,a,b,t);m16_butterfly(r,im,wr,wi,a+1,b+1,t+step);
    m16_butterfly(r,im,wr,wi,a+2,b+2,t+2*step);m16_butterfly(r,im,wr,wi,a+3,b+3,t+3*step);
   }
   for(;k<half;k++)m16_butterfly(r,im,wr,wi,base+k,base+k+half,k*step);
  }
 }
 /* 同时恢复k与h-k，先读取两端，再写回；不需要第二份FFT缓冲区。 */
 {double a=r[0],b=im[0];r[0]=a+b;im[0]=0.;r[h]=a-b;im[h]=0.;}
 for(k=1;k<=h/2;k++){
  unsigned p=h-k;double a=r[k],b=im[k],c=r[p],d=im[p];
  double er=(a+c)*.5,ei=(b-d)*.5,orr=(b+d)*.5,oi=(c-a)*.5;
  double x=er+wr[k]*orr-wi[k]*oi,y=ei+wr[k]*oi+wi[k]*orr;
  double u=er+wr[p]*orr+wi[p]*oi,v=-ei-wr[p]*oi+wi[p]*orr;
  r[k]=x;im[k]=y;if(p!=k){r[p]=u;im[p]=v;}
 }
 /* 消费者仅使用非负频率；补齐共轭部分使本函数也能独立全谱核验。 */
 for(k=1;k<h;k++){r[n-k]=r[k];im[n-k]=-im[k];}
 m16_real_fft_calls[n==4096?0:1]++;
}
