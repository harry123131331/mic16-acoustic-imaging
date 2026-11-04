#include "mic16_signature_dictionary.h"
#include "mic16_signature_recurrence.h"
#include "mic16_signature_dictionary_map.h"
#include <stddef.h>
#ifdef M16_SIGNATURE_DICTIONARY_ARM_WRAP
extern const uint32_t m16_signature_dictionary_values[];
extern const unsigned char m16_signature_dictionary_end[];
#define ENTRY __wrap_mic16_spatial_signature
#define ORIGINAL __real_mic16_spatial_signature
static int available(void){return (uintptr_t)m16_signature_dictionary_end-(uintptr_t)m16_signature_dictionary_values==M16_SIGNATURE_DICTIONARY_WORDS*4u;}
#else
#define ENTRY mic16_spatial_signature
#define ORIGINAL m16_original_spatial_signature
static uint32_t m16_signature_dictionary_values[M16_SIGNATURE_DICTIONARY_WORDS];static unsigned loaded;
extern void *fopen(const char *,const char *);extern size_t fread(void *,size_t,size_t,void *);extern int fclose(void *);
int m16_signature_dictionary_host_load(const char *path){void *f;size_t n,extra;unsigned char tail;loaded=0;if(!path)return -1;f=fopen(path,"rb");if(!f)return -1;
 n=fread(m16_signature_dictionary_values,4,M16_SIGNATURE_DICTIONARY_WORDS,f);extra=fread(&tail,1,1,f);fclose(f);if(n!=M16_SIGNATURE_DICTIONARY_WORDS || extra)return -1;loaded=1;return 0;}
static int available(void){return loaded!=0;}
#endif
extern int ORIGINAL(unsigned,unsigned,unsigned,float *);
unsigned m16_signature_dictionary_mode,m16_signature_dictionary_calls[3];
int m16_signature_dictionary_select(unsigned mode){unsigned i;if(mode>2 || (mode==2 && !available()))return -1;m16_signature_dictionary_mode=mode;for(i=0;i<3;i++)m16_signature_dictionary_calls[i]=0;return 0;}
int m16_signature_dictionary_lookup(unsigned g,unsigned p,unsigned b,float *ri){
 unsigned code,index;union{float f;uint32_t u;} re,im;
 if(g>=325 || p>=32 || b<32 || b>838 || !ri || !available())return -1;
 code=m16_signature_dictionary_codes[g*32+p];index=((code&0x7fffu)*807+b-32)*2;
 re.u=m16_signature_dictionary_values[index];im.u=m16_signature_dictionary_values[index+1]^((code&0x8000u)<<16);
 ri[0]=re.f;ri[1]=im.f;m16_signature_dictionary_calls[2]++;return 0;
}
int ENTRY(unsigned g,unsigned p,unsigned b,float *ri){
 if(m16_signature_dictionary_mode==2)return m16_signature_dictionary_lookup(g,p,b,ri);
 if(m16_signature_dictionary_mode==1){m16_signature_dictionary_calls[1]++;return m16_signature_recurrence(g,p,b,ri);}
 m16_signature_dictionary_calls[0]++;return ORIGINAL(g,p,b,ri);
}
