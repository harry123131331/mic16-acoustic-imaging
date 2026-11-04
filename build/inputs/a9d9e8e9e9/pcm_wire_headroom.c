#include "mic16_pcm_wire.h"
#include <string.h>
#define MAGIC 0x50434d31u
static void u16(unsigned char *p,unsigned x){p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
static void u32(unsigned char *p,uint32_t x){for(unsigned i=0;i<4u;i++)p[i]=(unsigned char)(x>>(8u*i));}
static void u64(unsigned char *p,uint64_t x){u32(p,(uint32_t)x);u32(p+4,(uint32_t)(x>>32));}
static uint32_t crc(uint32_t c,const unsigned char *p,unsigned n){
 static const uint32_t table[16]={0,0x1db71064u,0x3b6e20c8u,0x26d930acu,0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
 while(n--){c^=*p++;c=(c>>4)^table[c&15u];c=(c>>4)^table[c&15u];}return c;
}
int m16_pcm_wire_init(m16_pcm_wire *t){
 if(!t||t->magic||(uintptr_t)t%_Alignof(m16_pcm_wire))return -2200;
 *t=(m16_pcm_wire){0};t->magic=MAGIC;return 0;
}
int m16_pcm_wire_begin(m16_pcm_wire *t,const float *x,unsigned epoch,uint64_t first,unsigned scale){
 if(!t||t->magic!=MAGIC||!x||(uintptr_t)x%4u||!epoch||first%2048u||first/2048u>UINT32_MAX||scale>30u)return -2200;
 if(t->pcm||t->pending)return -2201;
 uintptr_t a=(uintptr_t)t,b=(uintptr_t)x;
 if(a>UINTPTR_MAX-sizeof(*t)||b>UINTPTR_MAX-32768u*4u||!(a+sizeof(*t)<=b||b+32768u*4u<=a))return -2200;
 t->pcm=x;t->epoch=epoch;t->first_sample=first;t->frame=(unsigned)(first/2048u);t->cursor=0;t->scale_exponent=scale;return 0;
}
int m16_pcm_wire_packet(m16_pcm_wire *t,const unsigned char **data,unsigned *bytes){
 if(!t||t->magic!=MAGIC||!data||!bytes)return -2200;
 if(!t->pcm)return 0;
 if(!t->pending){
  unsigned char *p=t->packet;memset(p,0,64);memcpy(p,"M16P",4);u16(p+4,1);u16(p+6,64);
  u32(p+8,t->epoch);u32(p+12,t->frame);u64(p+16,t->first_sample);u32(p+24,78125);u32(p+28,2);
  u16(p+32,16);u16(p+34,1);u32(p+36,2048);u16(p+40,t->cursor/16u);u16(p+42,128);
  u32(p+44,t->cursor);u32(p+48,16);u32(p+52,t->scale_exponent);u32(p+56,2u);
  for(unsigned i=0;i<256u;i++){
   float x=t->pcm[t->cursor*16u+i];uint32_t word;
   if(!(x>=-4.f&&x<=4.f))return -2202;
   memcpy(&word,&x,4);u32(p+64+i*4u,word);
  }
  uint32_t sum=crc(0xffffffffu,p,60);sum=crc(sum,p+64,1024)^0xffffffffu;u32(p+60,sum);
  t->pending=1;t->bytes=1088;
 }
 *data=t->packet;*bytes=t->bytes;return 1;
}
int m16_pcm_wire_ack(m16_pcm_wire *t){
 if(!t||t->magic!=MAGIC||!t->pending||!t->pcm)return -2203;
 t->pending=0;t->cursor+=16u;if(t->cursor==2048u)t->pcm=0;return 0;
}
void m16_pcm_wire_stop(m16_pcm_wire *t){if(t&&t->magic==MAGIC){t->pcm=0;t->pending=0;}}
