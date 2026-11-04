/* 测试数据流：不生成声源预测，不控制现场设备。 */
#include "mic16_frontend_bridge.h"
extern void *fopen(const char *,const char *);
extern size_t fread(void *,size_t,size_t,void *),fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *),strcmp(const char *,const char *),printf(const char *,...);
extern unsigned long strtoul(const char *,char **,int);
static int32_t pcm[M16_FRONT_PCM_VALUES];static float normalized[M16_FRONT_PCM_VALUES];
static uint64_t words[M16_FRONT_PCM_WORDS];static mic16_spectral_frame out;static mic16_spectral_workspace work;
int main(int argc,char **argv){
 void *in,*output;unsigned scale;int rc;
 if(argc!=5)return 2;scale=(unsigned)strtoul(argv[2],0,10);
 in=fopen(argv[3],"rb");output=fopen(argv[4],"wb");if(!in || !output)return 3;
 if(!strcmp(argv[1],"pack")){
  if(fread(pcm,4,M16_FRONT_PCM_VALUES,in)!=M16_FRONT_PCM_VALUES)return 4;
  rc=m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,scale,words,M16_FRONT_PCM_WORDS);if(rc)return 5;
  if(fwrite(words,8,M16_FRONT_PCM_WORDS,output)!=M16_FRONT_PCM_WORDS)return 6;
 }else{
  if(!strcmp(argv[1],"import")){
   if(fread(words,8,M16_FRONT_SPECTRUM_WORDS,in)!=M16_FRONT_SPECTRUM_WORDS)return 4;
   rc=m16_frontend_import_f32_spectrum(words,M16_FRONT_SPECTRUM_WORDS,scale,&out);
  }else if(!strcmp(argv[1],"cpu")){
   if(fread(normalized,4,M16_FRONT_PCM_VALUES,in)!=M16_FRONT_PCM_VALUES)return 4;
   mic16_spectral_init(&work);rc=mic16_spectral_process(normalized,&out,&work);
  }else return 7;
  if(rc || fwrite(&out,1,sizeof(out),output)!=sizeof(out))return 8;
 }
 fclose(in);fclose(output);printf("FRONTEND_IO_PASS mode=%s\n",argv[1]);return 0;
}
