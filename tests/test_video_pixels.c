#include "mic16_video_pixels.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *fopen(const char *,const char *);
extern size_t fread(void *,size_t,size_t,void *);
extern int fclose(void *);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
#include <stdint.h>
#define FULL (800u*480u*2u)
static unsigned char whole[FULL+16],chunked[FULL+16];
static unsigned checks;
#define CHECK(ok) do{checks++;if(!(ok)){printf("VIDEO_PIXELS_FAIL line=%u\n",(unsigned)__LINE__);return 1;}}while(0)
static uint16_t expected(unsigned i){return (uint16_t)((i*73u)^257u);}
static void put(unsigned char *p,uint16_t value){p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);}
static uint16_t get(const unsigned char *p){return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
int main(int argc,char **argv){
 void *f;unsigned i,offset,n;unsigned char trace[128],before[128];
 CHECK(argc==2);f=fopen(argv[1],"rb");CHECK(f!=0);
 CHECK(fread(trace,1,sizeof(trace),f)==sizeof(trace));CHECK(fread(before,1,1,f)==0);fclose(f);
 /* 输入来自独立的实际原厂FIFO仿真，不由被测转换函数生成。 */
 CHECK(get(trace)==(uint16_t)(0x1200+3*0x35));
 CHECK(m16_video_fifo64_to_rgb565(trace,sizeof(trace))==0);
 for(i=0;i<64;i++)CHECK(get(trace+2*i)==(uint16_t)(0x1200+i*0x35));
 memcpy(before,trace,sizeof(trace));
 for(i=0;i<16;i++)if(i==0||(i&7u)){
  CHECK(m16_video_fifo64_to_rgb565(trace,i)<0);CHECK(memcmp(before,trace,sizeof(trace))==0);
 }
 CHECK(m16_video_fifo64_to_rgb565(0,8)<0);
 memset(whole,0xA5,sizeof(whole));
 for(i=0;i<FULL/2;i++)put(whole+8+2*i,expected((i&~3u)+(3u-(i&3u))));
 memcpy(chunked,whole,sizeof(whole));
 CHECK(m16_video_fifo64_to_rgb565(whole+8,FULL)==0);
 for(offset=0;offset<FULL;offset+=n){n=FULL-offset;if(n>16384u)n=16384u;CHECK(m16_video_fifo64_to_rgb565(chunked+8+offset,n)==0);}
 CHECK(memcmp(whole,chunked,sizeof(whole))==0);
 for(i=0;i<8;i++){CHECK(whole[i]==0xA5);CHECK(whole[FULL+8+i]==0xA5);}
 for(i=0;i<FULL/2;i++)CHECK(get(whole+8+2*i)==expected(i));
 /* 覆盖全部65536种16位值，确认颜色位和像素内字节保持。 */
 for(i=0;i<65536u;i++)put(whole+2*i,(uint16_t)((i&~3u)+(3u-(i&3u))));
 CHECK(m16_video_fifo64_to_rgb565(whole,65536u*2u)==0);
 for(i=0;i<65536u;i++)CHECK(get(whole+2*i)==(uint16_t)i);
 printf("VIDEO_PIXELS_PASS checks=%u full_bytes=%u chunks=47 colors=65536\n",checks,FULL);
 return 0;
}
