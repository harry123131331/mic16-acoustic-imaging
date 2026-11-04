/* 原核与连续流水逐输出位比较；可独立运行原冻结黄金输入的RTL协同仿真。 */
#include "mic16_tf_zone_streaming_hls_q2_24.h"
#include "mic16_spatial_tables.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
extern void m16_scanner_reference(ap_uint<64> *,unsigned,unsigned,unsigned,unsigned);
static ap_uint<64> a[50532],b[50532],initial[50532];
static unsigned state=20261022u;
static unsigned next(){state=state*1664525u+1013904223u;return state;}
static ap_uint<64> packed(float re,float im){mic16_tf_q2_24_t x=re,y=im;ap_uint<64> r=0;r.range(25,0)=x.range(25,0);r.range(51,26)=y.range(25,0);return r;}
int main(int argc,char **argv){
 unsigned errors=0,checks=0,cases=argc>1?std::strtoul(argv[1],0,10):7;const double pi=3.1415926535897932384626433832795;
 if(!cases || cases>7)return 3;
 for(unsigned i=0;i<50532;i++)initial[i]=UINT64_C(0x53ca4e17bd2869f0);
 for(unsigned g=0;g<325;g++)for(unsigned p=0;p<32;p++){
  unsigned k=g*32+p;double step=2.*pi*(double)m16_grid_lags[g][p]/4096.;
  initial[25824+k]=packed((float)std::cos(32.*step),(float)std::sin(32.*step));
  initial[36224+k]=packed((float)std::cos(step),(float)std::sin(step));
 }
 for(unsigned scenario=0;scenario<cases;scenario++){
  if(scenario==6)for(unsigned i=0;i<10400;i++){
   initial[25824+i]=packed((i&1)?-2.f:1.99999988f,(i&2)?-2.f:1.99999988f);
   initial[36224+i]=packed((i&4)?-2.f:1.99999988f,(i&8)?-2.f:1.99999988f);
  }
  for(unsigned bin=0;bin<807;bin++)for(unsigned p=0;p<32;p++){
   float re=((int)(next()>>16)-32768)*0x1p-15f,im=((int)(next()>>16)-32768)*0x1p-15f;
   if(scenario==0)re=im=0.f;
   if(scenario==2){double phase=2.*pi*(double)m16_grid_lags[162][p]*(32+bin)/4096.;re=(float)std::cos(phase);im=(float)std::sin(phase);}
   if(scenario==3){re=(p&1)?-1.f:1.f;im=(bin&1)?-1.f:1.f;}
   if(scenario==4 && bin!=333)re=im=0.f;
   if(scenario==5){re*=0x1p-16f;im*=0x1p-16f;}
   if(scenario==6){re=(p&1)?-2.f:1.99999988f;im=(bin&1)?-2.f:1.99999988f;}
   initial[bin*32+p]=packed(re,im);
  }
  for(unsigned i=0;i<50532;i++)a[i]=b[i]=initial[i];
  m16_scanner_reference(a,0,25824,36224,46624);
  mic16_tf_zone_streaming_hls_q2_24(b,0,25824,36224,46624);
  for(unsigned i=0;i<50532;i++){checks++;if(a[i]!=b[i]){if(errors<5)std::printf("SCANNER_PIPELINE_ERROR case=%u word=%u\n",scenario,i);errors++;}}
  std::printf("SCANNER_PIPELINE_CASE case=%u checks=%u errors=%u\n",scenario,checks,errors);std::fflush(stdout);
 }
 std::printf("SCANNER_PIPELINE_%s cases=%u checks=%u errors=%u\n",errors?"FAIL":"PASS",cases,checks,errors);return errors?1:0;
}
