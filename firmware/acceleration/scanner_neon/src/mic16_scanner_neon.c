#include "mic16_scanner_neon.h"
#include "mic16_spatial_tables.h"
#include "mic16_cooperate.h"
#include <stdint.h>
#define COARSE_GUARD 0.0002f
#define TREE_GUARD 0.000004
static float frame_major[807*32][20*2] __attribute__((aligned(64)));
static float signature[32*34*2] __attribute__((aligned(64)));
unsigned m16_scanner_neon_mode,m16_scanner_neon_calls[6];
int m16_scanner_neon_select(unsigned mode){unsigned i;if(mode>2)return -1;m16_scanner_neon_mode=mode;for(i=0;i<6;i++)m16_scanner_neon_calls[i]=0;return 0;}
/* 先完成旧FP32加法的舍入，再与峰值区间比较；避免提升精度反而漏掉旧回退。 */
static int coarse_may_compete(float coarse,float peak,unsigned exact){
 float raised=coarse+COARSE_GUARD;
 return (double)raised>=(double)peak-(exact?0.:TREE_GUARD);
}
static int allowed(unsigned a,unsigned b){return (m16_second15[a][b/8]>>(b%8))&1;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;}
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
static int load_signature(unsigned z,unsigned g){
 unsigned p,b,start=z*34,n=807-start;if(n>34)n=34;
 for(p=0;p<32;p++)for(b=0;b<n;b++)if(mic16_spatial_signature(g,p,32+start+b,signature+(p*34+b)*2))return -1;
 return 0;
}
static void transpose(const float *x){unsigned i,j,t;for(i=0;i<807u*32u;i+=32){M16_COOPERATE();for(t=0;t<20;t++)for(j=i;j<i+32;j++){frame_major[j][t*2]=x[(t*807u*32u+j)*2];frame_major[j][t*2+1]=x[(t*807u*32u+j)*2+1];}}}
static void approximate_loaded(unsigned z,float *values){
 unsigned start=z*34,n=807-start,t,p,b,j,count;if(n>34)n=34;
 for(t=0;t<20;t+=4){
  v4 terms[1088];float sums[4];count=0;
  for(p=0;p<32;p++){if(!(p&3u))M16_COOPERATE();for(b=0;b<n;b++){
   v4 re,im;load_complex4(frame_major[(start+b)*32+p]+t*2,&re,&im);
   terms[count++]=add4(mul4(dup4(signature[(p*34+b)*2]),re),mul4(dup4(signature[(p*34+b)*2+1]),im));
  }}
  /* 成对树深度<=11；不使用1088项顺序FP32累加来冒充小误差界。 */
  while(count>1){unsigned pairs=count/2;for(j=0;j<pairs;j++)terms[j]=add4(terms[j*2],terms[j*2+1]);if(count&1u)terms[pairs]=terms[count-1];count=pairs+(count&1u);}
  store4(sums,terms[0]);for(j=0;j<4;j++)values[t+j]=sums[j]/(float)(32*n);
 }
}
int m16_scanner_neon_probe(const float *x,unsigned z,unsigned g,float *values){if(!x || !values || z>=24 || g>=325)return -1;transpose(x);if(load_signature(z,g))return -1;approximate_loaded(z,values);return 0;}
static int approximate_grid(float *maps,unsigned z,unsigned g){
 float v[20];unsigned t;if(load_signature(z,g))return -1;approximate_loaded(z,v);for(t=0;t<20;t++)maps[(t*24+z)*325+g]=v[t];m16_scanner_neon_calls[0]++;return 0;
}
static int exact_grid(float *maps,unsigned z,unsigned g,mic16_spatial_workspace *w){
 unsigned start=z*34,n=807-start,t,p,b;if(n>34)n=34;if(load_signature(z,g))return -1;
 for(t=0;t<20;t+=4){double a0=0.,a1=0.,a2=0.,a3=0.;
  for(p=0;p<32;p++)for(b=0;b<n;b++){
   const float *v=frame_major[(start+b)*32+p]+t*2;double sr=signature[(p*34+b)*2],si=signature[(p*34+b)*2+1];
   if(!b && !(p&3u))M16_COOPERATE();
   a0+=sr*(double)v[0]+si*(double)v[1];a1+=sr*(double)v[2]+si*(double)v[3];a2+=sr*(double)v[4]+si*(double)v[5];a3+=sr*(double)v[6]+si*(double)v[7];
  }
  w->accum[t]=a0;w->accum[t+1]=a1;w->accum[t+2]=a2;w->accum[t+3]=a3;
 }
 for(p=0;p<32;p++)for(b=0;b<n;b++){w->signature[(p*34+b)*2]=signature[(p*34+b)*2];w->signature[(p*34+b)*2+1]=signature[(p*34+b)*2+1];}
 for(t=0;t<20;t++)maps[(t*24+z)*325+g]=(float)w->accum[t]/(float)(32*n);
 m16_scanner_neon_calls[1]++;return 0;
}
static int resolve_peaks_exact(float *maps,unsigned z,const unsigned char *selected,unsigned char *exact,mic16_spatial_workspace *w){
 unsigned t,g,pass,a,b,changed;unsigned char pending[325];unsigned best[20];
 /* 先解决第一峰，再按已经精算的第一峰解决15度外第二峰。
  * 所有区间判定在FP64中进行，避免FP32的加减舍入错误排除边界候选。 */
 for(pass=0;pass<2;pass++)for(;;){
  changed=0;for(g=0;g<325;g++)pending[g]=0;
  for(t=0;t<20;t++){
   const float *s=maps+(t*24+z)*325;double lower=-3.;
   if(pass){a=325;for(g=0;g<325;g++)if(selected[g] && (a==325 || s[g]>s[a]))a=g;best[t]=a;}
   else best[t]=325;
   for(g=0;g<325;g++)if(selected[g] && (!pass || allowed(best[t],g))){double v=(double)s[g]-(exact[g]?0.:TREE_GUARD);if(v>lower)lower=v;}
   if(lower==-3.)return 1;
   for(g=0;g<325;g++)if(selected[g] && !exact[g] && (!pass || allowed(best[t],g)) && (double)s[g]+TREE_GUARD>=lower)pending[g]=1;
  }
  for(g=0;g<325;g++)if(pending[g]){if(exact_grid(maps,z,g,w))return -1;exact[g]=1;changed=1;}
  m16_scanner_neon_calls[2]++;if(!changed)break;
 }
 /* 进入投票的两个分值必须已经回原FP64，不能只依赖小误差近似。 */
 for(t=0;t<20;t++){
  const float *s=maps+(t*24+z)*325;a=b=325;
  for(g=0;g<325;g++)if(selected[g] && (a==325 || s[g]>s[a]))a=g;
  for(g=0;g<325;g++)if(selected[g] && allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
  if(a==325 || b==325 || !exact[a] || !exact[b])return -1;
 }
 return 0;
}
/* 数值迁移模式：唯一可能的第一峰无需重算；多方向误差区间重叠时，
 * 才回原FP64确定稳定排名。第二峰只输出有界分值，沿用原数值门验收。 */
static int resolve_peaks_numeric(float *maps,unsigned z,const unsigned char *selected,unsigned char *exact,mic16_spatial_workspace *w){
 unsigned t,g,changed;unsigned char pending[325];
 for(;;){
  changed=0;for(g=0;g<325;g++)pending[g]=0;
  for(t=0;t<20;t++){
   const float *s=maps+(t*24+z)*325;double lower=-3.;unsigned possibilities=0;
   for(g=0;g<325;g++)if(selected[g]){double v=(double)s[g]-(exact[g]?0.:TREE_GUARD);if(v>lower)lower=v;}
   for(g=0;g<325;g++)if(selected[g] && (double)s[g]+(exact[g]?0.:TREE_GUARD)>=lower)possibilities++;
   if(possibilities>1)for(g=0;g<325;g++)if(selected[g] && !exact[g] && (double)s[g]+TREE_GUARD>=lower)pending[g]=1;
  }
  for(g=0;g<325;g++)if(pending[g]){if(exact_grid(maps,z,g,w))return -1;exact[g]=1;changed=1;}
  m16_scanner_neon_calls[2]++;if(!changed)break;
 }
 for(t=0;t<20;t++){
  const float *s=maps+(t*24+z)*325;unsigned a=325,b=325;
  for(g=0;g<325;g++)if(selected[g] && (a==325 || s[g]>s[a]))a=g;
  for(g=0;g<325;g++)if(selected[g] && allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
  if(a==325 || b==325)return 1;
 }
 return 0;
}
static int resolve_peaks(float *maps,unsigned z,const unsigned char *selected,unsigned char *exact,mic16_spatial_workspace *w){
 return m16_scanner_neon_mode==2?resolve_peaks_numeric(maps,z,selected,exact,w):resolve_peaks_exact(maps,z,selected,exact,w);
}

int m16_scanner_refine_neon(const float *x,float *maps,mic16_spatial_workspace *w,m16_scanner_refinement *r){
 unsigned i,z,t,g,a,b,full,zero=1;unsigned char selected[325],exact[325];
 if(!x || !maps || !w || !r || overlap(x,M16_SP_PHAT_FLOATS*4ul,maps,M16_SP_MAP_FLOATS*4ul) || overlap(x,M16_SP_PHAT_FLOATS*4ul,w,sizeof(*w)) || overlap(maps,M16_SP_MAP_FLOATS*4ul,w,sizeof(*w)) || overlap(r,sizeof(*r),x,M16_SP_PHAT_FLOATS*4ul) || overlap(r,sizeof(*r),maps,M16_SP_MAP_FLOATS*4ul) || overlap(r,sizeof(*r),w,sizeof(*w)))return -1;
 for(i=0;i<M16_SP_PHAT_FLOATS;i++){if(x[i]!=x[i] || x[i]<-1.000001f || x[i]>1.000001f)return -1;if(x[i]!=0.f)zero=0;}
 for(i=0;i<M16_SP_MAP_FLOATS;i++)if(maps[i]!=maps[i] || maps[i]<-2.f || maps[i]>2.f)return -1;
 r->refined_grids=r->full_zones=0;r->zero_input=zero;
 if(zero){for(i=0;i<M16_SP_MAP_FLOATS;i++)maps[i]=0.f;m16_scanner_neon_calls[5]++;return 0;}
 transpose(x);
 for(z=0;z<24;z++){
  unsigned before=m16_scanner_neon_calls[1];int rc;
  for(g=0;g<325;g++)selected[g]=exact[g]=0;
  /* 与保留实现完全相同的初始粗筛集合。 */
  for(t=0;t<20;t++){
   const float *s=maps+(t*24+z)*325;a=0;b=325;
   for(g=1;g<325;g++)if(s[g]>s[a])a=g;
   for(g=0;g<325;g++)if(allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
   if(b==325)return -1;
   for(g=0;g<325;g++)if(s[g]>=s[a]-2.f*COARSE_GUARD || (allowed(a,g) && s[g]>=s[b]-2.f*COARSE_GUARD))selected[g]=1;
  }
  for(g=0;g<325;g++)if(selected[g]){if(approximate_grid(maps,z,g))return -1;m16_scanner_neon_calls[3]++;}
  rc=resolve_peaks(maps,z,selected,exact,w);if(rc<0)return -1;full=rc!=0;
  /* 模式1两峰已精算；模式2保留粗分FP32加法舍入，再按峰值区间保守回退。 */
  for(t=0;t<20 && !full;t++){
   const float *s=maps+(t*24+z)*325;a=b=325;
   for(g=0;g<325;g++)if(selected[g] && (a==325 || s[g]>s[a]))a=g;
   for(g=0;g<325;g++)if(selected[g] && allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
   if(a==325 || b==325){full=1;break;}
   for(g=0;g<325;g++)if(!selected[g] && ((m16_scanner_neon_mode==2?coarse_may_compete(s[g],s[a],exact[a]):s[g]+COARSE_GUARD>=s[a]) || (allowed(a,g) && (m16_scanner_neon_mode==2?coarse_may_compete(s[g],s[b],exact[b]):s[g]+COARSE_GUARD>=s[b])))){full=1;break;}
  }
  if(full){
   r->full_zones++;m16_scanner_neon_calls[4]++;
   for(g=0;g<325;g++)if(!selected[g]){if(approximate_grid(maps,z,g))return -1;selected[g]=1;}
   if(resolve_peaks(maps,z,selected,exact,w))return -1;
  }
  r->refined_grids+=m16_scanner_neon_calls[1]-before;
 }
 return 0;
}
#ifdef M16_SCANNER_NEON_ARM_WRAP
#define ENTRY __wrap_m16_scanner_refine_dataflow
#define ORIGINAL __real_m16_scanner_refine_dataflow
#else
#define ENTRY m16_scanner_refine_dataflow
#define ORIGINAL m16_original_refine_dataflow
#endif
extern int ORIGINAL(const float *,float *,mic16_spatial_workspace *,m16_scanner_refinement *);
int ENTRY(const float *x,float *m,mic16_spatial_workspace *w,m16_scanner_refinement *r){return m16_scanner_neon_mode?m16_scanner_refine_neon(x,m,w,r):ORIGINAL(x,m,w,r);}
