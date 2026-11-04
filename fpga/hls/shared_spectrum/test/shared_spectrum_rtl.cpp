
/* 对完整arena进行HLS自动C/RTL比较；另用FP64稀疏DFT核验候选频点。 */
#include "shared_spectrum.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
static ap_uint<64> arena[80944];
static const unsigned W=16384;
static const ap_uint<64> sentinel=0x12345678abcdef01ULL;
union Bits{float f;unsigned u;};
static float sample(unsigned i,unsigned c,unsigned mode){
 (void)mode;return i?0.f:.01f*(c+1);
}
static void fill(unsigned n,unsigned mode){
 for(unsigned i=0;i<80944;i++)arena[i]=sentinel;
 for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<16;c++){
  unsigned at=i*16+c;Bits v;v.f=c<n?sample(i,c,mode):0.f;
  if(at&1)arena[at>>1].range(63,32)=v.u;else arena[at>>1].range(31,0)=v.u;
 }
}
static unsigned checksum(){unsigned h=2166136261u;for(unsigned i=0;i<80944;i++){unsigned long long v=(unsigned long long)arena[i];h=(h^(unsigned)v)*16777619u;h=(h^(unsigned)(v>>32))*16777619u;}return h;}
int main(){unsigned errors=0;const unsigned bins[8]={32,33,64,137,331,512,837,838};
 for(unsigned mode=1;mode<=1;mode++){
  unsigned n=mode==1?1:16;fill(n,mode);
  errors+=m16_sp_shared(arena,0,W,2048,1,n)!=0;
  for(unsigned c=0;c<n;c++)for(unsigned j=0;j<8;j++){
   unsigned k=bins[j];double re=0.,im=0.;
   for(unsigned i=0;i<2048;i++){double a=-6.2831853071795864769*k*i/4096.,v=sample(i,c,mode);re+=v*std::cos(a);im+=v*std::sin(a);}
   unsigned long long packed=(unsigned long long)arena[W+c*807+k-32];Bits r,t;r.u=(unsigned)packed;t.u=(unsigned)(packed>>32);
   double e=std::fmax(std::fabs(r.f-re),std::fabs(t.f-im))/(1.+std::hypot(re,im));errors+=e>2e-5;
  }
  for(unsigned i=W+n*807;i<80944;i++)errors+=arena[i]!=sentinel;
  std::printf("SHARED_RTL_CANDIDATE channels=%u checksum=%u errors=%u\n",n,checksum(),errors);
 }
 /* 旧整数模式以零输入核验共享FFT轮转、模式切换、全部AXI输出及返回。 */
 for(unsigned i=0;i<80944;i++)arena[i]=sentinel;
 for(unsigned i=0;i<16384;i++)arena[i]=0;
 errors+=m16_sp_shared(arena,0,W,2048,0,16)!=0;
 unsigned before=checksum();errors+=m16_sp_shared(arena,0,W,2048,2,16)!=-1;errors+=checksum()!=before;
 std::printf("SHARED_RTL_LEGACY_AND_REJECT checksum=%u errors=%u\n",before,errors);
 return errors?1:0;
}
