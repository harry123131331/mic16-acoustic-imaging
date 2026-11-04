#include "mic16_simd_fft.h"
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
/* 临时FFT缓冲区只在同步变换期间使用；不跨回调持有，不分配堆。
 * ARM使用显式单精度乘、加、减，禁止快数学和融合乘加；宿主机使用同序标量作数值对照。
 * 这是精度迁移，不承诺全输入域逐位等价；原黄金和实板检查均不可省略。 */
static float r[2048] __attribute__((aligned(64))),im[2048] __attribute__((aligned(64)));
static float twr[2][2048] __attribute__((aligned(64))),twi[2][2048] __attribute__((aligned(64)));
static const double *owner_r[2],*owner_i[2];
/* 仅窗口边界调用；只使查表失效，重新构造的成本留在变换内部。 */
void m16_simd_fft_clear_tables(void){owner_r[0]=owner_r[1]=owner_i[0]=owner_i[1]=0;}
unsigned m16_simd_fft_enabled,m16_simd_fft_calls[4],m16_simd_fft_vector_groups;
int m16_simd_fft_select(unsigned enabled){unsigned i;if(enabled>2)return -1;m16_simd_fft_enabled=enabled;m16_simd_fft_vector_groups=0;for(i=0;i<4;i++)m16_simd_fft_calls[i]=0;return 0;}
/* ARMv7 NEON浮点将非正规输入/结果归零，128位SIMD不使用VFP的逐项IEEE模式。
 * 在宿主机按每次乘/加/减显式模拟该语义；小级数的原标量蝶形保持不变。 */
static inline float clean(float x){union{float f;unsigned u;} v;v.f=x;if((v.u&0x7f800000u)==0)v.u&=0x80000000u;return v.f;}
static inline float muln(float x,float y){return clean(clean(x)*clean(y));}
static inline float addn(float x,float y){return clean(clean(x)+clean(y));}
static inline float subn(float x,float y){return clean(clean(x)-clean(y));}
static inline void one_neon_model(unsigned a,unsigned b,float wr,float wi){
 float br=r[b],bi=im[b],ar=r[a],ai=im[a];float re=subn(muln(wr,br),muln(wi,bi)),ii=addn(muln(wr,bi),muln(wi,br));
 r[a]=addn(ar,re);im[a]=addn(ai,ii);r[b]=subn(ar,re);im[b]=subn(ai,ii);
}
static void one(unsigned a,unsigned b,float wr,float wi){
 float br=r[b],bi=im[b],ar=r[a],ai=im[a];
 float re=wr*br-wi*bi,ii=wr*bi+wi*br;
 r[a]=ar+re;im[a]=ai+ii;r[b]=ar-re;im[b]=ai-ii;
}
void m16_simd_fft_transform(double *out_r,double *out_i,const double *wr,const double *wi,unsigned n,unsigned cooperate){
 unsigned i,j=0,bit,len,base,k,h=n/2,bank=n==4096?0:1;
 /* 表按每级蝶形的访问顺序排布，避免SIMD循环内跨步收集旋转因子。 */
 if(owner_r[bank]!=wr || owner_i[bank]!=wi){
  for(len=2;len<=h;len<<=1){unsigned half=len/2,step=n/len;
   for(k=0;k<half;k++){twr[bank][half-1+k]=(float)wr[k*step];twi[bank][half-1+k]=(float)wi[k*step];}}
  owner_r[bank]=wr;owner_i[bank]=wi;m16_simd_fft_calls[2]++;
 }
 for(i=0;i<h;i++){r[i]=(float)out_r[2*i];im[i]=(float)out_r[2*i+1];}
 for(i=1;i<h;i++){
  bit=h/2;for(;j&bit;bit>>=1)j^=bit;j^=bit;
  if(i<j){float a=r[i],b=im[i];r[i]=r[j];im[i]=im[j];r[j]=a;im[j]=b;}
 }
 for(len=2;len<=h;len<<=1){unsigned half=len/2;const float *tr=twr[bank]+half-1,*ti=twi[bank]+half-1;
  if(cooperate){M16_COOPERATE();}
  for(base=0;base<h;base+=len){
   k=0;
#ifdef __ARM_NEON
   if(m16_simd_fft_enabled==2)for(;k+3<half;k+=4){unsigned a=base+k,b=a+half;
    float32x4_t ar=vld1q_f32(r+a),ai=vld1q_f32(im+a),br=vld1q_f32(r+b),bi=vld1q_f32(im+b);
    float32x4_t wrr=vld1q_f32(tr+k),wii=vld1q_f32(ti+k);
    float32x4_t re=vsubq_f32(vmulq_f32(wrr,br),vmulq_f32(wii,bi));
    float32x4_t ii=vaddq_f32(vmulq_f32(wrr,bi),vmulq_f32(wii,br));
    vst1q_f32(r+a,vaddq_f32(ar,re));vst1q_f32(im+a,vaddq_f32(ai,ii));
    vst1q_f32(r+b,vsubq_f32(ar,re));vst1q_f32(im+b,vsubq_f32(ai,ii));m16_simd_fft_vector_groups++;
   }
#endif
   for(;k+3<half;k+=4){unsigned lane;for(lane=0;lane<4;lane++)one_neon_model(base+k+lane,base+k+lane+half,tr[k+lane],ti[k+lane]);}
   for(;k<half;k++)one(base+k,base+k+half,tr[k],ti[k]);
  }
 }
 {float a=r[0],b=im[0];out_r[0]=a+b;out_i[0]=0.;out_r[h]=a-b;out_i[h]=0.;}
 for(k=1;k<=h/2;k++){
  unsigned p=h-k;float a=r[k],b=im[k],c=r[p],d=im[p];
  float er=(a+c)*.5f,ei=(b-d)*.5f,orr=(b+d)*.5f,oi=(c-a)*.5f;
  float wk=(float)wr[k],ik=(float)wi[k],wp=(float)wr[p],ip=(float)wi[p];
  out_r[k]=er+wk*orr-ik*oi;out_i[k]=ei+wk*oi+ik*orr;
  if(p!=k){out_r[p]=er+wp*orr+ip*oi;out_i[p]=-ei-wp*oi+ip*orr;}
 }
 for(k=1;k<h;k++){out_r[n-k]=out_r[k];out_i[n-k]=-out_i[k];}
 m16_simd_fft_calls[bank]++;
}
#ifdef M16_SIMD_ARM_WRAP
#define ENTRY __wrap_m16_real_fft_transform
#define ORIGINAL __real_m16_real_fft_transform
#else
#define ENTRY m16_real_fft_transform
#define ORIGINAL m16_original_real_fft_transform
#endif
extern void ORIGINAL(double *,double *,const double *,const double *,unsigned,unsigned);
void ENTRY(double *r0,double *i0,const double *wr,const double *wi,unsigned n,unsigned cooperate){
 unsigned i;double maximum=0.;
 if(!m16_simd_fft_enabled){ORIGINAL(r0,i0,wr,wi,n,cooperate);return;}
 for(i=0;i<n;i++){double a=r0[i];if(a<0.)a=-a;if(a>maximum)maximum=a;}
 /* 极弱输入及超大输入保留FP64；避免ARMv7 NEON的非正规数处理改变此边界。
  * 这只约束输入动态范围，不是所有中间频点都不会出现相消的证明。 */
 if(maximum!=0. && (maximum<1e-15 || maximum>2.)){
  m16_simd_fft_calls[3]++;ORIGINAL(r0,i0,wr,wi,n,cooperate);return;
 }
 m16_simd_fft_transform(r0,i0,wr,wi,n,cooperate);
}

/* 公共定位FFT控制480个方向索引，对窄带相位误差敏感；部署候选默认保留FP64。
 * 候选和统计FFT仍可选择单精度；不修改任何解码、阈值或模型。 */
unsigned m16_simd_fft_preserve_public=1;
#ifdef M16_SIMD_ARM_WRAP
#define FRONT_ENTRY __wrap_m16_spectral_process_real
#define FRONT_ORIGINAL __real_m16_spectral_process_real
#else
#define FRONT_ENTRY m16_spectral_process_real
#define FRONT_ORIGINAL m16_original_spectral_process_real
#endif
extern int FRONT_ORIGINAL(const float *,mic16_spectral_frame *,mic16_spectral_workspace *);
int FRONT_ENTRY(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){
 unsigned saved=m16_simd_fft_enabled;int rc;if(m16_simd_fft_preserve_public)m16_simd_fft_enabled=0;
 rc=FRONT_ORIGINAL(x,o,w);m16_simd_fft_enabled=saved;return rc;
}
