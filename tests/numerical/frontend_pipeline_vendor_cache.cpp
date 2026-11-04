/* 原HLS/厂商FFT模型的24帧缓存探针；非RTL、非实板，不缓存模型分数。 */
#include "mic16_tf_phat_frontend_hls_q2_24.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static ap_uint<64> arena[MIC16_TF_FRONTEND_ARENA_WORD_COUNT];
static int32_t raw[2048*16];
static uint32_t normalized_hash(void){
 uint32_t h=2166136261u;union{float f;uint32_t u;}v;
 for(unsigned i=0;i<32768;i++){
  v.f=(float)((double)raw[i]/33554432.);
  for(unsigned j=0;j<4;j++){h^=(v.u>>(8*j))&255u;h*=16777619u;}
 }
 return h;
}
int main(int argc,char **argv){
 FILE *in,*out;unsigned which;uint64_t word;
 if(argc!=4)return 2;which=(unsigned)strtoul(argv[1],0,10);if(which>1)return 2;
 in=fopen(argv[2],"rb");out=fopen(argv[3],"wb");if(!in || !out)return 3;
 for(unsigned frame=0;frame<24;frame++){
  if(fread(raw,4,32768,in)!=32768)return 4;
  for(unsigned i=0;i<16384;i++)arena[i]=(uint64_t)(uint32_t)raw[i*2]|((uint64_t)(uint32_t)raw[i*2+1]<<32);
  m16_tf_front_q24(arena,0,MIC16_TF_FRONTEND_PCM_WORD_COUNT,2048);
  uint32_t tag[5]={UINT32_C(0x4636314d),which,frame,25,normalized_hash()};
  if(fwrite(tag,4,5,out)!=5)return 5;
  for(unsigned i=0;i<12912;i++){
   word=arena[MIC16_TF_FRONTEND_PCM_WORD_COUNT+i].to_uint64();if(fwrite(&word,8,1,out)!=1)return 6;
  }
 }
 if(fgetc(in)!=EOF)return 7;fclose(in);fclose(out);puts("VENDOR_24_FRAMES_CMODEL_COMPLETE_NOT_RTL");return 0;
}
