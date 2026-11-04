#include "mic16_shared_spectrum_pack.h"
static int overlap(const void *a,size_t an,const void *b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return 1;
 return x<y+bn && y<x+an;
}
int m16_shared_pack_contiguous(const double *w,unsigned slot,uint64_t *a,size_t words){
 unsigned i;double maximum=0.;
 if(!w || !a || slot>=16 || words<M16_SHARED_ARENA_WORDS || ((uintptr_t)a&63u) ||
    overlap(w,2048*sizeof(double),a,M16_SHARED_ARENA_WORDS*sizeof(uint64_t)))return -1;
 /* 先检查再写，保留原混合FFT的极弱/超大回退；有限值也在PS侧阻断。 */
 for(i=0;i<2048;i++){
  double v=w[i];union{double d;uint64_t u;}bits;bits.d=v;
  if((bits.u&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000))return -1;
  if(v>2. || v< -2.)return 1;
  if(v<0.)v=-v;
  if(v>maximum)maximum=v;
 }
 if(maximum!=0. && maximum<1e-15)return 1;
 for(i=0;i<1024;i++){
  union{float f;uint32_t u;}x,y;x.f=(float)w[2*i];y.f=(float)w[2*i+1];
  a[slot*1024+i]=(uint64_t)x.u|((uint64_t)y.u<<32);
 }
 return 0;
}
int m16_shared_read_bins(const uint64_t *a,size_t words,unsigned slot,double *r,double *im){
 unsigned i;const size_t bytes=807*sizeof(double),arena_bytes=M16_SHARED_ARENA_WORDS*sizeof(uint64_t);
 if(!a || !r || !im || words<M16_SHARED_ARENA_WORDS || slot>=16 || ((uintptr_t)a&63u) ||
    overlap(r,bytes,im,bytes) || overlap(r,bytes,a,arena_bytes) || overlap(im,bytes,a,arena_bytes))return -1;
 /* 先核验全部频点为有限F32，避免部分更新后继续使用损坏的输出。 */
 for(i=0;i<807;i++){
  uint64_t v=a[M16_SHARED_WORKSPACE_WORD+slot*807+i];
  if((((uint32_t)v)&0x7f800000u)==0x7f800000u || (((uint32_t)(v>>32))&0x7f800000u)==0x7f800000u)return -1;
 }
 for(i=0;i<807;i++){
  uint64_t v=a[M16_SHARED_WORKSPACE_WORD+slot*807+i];union{float f;uint32_t u;}x,y;
  x.u=(uint32_t)v;y.u=(uint32_t)(v>>32);r[i]=(double)x.f;im[i]=(double)y.f;
 }
 return 0;
}
