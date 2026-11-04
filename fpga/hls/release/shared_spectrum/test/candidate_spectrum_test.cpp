/* 现用模式4逐位比较完整原核，另核对专用核对非现用模式的明确拒绝。 */
#include "shared_spectrum.h"
#include <math.h>
#include <stdio.h>
#include <vector>
extern int m16_original_sp_shared(ap_uint<64> *,unsigned,unsigned,unsigned,unsigned,unsigned);
static unsigned long long compared=0;
static unsigned seed=20261027u;
static float value(unsigned kind,unsigned c,unsigned i){
 seed=1664525u*seed+1013904223u;
 float noise=(float)((int)(seed>>8)-8388608)*(1.0f/33554432.0f);
 switch(kind){
 case 0:return 0.f;
 case 1:return i==31u?.25f:0.f;
 case 2:return (float)(.2*sin(6.2831853071795864769*800.*i/39062.5));
 case 3:return noise;
 case 4:return noise*1.e-30f;
 case 5:return (i&1u)?-.25f:.25f;
 case 6:return i%13u==0u?-.0f:noise*.001f;
 default:return (float)(.15*sin(.03*i+.1*c)+.05*cos(.017*i));
 }
}
static ap_uint<64> pair(float a,float b){
 union {float f;unsigned u;} x,y;x.f=a;y.f=b;
 ap_uint<64> word=0;word.range(31,0)=x.u;word.range(63,32)=y.u;return word;
}
int main(int argc,char **argv){
 // C仿真保留24组全组合；RTL用单通道脉冲和16通道随机覆盖边界。
 const bool rtl_boundaries=argc>1;
 const unsigned count=80944,input=128,output=32768;
 unsigned cases=0;
 for(unsigned kind=0;kind<8;kind++)for(unsigned step=0;step<3;step++){
  if(rtl_boundaries&&!((kind==1u&&step==0u)||(kind==3u&&step==2u)))continue;
  unsigned active=step==0?1u:step==1?4u:16u;
  std::vector<ap_uint<64> > a(count),b(count);
  for(unsigned i=0;i<count;i++)a[i]=((ap_uint<64>)0x6d163c82u<<32)|i;
  for(unsigned c=0;c<active;c++)for(unsigned i=0;i<1024;i++)a[input+c*1024+i]=pair(value(kind,c,2*i),value(kind,c,2*i+1));
  b=a;
  int old=m16_original_sp_shared(&a[0],input,output,2048,4,active);
  int current=m16_sp_shared(&b[0],input,output,2048,4,active);
  if(old||current){printf("RETURN_FAIL kind=%u active=%u old=%d new=%d\n",kind,active,old,current);return 1;}
  for(unsigned i=0;i<count;i++){
   if(a[i]!=b[i]){printf("WORD_FAIL kind=%u active=%u at=%u\n",kind,active,i);return 2;}
   compared++;
  }
  cases++;
 }
 std::vector<ap_uint<64> > a(count),before(count);
 for(unsigned i=0;i<count;i++)a[i]=0x51aa31bb;
 before=a;
 for(unsigned mode=0;mode<2;mode++){
  if(m16_sp_shared(&a[0],input,output,2048,mode,1)!=-1)return 3;
  for(unsigned i=0;i<count;i++)if(a[i]!=before[i])return 4;
 }
 printf("MODE4_OLD_NEW_BIT_EXACT cases=%u compared_words=%llu inactive_modes_rejected=2\n",cases,compared);
 return 0;
}
