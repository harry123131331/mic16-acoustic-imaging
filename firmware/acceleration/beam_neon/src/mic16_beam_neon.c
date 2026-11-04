#include "mic16_beam_neon.h"
#include "mic16_cooperate.h"
#include <stdint.h>
extern double floor(double),ceil(double);
extern int m16_previous_beam_tiles_entry(const float *,unsigned,const double *,float *,unsigned *);
#ifdef M16_BEAM_NEON_ARM_WRAP
#define ENTRY __wrap_mic16_candidate_beam_phase_fast
#else
#define ENTRY mic16_candidate_beam_phase_fast
#endif
static float observed[16][40960] __attribute__((aligned(64)));
static float reference[16][8192] __attribute__((aligned(64)));
static const float *keys[2];static unsigned valid[2],active;
unsigned m16_beam_neon_mode,m16_beam_neon_calls[6];
void m16_beam_neon_begin(void){active=1;valid[0]=valid[1]=0;keys[0]=keys[1]=0;}
void m16_beam_neon_end(void){active=0;valid[0]=valid[1]=0;keys[0]=keys[1]=0;}
int m16_beam_neon_select(unsigned mode){unsigned i;if(mode>2)return -1;m16_beam_neon_mode=mode;m16_beam_neon_end();for(i=0;i<6;i++)m16_beam_neon_calls[i]=0;return 0;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;}
static inline float clean(float f){union{float f;uint32_t u;}v;v.f=f;if((v.u&0x7f800000u)==0)v.u&=0x80000000u;return v.f;}
#ifdef __ARM_NEON
#include <arm_neon.h>
typedef float32x4_t v4;
static inline v4 load4(const float *p){return vld1q_f32(p);}
static inline void store4(float *p,v4 v){vst1q_f32(p,v);}
static inline v4 dup4(float v){return vdupq_n_f32(clean(v));}
static inline v4 add4(v4 a,v4 b){return vaddq_f32(a,b);}
static inline v4 sub4(v4 a,v4 b){return vsubq_f32(a,b);}
static inline v4 mul4(v4 a,v4 b){return vmulq_f32(a,b);}
#else
/* 宿主机也执行四路向量，只用整数掩码模拟ARMv7 NEON的非正规数归零。
 * 与此前逐标量模拟分开记录；它仍然不是ARM周期模型。 */
typedef float v4 __attribute__((vector_size(16)));
typedef uint32_t u4 __attribute__((vector_size(16)));
static inline v4 clean4(v4 f){union{v4 f;u4 u;}v;u4 e,mask;v.f=f;e=v.u&(u4){0x7f800000u,0x7f800000u,0x7f800000u,0x7f800000u};mask=(u4)(e==(u4){0,0,0,0});v.u=(v.u&~mask)|(v.u&(u4){0x80000000u,0x80000000u,0x80000000u,0x80000000u}&mask);return v.f;}
static inline v4 load4(const float *p){v4 v;__builtin_memcpy(&v,p,16);return clean4(v);}
static inline void store4(float *p,v4 v){__builtin_memcpy(p,&v,16);}
static inline v4 dup4(float v){v=clean(v);return (v4){v,v,v,v};}
static inline v4 add4(v4 a,v4 b){return clean4(a+b);}
static inline v4 sub4(v4 a,v4 b){return clean4(a-b);}
static inline v4 mul4(v4 a,v4 b){return clean4(a*b);}
#endif
static float *prepare(const float *x,unsigned n){
 unsigned slot=n==8192,i,c,j,step=n;float *a=n==8192?&reference[0][0]:&observed[0][0];
 if(valid[slot] && keys[slot]==x)return a;
 valid[slot]=0;
 /* 先验证整块，非法输入不留下可复用的部分缓存。 */
 for(i=0;i<n*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return 0;
 for(i=0;i<n;i+=16){if(!(i&127u))M16_COOPERATE();for(j=0;j<16;j++)for(c=0;c<16;c++)a[c*step+i+j]=x[(i+j)*16+c];}
 keys[slot]=x;valid[slot]=1;m16_beam_neon_calls[slot]++;return a;
}
static inline float exact1(const float *p,double f){double a=p[0];if(f!=0.)a+=((double)p[1]-a)*f;return (float)a;}
static inline float approximate1(const float *p,float f){float a=clean(p[0]);return f==0.f?a:clean(a+clean(clean(clean(p[1])-a)*f));}
static inline v4 approximate4(const float *p,float f){v4 a=load4(p);return f==0.f?a:add4(a,mul4(sub4(load4(p+1),a),dup4(f)));}
int m16_beam_neon(const float *restrict x,unsigned n,const double *d,float *restrict out,unsigned *guard){
 float arrival[16],lags[32],fraction32[16],*planar;double fraction[16],minshift=0.;int offset[16];unsigned i,c,g;
 if(!x || !out || !guard || (n!=8192 && n!=40960) || mic16_candidate_geometry(d,arrival,lags))return -1;
 if(overlap(x,(unsigned long)n*16*4,out,(unsigned long)n*4))return -1;
 if(!active){m16_beam_neon_calls[5]++;return m16_previous_beam_tiles_entry(x,n,d,out,guard);}
 for(c=0;c<16;c++){
  double s=(double)arrival[c]-10.,scaled;
  if(s>0.)return -1;if(s<minshift)minshift=s;
  if(s< -40960.)goto fallback;scaled=s*0x1p37;
  if(scaled!=(double)(int64_t)scaled)goto fallback;
  offset[c]=(int)floor(s);fraction[c]=s-offset[c];fraction32[c]=clean((float)fraction[c]);
 }
 g=(unsigned)ceil(-minshift)+2;planar=prepare(x,n);if(!planar)return -1;
 m16_beam_neon_calls[m16_beam_neon_mode==2?3:2]++;m16_feature_fast_calls[0]++;
 M16_COOPERATE();for(i=0;i<g && i<n;i++)out[i]=0.f;
 if(m16_beam_neon_mode==2){
  for(;i+3<n;i+=4){v4 r[8];if((i&127u)<4u)M16_COOPERATE();
   for(c=0;c<8;c++){
    const float *a=planar+c*n+(unsigned)((int)i+offset[c]),*b=planar+(c+8)*n+(unsigned)((int)i+offset[c+8]);
    r[c]=add4(approximate4(a,fraction32[c]),approximate4(b,fraction32[c+8]));
   }
   store4(out+i,mul4(add4(add4(add4(r[0],r[1]),add4(r[2],r[3])),add4(add4(r[4],r[5]),add4(r[6],r[7]))),dup4(0.0625f)));m16_beam_neon_calls[4]++;
  }
 }
 for(;i<n;i++){
  float r[8];if(!(i&127u))M16_COOPERATE();
  for(c=0;c<8;c++){
   const float *a=planar+c*n+(unsigned)((int)i+offset[c]),*b=planar+(c+8)*n+(unsigned)((int)i+offset[c+8]);
   r[c]=m16_beam_neon_mode==2?clean(approximate1(a,fraction32[c])+approximate1(b,fraction32[c+8])):exact1(a,fraction[c])+exact1(b,fraction[c+8]);
  }
  if(m16_beam_neon_mode==2){float a=clean(clean(r[0]+r[1])+clean(r[2]+r[3])),b=clean(clean(r[4]+r[5])+clean(r[6]+r[7]));out[i]=clean(clean(a+b)*0.0625f);}
  else out[i]=(((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7])))/16.f;
 }
 *guard=g;return 0;
fallback:m16_beam_neon_calls[5]++;return m16_previous_beam_tiles_entry(x,n,d,out,guard);
}
int ENTRY(const float *x,unsigned n,const double *d,float *out,unsigned *guard){return m16_beam_neon_mode?m16_beam_neon(x,n,d,out,guard):m16_previous_beam_tiles_entry(x,n,d,out,guard);}
