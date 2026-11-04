/* 使用既有固定夹具的一帧，补高翻转输入；不生成房间数据或变更计算。 */
#include "shared_spectrum.h"
#include "mic16_shared_spectrum_pack.h"
#include <cstdio>
static ap_uint<64> arena[80944];
static uint64_t words[80944] __attribute__((aligned(64)));
static float input[2048];static double window[2048];static uint64_t expected[807];
static const ap_uint<64> sentinel=0x48a2e7b39cf016d5ULL;
int main(){unsigned errors=0;
 FILE *f=std::fopen("M:/build/hls/shared_spectrum_contiguous_20261023/audio_rtl/input.f32","rb");
 if(!f)return 2;unsigned got=std::fread(input,4,2048,f);std::fclose(f);if(got!=2048)return 3;
 f=std::fopen("M:/build/hls/shared_spectrum_contiguous_20261023/audio_rtl/expected.f32","rb");
 if(!f)return 4;got=std::fread(expected,8,807,f);std::fclose(f);if(got!=807)return 5;
 for(unsigned i=0;i<80944;i++)arena[i]=sentinel;
 for(unsigned i=0;i<2048;i++)window[i]=input[i];
 if(m16_shared_pack_contiguous(window,0,words,80944))return 6;
 for(unsigned i=0;i<1024;i++)arena[i]=words[i];
 errors+=m16_sp_shared(arena,0,16384,2048,4,1)!=0;
 for(unsigned i=0;i<1024;i++)errors+=arena[i]!=(ap_uint<64>)words[i];
 for(unsigned i=1024;i<16384;i++)errors+=arena[i]!=sentinel;
 for(unsigned i=0;i<807;i++)errors+=arena[16384+i]!=(ap_uint<64>)expected[i];
 for(unsigned i=16384+807;i<80944;i++)errors+=arena[i]!=sentinel;
 std::printf("MODE4_AUDIO_RTL active=1 samples=2048 complex=807 bit_errors=%u\n",errors);return errors?1:0;
}
