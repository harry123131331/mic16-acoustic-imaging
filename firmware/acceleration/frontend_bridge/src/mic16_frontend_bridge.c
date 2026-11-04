#include "mic16_frontend_bridge.h"
static int overlap(const void *a,size_t na,const void *b,size_t nb){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return x<=y?y-x<na:x-y<nb;
}
static double component(uint32_t bits){
 union {uint32_t u;float f;} v;v.u=bits;return (double)v.f;
}
static int valid_component(uint32_t bits,double gain){
 double value;
 if((bits&UINT32_C(0x7f800000))==UINT32_C(0x7f800000))return 0;
 value=component(bits)*gain;return value>=-4096. && value<=4096.;
}
int m16_frontend_pack_i32(const int32_t *pcm,size_t values,unsigned scale,uint64_t *words,size_t count){
 size_t i;int64_t bound;
 if(!pcm || !words || values!=M16_FRONT_PCM_VALUES || count!=M16_FRONT_PCM_WORDS || scale>31 ||
    overlap(pcm,M16_FRONT_PCM_VALUES*sizeof(*pcm),words,M16_FRONT_PCM_WORDS*sizeof(*words)))return -1;
 bound=INT64_C(1)<<scale;
 /* 先检查整帧，避免出现一部分新包、一部分旧包。 */
 for(i=0;i<values;i++)if((int64_t)pcm[i]<-bound || (int64_t)pcm[i]>bound)return -1;
 for(i=0;i<count;i++)words[i]=(uint64_t)(uint32_t)pcm[i*2]|((uint64_t)(uint32_t)pcm[i*2+1]<<32);
 return 0;
}
int m16_frontend_import_f32_spectrum(const uint64_t *words,size_t count,unsigned scale,mic16_spectral_frame *out){
 size_t i;unsigned b,c;double gain;
 if(!words || !out || count!=M16_FRONT_SPECTRUM_WORDS || scale>31 ||
    overlap(words,M16_FRONT_SPECTRUM_WORDS*sizeof(*words),out,sizeof(*out)))return -1;
 gain=1./(double)(UINT64_C(1)<<scale);
 /* 完整验证之后才改输出；observe的全部有限值与范围前提在这里已满足。 */
 for(i=0;i<count;i++)if(!valid_component((uint32_t)words[i],gain) || !valid_component((uint32_t)(words[i]>>32),gain))return -1;
 for(c=0;c<16;c++)for(b=0;b<807;b++){
  uint64_t value=words[c*807u+b];size_t j=(b*16u+c)*2u;
  out->spectrum[j]=component((uint32_t)value)*gain;
  out->spectrum[j+1]=component((uint32_t)(value>>32))*gain;
 }
 return mic16_spectral_observe(out);
}
