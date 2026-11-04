#include "mic16_response_neon.h"
#include "mic16_cooperate.h"
#include <stdint.h>
extern double sin(double),cos(double);
#define PI 3.14159265358979323846264338327950288
static float frame_major[807*32][40] __attribute__((aligned(64)));
static const mic16_physical_workspace *owner;
unsigned m16_response_neon_mode,m16_response_neon_calls[3];
void m16_response_neon_begin(void){unsigned i;owner=0;for(i=0;i<3;i++)m16_response_neon_calls[i]=0;}
int m16_response_neon_select(unsigned mode){if(mode>1)return -1;m16_response_neon_mode=mode;m16_response_neon_begin();return 0;}
#ifdef __ARM_NEON
#include <arm_neon.h>
typedef float32x4_t v4;
static inline v4 dup4(float a){return vdupq_n_f32(a);}
static inline v4 add4(v4 a,v4 b){return vaddq_f32(a,b);}
static inline v4 mul4(v4 a,v4 b){return vmulq_f32(a,b);}
static inline void load_complex4(const float *p,v4 *re,v4 *im){float32x4x2_t v=vld2q_f32(p);*re=v.val[0];*im=v.val[1];}
static inline void store4(float *p,v4 a){vst1q_f32(p,a);}
#else
typedef float v4 __attribute__((vector_size(16)));typedef uint32_t u4 __attribute__((vector_size(16)));
static inline v4 clean4(v4 f){union{v4 f;u4 u;}v;u4 mask;v.f=f;mask=(u4)((v.u&(u4){0x7f800000u,0x7f800000u,0x7f800000u,0x7f800000u})==(u4){0,0,0,0});v.u=(v.u&~mask)|(v.u&(u4){0x80000000u,0x80000000u,0x80000000u,0x80000000u}&mask);return v.f;}
static inline v4 dup4(float a){return clean4((v4){a,a,a,a});}
static inline v4 add4(v4 a,v4 b){return clean4(a+b);}
static inline v4 mul4(v4 a,v4 b){return clean4(a*b);}
static inline void load_complex4(const float *p,v4 *re,v4 *im){*re=clean4((v4){p[0],p[2],p[4],p[6]});*im=clean4((v4){p[1],p[3],p[5],p[7]});}
static inline void store4(float *p,v4 a){__builtin_memcpy(p,&a,16);}
#endif

void m16_response_neon(const float *lags,float *out,mic16_physical_workspace *w){
 unsigned i,j,z,p,b,t;
 if(owner!=w){
  for(i=0;i<807u*32u;i+=32){M16_COOPERATE();for(t=0;t<20;t++)for(j=i;j<i+32;j++){
   frame_major[j][t*2]=w->phat[(t*807u*32u+j)*2];frame_major[j][t*2+1]=w->phat[(t*807u*32u+j)*2+1];
  }}owner=w;m16_response_neon_calls[0]++;
 }
 m16_response_neon_calls[1]++;
 for(z=0;z<24;z++){
  unsigned start=z*34,n=807-start;if(n>34)n=34;M16_COOPERATE();
  for(p=0;p<32;p++)for(b=0;b<n;b++){
   double phase=2.*PI*(double)lags[p]*(double)(32+start+b)/4096.;
   w->signature[(p*34+b)*2]=(float)cos(phase);w->signature[(p*34+b)*2+1]=(float)sin(phase);
  }
  for(t=0;t<20;t+=4){
   v4 stack[12],value;unsigned occupied=0,level,has=0;float values[4];
   for(p=0;p<32;p++)for(b=0;b<n;b++){
    v4 re,im;load_complex4(frame_major[(start+b)*32+p]+t*2,&re,&im);
    value=add4(mul4(dup4(w->signature[(p*34+b)*2]),re),mul4(dup4(w->signature[(p*34+b)*2+1]),im));
    /* 同大小的相邻区段合并；从低位补尾，等价于原相邻成对树。 */
    level=0;while(occupied&(1u<<level)){value=add4(stack[level],value);occupied&=~(1u<<level);level++;}
    stack[level]=value;occupied|=1u<<level;
   }
   for(level=0;level<12;level++)if(occupied&(1u<<level)){
    if(has)value=add4(stack[level],value);else{value=stack[level];has=1;}
   }
   store4(values,value);for(j=0;j<4;j++)out[(t+j)*88+z]=values[j]/(float)(32*n);
   m16_response_neon_calls[2]++;
  }
 }
}
