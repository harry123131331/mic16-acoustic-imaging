#include "mic16_response_recurrence.h"
#include <stdint.h>
extern double sin(double),cos(double);
#define PI 3.14159265358979323846264338327950288
static double rotor[32][2];
unsigned m16_response_recurrence_mode,m16_response_recurrence_calls[4];
int m16_response_recurrence_select(unsigned mode){unsigned i;if(mode>1)return -1;m16_response_recurrence_mode=mode;for(i=0;i<4;i++)m16_response_recurrence_calls[i]=0;return 0;}
static int rounding_safe(double x,float *value){
 union{float f;uint32_t u;} q;union{double d;uint64_t u;} half,diff;uint32_t bits,exponent;
 q.f=(float)x;bits=q.u&0x7fffffffu;
 if(bits<0x38000000u || bits>0x3f800000u)return 0;
 exponent=(bits>>23)+872u-((bits&0x007fffffu)==0);
 half.u=(uint64_t)exponent<<52;diff.d=x-(double)q.f;diff.u&=UINT64_C(0x7fffffffffffffff);
 if(half.d-diff.d<=0x1p-40)return 0;*value=q.f;return 1;
}

/* 每个候选重新准备，禁止用浮点指针地址充当跨候选的缓存键。 */
void m16_response_recurrence_begin(const float *lags){
 unsigned p;for(p=0;p<32;p++){double step=2.*PI*(double)lags[p]/4096.;rotor[p][0]=cos(step);rotor[p][1]=sin(step);}m16_response_recurrence_calls[0]++;
}
void m16_response_recurrence_block(const float *lags,unsigned start,unsigned n,float *out){
 unsigned p,b;
 for(p=0;p<32;p++){
  double lag=(double)lags[p],phase=2.*PI*lag*(double)start/4096.;
  double re=cos(phase),im=sin(phase),cr=rotor[p][0],ci=rotor[p][1];
  out[p*34*2]=(float)re;out[p*34*2+1]=(float)im;m16_response_recurrence_calls[1]++;
  for(b=1;b<n;b++){
   double nr=re*cr-im*ci,ni=re*ci+im*cr;float r,i;
   if(lag==0.){phase=2.*PI*lag*(double)(start+b)/4096.;nr=1.;ni=phase;r=1.f;i=(float)phase;}
   else if(!(rounding_safe(nr,&r) && rounding_safe(ni,&i))){
    phase=2.*PI*lag*(double)(start+b)/4096.;nr=cos(phase);ni=sin(phase);r=(float)nr;i=(float)ni;m16_response_recurrence_calls[2]++;
   }
   out[(p*34+b)*2]=r;out[(p*34+b)*2+1]=i;re=nr;im=ni;m16_response_recurrence_calls[3]++;
  }
 }
}
