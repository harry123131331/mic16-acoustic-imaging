/* 解析、布局、范围和提交前完整检查；不以伪寄存器冒充HLS执行。 */
#include "mic16_frontend_bridge.h"
extern int printf(const char *,...);
extern int memcmp(const void *,const void *,size_t);
extern void *memset(void *,int,size_t);
static int32_t pcm[M16_FRONT_PCM_VALUES];static uint64_t packed[M16_FRONT_PCM_WORDS];
static uint64_t spectrum[M16_FRONT_SPECTRUM_WORDS];static mic16_spectral_frame out,saved;
static unsigned checks,errors;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("FRONTEND_BRIDGE_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
static uint64_t complex_word(float r,float i){union{uint32_t u;float f;}a,b;a.f=r;b.f=i;return (uint64_t)a.u|((uint64_t)b.u<<32);}
static void rejected(unsigned scale,size_t count){
 saved=out;CHECK(m16_frontend_import_f32_spectrum(spectrum,count,scale,&out)==-1);
 CHECK(!memcmp(&out,&saved,sizeof(out)));
}
int main(void){
 unsigned i,c,b;
 for(i=0;i<M16_FRONT_PCM_VALUES;i++)pcm[i]=(int32_t)(i%65536)-16384;
 CHECK(!m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,16,packed,M16_FRONT_PCM_WORDS));
 for(i=0;i<M16_FRONT_PCM_WORDS;i++)CHECK((uint32_t)packed[i]==(uint32_t)pcm[i*2] && (uint32_t)(packed[i]>>32)==(uint32_t)pcm[i*2+1]);
 packed[0]=123;pcm[M16_FRONT_PCM_VALUES-1]=65537;
 CHECK(m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,16,packed,M16_FRONT_PCM_WORDS)==-1 && packed[0]==123);
 pcm[M16_FRONT_PCM_VALUES-1]=0;
 CHECK(m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES-1,16,packed,M16_FRONT_PCM_WORDS)==-1 && packed[0]==123);
 CHECK(m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,32,packed,M16_FRONT_PCM_WORDS)==-1 && packed[0]==123);
 CHECK(m16_frontend_pack_i32(0,M16_FRONT_PCM_VALUES,16,packed,M16_FRONT_PCM_WORDS)==-1);
 CHECK(m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,16,(uint64_t *)pcm,M16_FRONT_PCM_WORDS)==-1);
 memset(pcm,0,sizeof(pcm));pcm[0]=INT32_MIN;pcm[1]=INT32_MAX;
 CHECK(!m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,31,packed,M16_FRONT_PCM_WORDS));
 CHECK(packed[0]==UINT64_C(0x7fffffff80000000));
 pcm[0]=-1;pcm[1]=1;CHECK(!m16_frontend_pack_i32(pcm,M16_FRONT_PCM_VALUES,0,packed,M16_FRONT_PCM_WORDS));
 for(c=0;c<16;c++)for(b=0;b<807;b++)spectrum[c*807u+b]=complex_word((float)(c*1024u+b),(float)(-(int)(c*1024u+b)));
 CHECK(!m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,16,&out));
 for(c=0;c<16;c++)for(b=0;b<807;b++){
  unsigned j=(b*16u+c)*2u;CHECK(out.spectrum[j]==(double)(c*1024u+b)/65536. && out.spectrum[j+1]==-(double)(c*1024u+b)/65536.);
 }
 rejected(32,M16_FRONT_SPECTRUM_WORDS);rejected(16,M16_FRONT_SPECTRUM_WORDS-1);rejected(16,M16_FRONT_SPECTRUM_WORDS+1);
 spectrum[M16_FRONT_SPECTRUM_WORDS-1]=UINT64_C(0x7fc0000000000000);rejected(16,M16_FRONT_SPECTRUM_WORDS);
 spectrum[M16_FRONT_SPECTRUM_WORDS-1]=UINT64_C(0x000000007f800000);rejected(16,M16_FRONT_SPECTRUM_WORDS);
 spectrum[M16_FRONT_SPECTRUM_WORDS-1]=complex_word(4097.f,0.f);rejected(0,M16_FRONT_SPECTRUM_WORDS);
 saved=out;CHECK(m16_frontend_import_f32_spectrum(0,M16_FRONT_SPECTRUM_WORDS,16,&out)==-1 && !memcmp(&out,&saved,sizeof(out)));
 CHECK(m16_frontend_import_f32_spectrum((uint64_t *)&out,M16_FRONT_SPECTRUM_WORDS,16,&out)==-1 && !memcmp(&out,&saved,sizeof(out)));
 CHECK(m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,16,0)==-1);
 /* 旧raw互谱=1时会通过旧门，但除以2^16后必须归零。 */
 for(i=0;i<M16_FRONT_SPECTRUM_WORDS;i++)spectrum[i]=complex_word(1.f,0.f);
 CHECK(!m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,16,&out));
 for(i=0;i<807*32*2;i++)CHECK(out.phat[i]==0.f);
 /* FP32邻接值位于当前绝对门的两侧，不以epsilon偏移改变规则。 */
 for(i=0;i<M16_FRONT_SPECTRUM_WORDS;i++)spectrum[i]=complex_word(0.0001f,0.f);
 CHECK(!m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,0,&out) && out.phat[0]==0.f);
 for(i=0;i<M16_FRONT_SPECTRUM_WORDS;i++)spectrum[i]=UINT64_C(0x38d1b718);
 CHECK(!m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,0,&out) && out.phat[0]==1.f);
 spectrum[0]=complex_word(-0.f,0.f);CHECK(!m16_frontend_import_f32_spectrum(spectrum,M16_FRONT_SPECTRUM_WORDS,0,&out));
 printf("FRONTEND_BRIDGE_%s checks=%u errors=%u NO_HARDWARE\n",errors?"FAIL":"PASS",checks,errors);
 return errors?1:0;
}
