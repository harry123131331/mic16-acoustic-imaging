#include "mic16_resample_pipeline.h"
#include "mic16_cooperate.h"
#include "mic16_resample_tables.h"
#include <stdint.h>
#ifdef M16_RESAMPLE_PIPELINE_ARM_WRAP
#define ORIGINAL __real_m16_resample_dataflow
#define ENTRY __wrap_m16_resample_dataflow
#else
#define ORIGINAL m16_original_resample_dataflow
#define ENTRY m16_resample_dataflow
#endif
extern int ORIGINAL(const float *,unsigned,unsigned,unsigned,double *);
static double phase[256][49] __attribute__((aligned(64)));static unsigned ready;
unsigned m16_resample_pipeline_enabled,m16_resample_pipeline_calls[4];
void m16_resample_pipeline_clear_tables(void){ready=0;}
int m16_resample_pipeline_select(unsigned mode){unsigned i;if(mode>1)return -1;m16_resample_pipeline_enabled=mode;for(i=0;i<4;i++)m16_resample_pipeline_calls[i]=0;return 0;}
static void init(void){unsigned p,k;for(p=0;p<256;p++)for(k=0;k<49;k++){unsigned h=p+(48-k)*256;phase[p][k]=h<=12500?m16_resample_h[h]:0.;}ready=1;}
static double boundary(const float *x,unsigned n,unsigned j){
 int time=(int)((j+10)*625),lo=(time-12500+255)/256,hi=time/256,k;double s=0.;
 if(time-12500<0)lo=0;if(lo<0)lo=0;if(hi>=(int)n)hi=(int)n-1;
 for(k=lo;k<=hi;k++){int h=time-k*256;if(h>=0 && h<=12500)s+=(double)x[k]*m16_resample_h[h];}return s;
}
/* 每个输出仍从最旧到最新按原49抽头做FP64加法；四个独立和交错执行，
 * 不分拆单个和、不改系数、不更改舍入、不使用融合乘加。 */
static int calculate(const float *restrict x,unsigned n,double *restrict out){
 unsigned j,nout=(n*256+624)/625;
 if(!ready)init();
 for(j=0;j+3<nout;j+=4){
  unsigned t0=(j+10)*625,t1=t0+625,t2=t1+625,t3=t2+625;
  unsigned p0=t0&255u,p1=t1&255u,p2=t2&255u,p3=t3&255u;
  int lo0=(int)(t0>>8)-48,lo1=(int)(t1>>8)-48,lo2=(int)(t2>>8)-48,lo3=(int)(t3>>8)-48;
  if((j&127u)==0)M16_COOPERATE();
  if(lo0>=0 && (t3>>8)<n){
   unsigned k;double a0=0.,a1=0.,a2=0.,a3=0.;
   const double *h0=phase[p0],*h1=phase[p1],*h2=phase[p2],*h3=phase[p3];
   const float *x0=x+lo0,*x1=x+lo1,*x2=x+lo2,*x3=x+lo3;
   if(p0<=212u)a0+=(double)x0[0]*h0[0];if(p1<=212u)a1+=(double)x1[0]*h1[0];
   if(p2<=212u)a2+=(double)x2[0]*h2[0];if(p3<=212u)a3+=(double)x3[0]*h3[0];
   for(k=1;k<=48;k++){
    a0+=(double)x0[k]*h0[k];a1+=(double)x1[k]*h1[k];
    a2+=(double)x2[k]*h2[k];a3+=(double)x3[k]*h3[k];
   }
   out[j]=a0;out[j+1]=a1;out[j+2]=a2;out[j+3]=a3;m16_resample_pipeline_calls[1]++;
  }else{unsigned k;for(k=0;k<4;k++)out[j+k]=boundary(x,n,j+k);m16_resample_pipeline_calls[2]+=4;}
 }
 for(;j<nout;j++){out[j]=boundary(x,n,j);m16_resample_pipeline_calls[2]++;}
 m16_resample_pipeline_calls[0]++;return (int)nout;
}
int m16_resample_pipeline(const float *x,unsigned n,unsigned stride,unsigned ch,double *out){
 unsigned i,nout;uintptr_t a=(uintptr_t)x,b=(uintptr_t)out;
 if(!x || !out || stride!=1 || ch || (n!=40960 && n!=8192)){m16_resample_pipeline_calls[3]++;return ORIGINAL(x,n,stride,ch,out);}
 nout=(n*256+624)/625;
 if((a<=b && b-a<n*4u) || (b<a && a-b<nout*8u)){m16_resample_pipeline_calls[3]++;return ORIGINAL(x,n,stride,ch,out);}
 for(i=0;i<n;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
 return calculate(x,n,out);
}
int ENTRY(const float *x,unsigned n,unsigned s,unsigned c,double *out){return m16_resample_pipeline_enabled?m16_resample_pipeline(x,n,s,c,out):ORIGINAL(x,n,s,c,out);}
