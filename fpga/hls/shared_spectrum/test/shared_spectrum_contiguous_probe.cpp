#include "shared_spectrum.h"
#include "mic16_shared_spectrum_pack.h"
static uint64_t ps_words[80944] __attribute__((aligned(64)));
static double ps_window[2048];
#include "mic16_fft_engine_hls.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
static ap_uint<64> a[MIC16_TF_FRONTEND_ARENA_WORD_COUNT],b[MIC16_TF_FRONTEND_ARENA_WORD_COUNT];
static unsigned checks,errors;static const unsigned W=MIC16_TF_FRONTEND_PCM_WORD_COUNT;
static const unsigned long long SENTINEL=0x6a3b5c7d19283746ULL;
union bits_t{float f;unsigned u;};
static void put(unsigned index,float f){bits_t v;v.f=f;if(index&1)a[index/2].range(63,32)=v.u;else a[index/2].range(31,0)=v.u;}
static float input(unsigned index){bits_t v;v.u=(unsigned)(index&1?a[index/2].range(63,32):a[index/2].range(31,0));return v.f;}
static void clear(){for(unsigned i=0;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++)a[i]=SENTINEL;}
static int unit(){
 clear();for(unsigned i=0;i<2048*16;i++){int v=(int)((i*997u)%4096u)-2048;ap_uint<32> q=(unsigned)v;if(i&1)a[i/2].range(63,32)=q;else a[i/2].range(31,0)=q;}
 for(unsigned i=0;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++)b[i]=a[i];
 m16_tf_front_q24(b,0,W,2048);errors+=m16_sp_shared(a,0,W,2048,0,16)!=0;
 for(unsigned i=0;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++){checks++;errors+=a[i]!=b[i];}
 clear();for(unsigned i=0;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++)b[i]=a[i];
 errors+=m16_sp_shared(a,0,W,2048,3,16)!=-1;errors+=m16_sp_shared(a,0,W,2048,1,0)!=-1;
 errors+=m16_sp_shared(a,0,W,2048,1,17)!=-1;errors+=m16_sp_shared(a,0,W,1024,1,16)!=-1;
 for(unsigned i=0;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++){checks++;errors+=a[i]!=b[i];}
 double maximum=0.;unsigned bins[8]={32,33,64,137,331,512,837,838};
 for(unsigned test=0;test<4;test++){
  unsigned channels=test==0?1:test==1?7:16;clear();
  for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<16;c++){
   double x=test==0?0.:test==1?(i==512+c?.125:0.):.1*std::sin(2.*3.14159265358979323846*137.*i/4096.+.1*c)+.05*std::cos(2.*3.14159265358979323846*331.*i/4096.);
   if(test==3)x*=1e-7;put(c*2048+i,(float)x);
  }
  errors+=m16_sp_shared(a,0,W,2048,4,channels)!=0;
  for(unsigned c=0;c<channels;c++)for(unsigned j=0;j<8;j++){
   unsigned bin=bins[j];double re=0.,im=0.;
   for(unsigned i=0;i<2048;i++){double phase=2.*3.14159265358979323846*bin*i/4096.,x=input(c*2048+i);re+=x*std::cos(phase);im-=x*std::sin(phase);}
   bits_t r,v;r.u=(unsigned)a[W+c*807+bin-32].range(31,0);v.u=(unsigned)a[W+c*807+bin-32].range(63,32);
   double d=std::hypot((double)r.f-re,(double)v.f-im)/(1.+std::hypot(re,im));if(d>maximum)maximum=d;checks++;errors+=!(d<=0.00002);
  }
  for(unsigned i=W+channels*807;i<MIC16_TF_FRONTEND_ARENA_WORD_COUNT;i++){checks++;errors+=a[i]!=(ap_uint<64>)SENTINEL;}
 }
 std::printf("SHARED_SPECTRUM_UNIT checks=%u errors=%u maximum_scaled_dft_error=%.17g\n",checks,errors,maximum);return errors?1:0;
}
/* 记录格式：逐变换2048个预加窗F32，输出逐变换807个实虚F32；16个变换打成一批。 */
static int replay(const char *ip,const char *op,unsigned count){
 FILE *in=std::fopen(ip,"rb"),*out=std::fopen(op,"wb");if(!in || !out)return 3;
 for(unsigned base=0;base<count;base+=16){
  unsigned active=count-base<16?count-base:16;clear();
  for(unsigned c=0;c<active;c++){
   for(unsigned i=0;i<2048;i++){float f;if(std::fread(&f,4,1,in)!=1)return 4;ps_window[i]=(double)f;}
   if(m16_shared_pack_contiguous(ps_window,c,ps_words,80944))return 8;
  }
  for(unsigned i=0;i<active*1024;i++)a[i]=ps_words[i];
  if(m16_sp_shared(a,0,W,2048,4,active))return 5;
  for(unsigned c=0;c<active;c++)for(unsigned bin=0;bin<807;bin++){unsigned ri[2];ri[0]=(unsigned)a[W+c*807+bin].range(31,0);ri[1]=(unsigned)a[W+c*807+bin].range(63,32);if(std::fwrite(ri,4,2,out)!=2)return 6;}
 }
 if(std::fgetc(in)!=EOF)return 7;std::fclose(in);std::fclose(out);std::printf("SHARED_SPECTRUM_REPLAY transforms=%u batches=%u\n",count,(count+15)/16);return 0;
}
int main(int argc,char **argv){return argc==4?replay(argv[1],argv[2],(unsigned)std::strtoul(argv[3],0,10)):unit();}
