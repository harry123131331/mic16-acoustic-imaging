/* 原HLS C++和厂商FFT位精确模型的离线探针；不修改原核，不代替RTL/实板。 */
#include "mic16_tf_phat_frontend_hls_q2_24.h"
#include <stdint.h>
#include <stdio.h>
static ap_uint<64> arena[MIC16_TF_FRONTEND_ARENA_WORD_COUNT];
int main(int argc,char **argv){
 FILE *in,*out,*phat;uint64_t word;
 if(argc!=4)return 2;in=fopen(argv[1],"rb");out=fopen(argv[2],"wb");phat=fopen(argv[3],"wb");if(!in || !out || !phat)return 3;
 for(unsigned i=0;i<MIC16_TF_FRONTEND_PCM_WORD_COUNT;i++){if(fread(&word,8,1,in)!=1)return 4;arena[i]=word;}
 fclose(in);m16_tf_front_q24(arena,0,MIC16_TF_FRONTEND_PCM_WORD_COUNT,2048);
 for(unsigned i=0;i<MIC16_TF_FRONTEND_SPECTRUM_COUNT;i++){
  word=arena[MIC16_TF_FRONTEND_PCM_WORD_COUNT+i].to_uint64();if(fwrite(&word,8,1,out)!=1)return 5;
 }
 for(unsigned i=0;i<MIC16_TF_FRONTEND_OBSERVATION_COUNT;i++){
  word=arena[MIC16_TF_FRONTEND_PCM_WORD_COUNT+MIC16_TF_FRONTEND_OUTPUT_OFFSET+i].to_uint64();if(fwrite(&word,8,1,phat)!=1)return 6;
 }
 fclose(out);fclose(phat);puts("FRONTEND_VENDOR_CMODEL_PASS_NOT_RTL");return 0;
}
