#include "mic16_response_dataflow.h"
#include <stdint.h>
extern int printf(const char *,...);extern void retained_response_unit(const float *,float *,mic16_physical_workspace *);extern long clock(void);
static mic16_physical_workspace w[2];static float a[20*88],b[20*88],lags[32];
void m16_cooperate(void){}
static unsigned state=20261022u;static unsigned next(void){state=state*1664525u+1013904223u;return state;}
static unsigned bits(float x){union{float f;unsigned u;}v;v.f=x;return v.u;}
static unsigned errors,checks,signature_checks;static unsigned hash(const float *p,unsigned n){unsigned i,h=2166136261u;for(i=0;i<n;i++)h=(h^bits(p[i]))*16777619u;return h;}
static void prepare(unsigned pattern){unsigned i;for(i=0;i<20*807*32*2;i++){
 union{float f;unsigned u;}v;v.u=pattern==0?(i&1?0x80000000u:0):pattern==1?((i&1?0x80000000u:0)|1u):pattern==2?(i&1?0xbf800000u:0x3f800000u):0;
 if(pattern>=3)v.f=((int)(next()>>16)-32768)*0x1p-15f;w[0].phat[i]=w[1].phat[i]=v.f;
}for(i=0;i<20*88;i++)a[i]=b[i]=-123.f;for(i=0;i<32*34*2;i++)w[0].signature[i]=w[1].signature[i]=.123f;}
int main(void){unsigned pattern,c,i,t,z;long s,e;
 for(pattern=0;pattern<6;pattern++){
  prepare(pattern);m16_response_dataflow_begin();
  for(c=0;c<16;c++){
   for(i=0;i<32;i++){union{float f;unsigned u;}v;v.u=((i+c)&1)?0x80000000u:0;
    lags[i]=pattern==0?v.f:pattern==1?(i&1?-20.f:20.f):((int)(next()>>16)-32768)*(20.f/32768.f);}
   retained_response_unit(lags,a,w);m16_response_dataflow(lags,b,w+1);
   for(t=0;t<20;t++)for(z=0;z<88;z++){checks++;errors+=bits(a[t*88+z])!=bits(b[t*88+z]);}
   for(i=0;i<32*34*2;i++){signature_checks++;errors+=bits(w[0].signature[i])!=bits(w[1].signature[i]);}
  }
  errors+=m16_response_dataflow_calls[0]!=1 || m16_response_dataflow_calls[1]!=16 || m16_response_dataflow_calls[2]!=16*24*5;
 }
 /* 缓冲区被覆盖但地址未变时，显式新窗口必须重建；不允许跨窗命中。 */
 prepare(5);m16_response_dataflow_begin();retained_response_unit(lags,a,w);m16_response_dataflow(lags,b,w+1);
 for(i=0;i<20*88;i++){checks++;errors+=bits(a[i])!=bits(b[i]);}
 errors+=m16_response_dataflow_calls[0]!=1;errors+=m16_response_dataflow_select(2)!=-1;
 printf("RESPONSE_UNIT checks=%u signature_checks=%u errors=%u hash=%u\n",checks,signature_checks,errors,hash(b,20*88));
 if(errors)return 1;
 /* 同一组16候选，各路径首个候选包含转置，不隐藏准备成本。 */
 for(pattern=0;pattern<7;pattern++)for(c=0;c<2;c++){
  unsigned mode=pattern&1?1-c:c;m16_response_dataflow_begin();s=clock();
  for(i=0;i<16;i++){unsigned p;for(p=0;p<32;p++)lags[p]=((int)p-16)*(float)(i+1)/16.f;if(mode)m16_response_dataflow(lags,b,w+1);else retained_response_unit(lags,b,w+1);}
  e=clock();printf("RESPONSE_HOST rep=%u mode=%u ticks=%ld hash=%u transposes=%u\n",pattern,mode,e-s,hash(b,20*88),m16_response_dataflow_calls[0]);
 }
 return 0;
}