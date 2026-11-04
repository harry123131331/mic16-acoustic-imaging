
/* 实际PS模块连续打包；最大16列跨DDR页读，核对全输入/输出/保留区及非法命令。 */
#include "shared_spectrum.h"
#include "mic16_shared_spectrum_pack.h"
#include <cstdio>
#include <cmath>
static ap_uint<64> a[80944];
static uint64_t words[80944] __attribute__((aligned(64)));
static double window[2048];
static const ap_uint<64> sentinel=0x12345678abcdef01ULL;
union Bits{float f;unsigned u;};
int main(){unsigned errors=0,h=2166136261u;
 for(unsigned i=0;i<80944;i++)a[i]=sentinel;
 for(unsigned c=0;c<16;c++){
  for(unsigned i=0;i<2048;i++)window[i]=0.;
  window[0]=c==0?.03125:c==7?-.0625:c==15?.125:0.;
  errors+=m16_shared_pack_contiguous(window,c,words,80944)!=0;
 }
 for(unsigned i=0;i<16384;i++)a[i]=words[i];
 errors+=m16_sp_shared(a,0,16384,2048,4,16)!=0;
 for(unsigned i=0;i<16384;i++)errors+=a[i]!=(ap_uint<64>)words[i];
 for(unsigned c=0;c<16;c++)for(unsigned b=0;b<807;b++){
  double expected=c==0?.03125:c==7?-.0625:c==15?.125:0.;
  uint64_t v=(uint64_t)a[16384+c*807+b];Bits r,im;r.u=(unsigned)v;im.u=(unsigned)(v>>32);
  errors+=std::fabs(r.f-expected)>1e-7 || std::fabs(im.f)>1e-7;
  h=(h^(unsigned)v)*16777619u;h=(h^(unsigned)(v>>32))*16777619u;
 }
 for(unsigned i=16384+16*807;i<80944;i++)errors+=a[i]!=sentinel;
 errors+=m16_sp_shared(a,0,16384,2048,4,0)!=-1;
 unsigned after=2166136261u;
 for(unsigned i=0;i<16384;i++)errors+=a[i]!=(ap_uint<64>)words[i];
 for(unsigned i=0;i<16*807;i++){uint64_t v=(uint64_t)a[16384+i];after=(after^(unsigned)v)*16777619u;after=(after^(unsigned)(v>>32))*16777619u;}
 for(unsigned i=16384+16*807;i<80944;i++)errors+=a[i]!=sentinel;
 errors+=after!=h;
 std::printf("MODE4_RTL batch=16 complex=12912 checksum=%u errors=%u\n",h,errors);return errors?1:0;
}
