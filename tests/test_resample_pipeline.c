#include "mic16_resample_pipeline.h"
#include <stdint.h>
#include <stddef.h>
extern int printf(const char *,...),memcmp(const void *,const void *,size_t);
extern int m16_original_resample_channel(const float *,unsigned,unsigned,unsigned,double *);
extern int m16_original_resample_dataflow(const float *,unsigned,unsigned,unsigned,double *);
void m16_cooperate(void){}
unsigned m16_dataflow_calls[6];
static float x[40960];static double a[16778],b[16778];
int main(void){unsigned pattern,n,i,j,state=20261022u,checks=0,errors=0;union{float f;unsigned u;} v;
 for(pattern=0;pattern<7;pattern++)for(n=8192;n<=40960;n+=32768){
  for(i=0;i<n;i++){state=state*1664525u+1013904223u;v.u=pattern==4?(i&1?0x80000001u:1u):pattern==5?(i&1?0x80000000u:0u):0;
   x[i]=pattern==0?0.f:pattern==1?((int)(state>>16)-32768)*0x1p-15f:pattern==2?(i==0 || i==n-1?1.f:0.f):pattern==3?(i&1?-1.f:1.f):pattern==6?((int)(i%97)-48)*0x1p-6f:v.f;}
  {int na=m16_original_resample_channel(x,n,1,0,a),nb=m16_resample_pipeline(x,n,1,0,b);checks++;if(na!=nb || memcmp(a,b,na*8u)){printf("FIR_DIFF case=%u n=%u\n",pattern,n);errors++;}}
 }
 for(n=1;n<512;n+=17){int na=m16_original_resample_channel(x,n,1,0,a),nb=m16_resample_pipeline(x,n,1,0,b);checks++;errors+=na!=nb || memcmp(a,b,na*8u)!=0;}
 v.u=0x7fc00001u;x[5]=v.f;b[0]=123.;checks++;errors+=m16_resample_pipeline(x,40960,1,0,b)!=-1 || b[0]!=123.;
 printf("FOUR_OUTPUT_FIR_%s checks=%u errors=%u\n",errors?"FAIL":"PASS",checks,errors);return errors?1:0;
}
