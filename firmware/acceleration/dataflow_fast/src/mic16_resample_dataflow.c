#include "mic16_dataflow_fast.h"
#include "mic16_cooperate.h"
#include "mic16_resample_tables.h"
#include <stdint.h>
#ifdef M16_DATAFLOW_ARM_WRAP
#define ORIGINAL __real_mic16_resample_channel
#else
#define ORIGINAL m16_original_resample_channel
#endif
extern int ORIGINAL(const float *,unsigned,unsigned,unsigned,double *);
static double batch[16778][16] __attribute__((aligned(64)));
/* 相位表只是原系数的重排，最旧输入优先，不做系数量化或重结合。 */
static double phases[256][49] __attribute__((aligned(64)));
static unsigned ready,active,valid,cached_n;static const float *cached_x;
static int overlap(const void *a,unsigned na,const void *b,unsigned nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
void m16_dataflow_begin(void){active=1;valid=0;cached_x=0;}
void m16_dataflow_end(void){active=0;valid=0;cached_x=0;}
static void init(void){unsigned p,k;for(p=0;p<256;p++)for(k=0;k<49;k++){
 unsigned h=p+(48-k)*256;phases[p][k]=h<=12500?m16_resample_h[h]:0.;}ready=1;}
int m16_resample_dataflow(const float *x,unsigned n,unsigned stride,unsigned ch,double *out){
 unsigned i,j,nout;
 if(!active || stride!=16 || (n!=40960 && n!=8192)){
  m16_dataflow_calls[4]++;return ORIGINAL(x,n,stride,ch,out);
 }
 if(!x || !out || ch>=16 || overlap(x,n*16u*4u,out,((n*256+624)/625)*8u))return -1;
 /* 完整批次输入先通过校验，不让无效的其它通道进入缓存。 */
 for(i=0;i<n;i++)if(!(x[i*16+ch]>=-1.f && x[i*16+ch]<=1.f))return -1;
 nout=(n*256+624)/625;
 if(!valid || cached_x!=x || cached_n!=n){
  for(i=0;i<n*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
  if(!ready)init();
  for(j=0;j<nout;j++){
   unsigned time=(j+10)*625,phase=time&255u,hi=time>>8;
   int lo=(int)hi-48,k;unsigned first=0,last=48;
   if(phase+48u*256u>12500u){lo++;first++;}
   if(lo<0){first+=(unsigned)-lo;lo=0;}
   if(hi>=n)last-=hi-n+1;
   if((j&127u)==0)M16_COOPERATE();
   double a0=0.;
   double a1=0.;
   double a2=0.;
   double a3=0.;
   double a4=0.;
   double a5=0.;
   double a6=0.;
   double a7=0.;
   double a8=0.;
   double a9=0.;
   double a10=0.;
   double a11=0.;
   double a12=0.;
   double a13=0.;
   double a14=0.;
   double a15=0.;
   /* 每通道保持k递增的49抽头累计；16通道共享每次系数载入与连续输入行。 */
   for(k=(int)first;k<=(int)last;k++){
    double h=phases[phase][k];const float *row=x+((unsigned)lo+(unsigned)k-first)*16;
    a0+=(double)row[0]*h;
    a1+=(double)row[1]*h;
    a2+=(double)row[2]*h;
    a3+=(double)row[3]*h;
    a4+=(double)row[4]*h;
    a5+=(double)row[5]*h;
    a6+=(double)row[6]*h;
    a7+=(double)row[7]*h;
    a8+=(double)row[8]*h;
    a9+=(double)row[9]*h;
    a10+=(double)row[10]*h;
    a11+=(double)row[11]*h;
    a12+=(double)row[12]*h;
    a13+=(double)row[13]*h;
    a14+=(double)row[14]*h;
    a15+=(double)row[15]*h;
   }
   batch[j][0]=a0;
   batch[j][1]=a1;
   batch[j][2]=a2;
   batch[j][3]=a3;
   batch[j][4]=a4;
   batch[j][5]=a5;
   batch[j][6]=a6;
   batch[j][7]=a7;
   batch[j][8]=a8;
   batch[j][9]=a9;
   batch[j][10]=a10;
   batch[j][11]=a11;
   batch[j][12]=a12;
   batch[j][13]=a13;
   batch[j][14]=a14;
   batch[j][15]=a15;
  }
  cached_x=x;cached_n=n;valid=1;m16_dataflow_calls[2]++;
 }else m16_dataflow_calls[3]++;
 for(j=0;j<nout;j++)out[j]=batch[j][ch];
 return (int)nout;
}
