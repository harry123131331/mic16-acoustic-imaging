/* 一般幅值范围走VFP平方根；极大、极小及非有限值走保留libm。
 * sqrt(a*a+b*b)与hypot不作全域逐位等价承诺；完整下游差异须独立核验。 */
#include <stdint.h>
extern double sqrt(double);
unsigned m16_magnitude_enabled,m16_magnitude_calls[2];
#ifdef M16_DATAFLOW_ARM_WRAP
#define ORIGINAL __real_hypot
#define ENTRY __wrap_hypot
#else
#define ORIGINAL hypot
#define ENTRY m16_hypot_bounded
#endif
extern double ORIGINAL(double,double);
int m16_magnitude_select(unsigned enabled){if(enabled>1)return -1;m16_magnitude_enabled=enabled;m16_magnitude_calls[0]=m16_magnitude_calls[1]=0;return 0;}
double ENTRY(double a,double b){
 union {double d;uint64_t u;} x,y,r;uint32_t hx,hy,m;
 if(!m16_magnitude_enabled)return ORIGINAL(a,b);
 x.d=a;y.d=b;hx=(uint32_t)(x.u>>32)&0x7fffffffu;hy=(uint32_t)(y.u>>32)&0x7fffffffu;m=hx>hy?hx:hy;
 /* 直接检查IEEE754指数：最大幅值[2^-400,2^401)，平方和不会上/下溢。
  * NaN/Inf指数自动落入原库；很小的一项被平方成0不影响此处模长精度。
  * 归一化的1e-8门附近保留原库，避免舍入改变PHAT是否活动。 */
 if(m>=0x26f00000u && m<0x59000000u){
  r.d=sqrt(a*a+b*b);
  if((uint32_t)(r.u>>32)-0x3e45798du>2u){m16_magnitude_calls[0]++;return r.d;}
 }
 if(a==0. && b==0.){m16_magnitude_calls[0]++;return 0.;}
 m16_magnitude_calls[1]++;return ORIGINAL(a,b);
}
