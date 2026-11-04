#include "statistics_tile.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint64_t arena[2306],before[2306];
static unsigned state=20261023u,checks=0,errors=0,calls=0;
static uint32_t next(){state=state*1664525u+1013904223u;return state;}
static float clean(float f){union{float f;uint32_t u;}v;v.f=f;if(!(v.u&0x7f800000u))v.u&=0x80000000u;return v.f;}
static float number(uint64_t u,unsigned half){union{float f;uint32_t u;}v;v.u=(uint32_t)(u>>(32*half));return clean(v.f);}
static uint64_t pack(float a,float b){union{float f;uint32_t u;}x,y;x.f=a;y.f=b;return x.u|((uint64_t)y.u<<32);}
static void check(bool ok){checks++;if(!ok && errors++<8)printf("TILE_CHECK_FAILED check=%u\n",checks);}
/* 独立标量参考：输出优先遍历，严格沿用原NEON宿主模型的逐操作舍入。 */
static void reference(unsigned frames,unsigned bins,unsigned candidates,uint64_t *out){
 for(unsigned t=0;t<frames;t++)for(unsigned b=0;b<bins;b++)for(unsigned c=0;c<candidates;c++){
  float sum[4]={0,0,0,0};
  for(unsigned p=0;p<32;p++){
   uint64_t a=arena[1+(t*8+b)*32+p],s=arena[1025+(b*32+p)*4+c];
   float ar=number(a,0),ai=number(a,1),br=number(s,0),bi=number(s,1);
   float real=clean(clean(ar*br)+clean(ai*bi)),imag=clean(clean(ai*br)-clean(ar*bi));
   unsigned j=p<16?0:2;sum[j]=clean(sum[j]+real);sum[j+1]=clean(sum[j+1]+imag);
  }
  unsigned pos=((t*8+b)*4+c)*2;out[pos]=pack(sum[0],sum[1]);out[pos+1]=pack(sum[2],sum[3]);
 }
}
int main(){
 const uint64_t guard=0x98af32b17ce054d6ull;uint64_t expected[256];
 for(unsigned pattern=0;pattern<4;pattern++)for(unsigned frames=1;frames<=4;frames++)
 for(unsigned bins=1;bins<=8;bins++)for(unsigned candidates=1;candidates<=4;candidates++){
#ifdef M16_TILE_RTL_SMOKE
  if(!((frames==4&&bins==8&&candidates==4)||(frames==1&&bins==1&&candidates==1)||(frames==3&&bins==5&&candidates==2)))continue;
#endif
  for(unsigned i=0;i<2306;i++)arena[i]=guard;
  for(unsigned i=1;i<2049;i++){
   float a=((int)(next()>>16)-32768)*(1.f/32768.f),b=((int)(next()>>16)-32768)*(1.f/32768.f);
   if(pattern==1){a=(i&1)?0.f:-0.f;b=(i&2)?1.f:-1.f;}
   if(pattern==2){union{uint32_t u;float f;}v;v.u=(i&1?0x80000000u:0u)|((i&15)+1u);a=v.f;b=-v.f;}
   if(pattern==3){a=(i&1)?1.f:-1.f;b=(i&1)?(-1.f/16777216.f):(1.f/16777216.f);}
   arena[i]=pack(a,b);
  }
  for(unsigned i=0;i<256;i++)expected[i]=guard;
  reference(frames,bins,candidates,expected);memcpy(before,arena,sizeof(arena));
  check(m16_statistics_tile(arena,2306,1,1025,2049,frames,bins,candidates)==0);calls++;
  for(unsigned i=0;i<256;i++)check(arena[2049+i]==expected[i]);
  check(!memcmp(before,arena,2049*sizeof(uint64_t)));check(arena[2305]==guard);
 }
 for(unsigned test=0;test<9;test++){
#ifdef M16_TILE_RTL_SMOKE
  if(test!=1 && test!=3 && test!=8)continue;
#endif
  memcpy(before,arena,sizeof(arena));int rc;
  if(test==0)rc=m16_statistics_tile(0,2306,1,1025,2049,4,8,4);
  else if(test==1)rc=m16_statistics_tile(arena,2306,1,1025,2049,0,8,4);
  else if(test==2)rc=m16_statistics_tile(arena,2306,1,1025,2049,4,9,4);
  else if(test==3)rc=m16_statistics_tile(arena,2306,1,1025,2049,4,8,5);
  else if(test==4)rc=m16_statistics_tile(arena,2306,1,1,2049,4,8,4);
  else if(test==5)rc=m16_statistics_tile(arena,2306,1,1025,2048,4,8,4);
  else if(test==6)rc=m16_statistics_tile(arena,2306,0xffffffffu,1025,2049,4,8,4);
  else if(test==7)rc=m16_statistics_tile(arena,2304,1,1025,2049,4,8,4);
  else{arena[1]=0x7fc00000u;memcpy(before,arena,sizeof(arena));rc=m16_statistics_tile(arena,2306,1,1025,2049,4,8,4);}
  check(rc==(test==8?-2:-1));check(!memcmp(arena,before,sizeof(arena)));calls++;
 }
 printf("STATISTICS_TILE_UNIT checks=%u errors=%u calls=%u HOST_OR_CSIM_NOT_ARM_TIMING\n",checks,errors,calls);
 return errors?1:0;
}
