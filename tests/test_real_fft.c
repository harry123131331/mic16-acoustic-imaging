#include "mic16_real_fft.h"
extern int printf(const char *,...);extern double sin(double),cos(double),fabs(double);
void m16_cooperate(void){}
static double r[4096],im[4096],a[4096],b[4096],wr[2048],wi[2048];
static double original[4096];
static void ref(unsigned n){unsigned i,j=0,bit;
 for(i=1;i<n;i++){bit=n/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){double x=a[i],y=b[i];a[i]=a[j];b[i]=b[j];a[j]=x;b[j]=y;}}
 m16_fft_stages(a,b,wr,wi,n,0);}
int main(void){unsigned ni,scenario,i,n,state,checks=0,errors=0;double maximum=0.,pi=3.14159265358979323846264338327950288;
 for(ni=0;ni<2;ni++){n=ni?4096:512;for(i=0;i<n/2;i++){wr[i]=cos(2.*pi*i/n);wi[i]=-sin(2.*pi*i/n);}
 for(scenario=0;scenario<8;scenario++){
  state=20261022u;
  for(i=0;i<n;i++){double v;state=state*1664525u+1013904223u;v=((int)(state>>16)-32768)*0x1p-15;
   if(scenario==0)v=0.;if(scenario==1)v=i==11?1.:0.;if(scenario==3)v=i&1?-1.:1.;
   if(scenario==4)v=sin(2.*pi*37.*i/n);if(scenario==5)v*=1e-20;
   if(scenario==6)v=.25;if(scenario==7)v=(i<n/2?v:0.);
   original[i]=a[i]=r[i]=v;b[i]=im[i]=0.;
  }
  ref(n);m16_real_fft_transform(r,im,wr,wi,n,0);
  for(i=0;i<n;i++){double e=fabs(r[i]-a[i]),f=fabs(im[i]-b[i]);if(f>e)e=f;if(e>maximum)maximum=e;checks+=2;errors+=e>2e-10;
   if(i<=n/2)printf("VALUE %u %u %u %.17g %.17g\n",n,scenario,i,r[i],im[i]);
  }
 }}
 printf("REAL_FFT_UNIT_%s checks=%u max=%g errors=%u\n",errors?"FAIL":"PASS",checks,maximum,errors);return errors?1:0;}
