/* 比较原floor入口与显式索引入口：普通/边界方向、极低幅度、错误合同及保护区。 */
#include "mic16_beam_index.h"
#include <stddef.h>
#include <stdint.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
extern double floor(double),ceil(double);
static float pcm[40960*16],a[40960+16],b[40960+16];
static unsigned checks,errors,cases,accepted,rejected;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("BEAM_INDEX_ERROR line=%u case=%u\n",(unsigned)__LINE__,cases);}}while(0)
static float bits(uint32_t u){union {uint32_t u;float f;}v;v.u=u;return v.f;}
static const double directions[][2]={{0,0},{-60,-30},{60,30},{-15,5},{35,-20},{-89.999999,0},{89.999999,0},
 {0,-89.999999},{0,89.999999},{89.999,89.999},{-89.999,-89.999},{1e-18,0},{-1e-18,0},{0,1e-18},{0,-1e-18},{1e-7,-1e-7}};
static void initialize(unsigned pattern){
 unsigned i,state=314159265;
 for(i=0;i<40960*16;i++){
  state=state*1664525u+1013904223u;
  if(pattern==0)pcm[i]=((int)(state>>16)-32768)*0x1p-15f;
  else if(pattern==1)pcm[i]=(i&1)?-1.f:1.f;
  else if(pattern==2)pcm[i]=bits((i&1)?0x80000000u:0u);
  else if(pattern==3)pcm[i]=bits((i&1)?0x80000001u:1u);
  else pcm[i]=(i%997==0)?0.75f:0.f;
 }
}
static void compare(const float *x,unsigned n,const double *d,int expect_error){
 unsigned ga=0xdeadbeefu,gb=ga,i;int ra,rb,intact=1;
 cases++;memset(a,0x5a,sizeof(a));memset(b,0x5a,sizeof(b));
 ra=mic16_candidate_beam(x,n,d,a+8,&ga);rb=mic16_candidate_beam_indexed(x,n,d,b+8,&gb);
 CHECK(ra==rb);CHECK(ga==gb);CHECK(!memcmp(a,b,sizeof(a)));
 for(i=0;i<32;i++)if(((unsigned char *)a)[i]!=0x5a || ((unsigned char *)b)[i]!=0x5a)intact=0;
 if(n==8192 || n==40960)for(i=0;i<32;i++)if(((unsigned char *)(a+8+n))[i]!=0x5a || ((unsigned char *)(b+8+n))[i]!=0x5a)intact=0;
 CHECK(intact);
 if(expect_error)CHECK(ra==-1);
 if(ra){rejected++;CHECK(ga==0xdeadbeefu);intact=1;
  for(i=0;i<sizeof(a);i++)if(((unsigned char *)a)[i]!=0x5a || ((unsigned char *)b)[i]!=0x5a)intact=0;
  CHECK(intact);
 }
 else{
  accepted++;CHECK(ga<n);
  for(i=0;i<ga;i++)if(a[i+8]!=0.f){CHECK(0);break;}
 }
}
int main(void){
 unsigned pattern,which,size,i,c;double invalid[][2]={{90,0},{-90,0},{0,90},{0,-90},{0,0}};
 for(pattern=0;pattern<5;pattern++){
  initialize(pattern);
  for(which=0;which<sizeof(directions)/sizeof(directions[0]);which++)for(size=0;size<2;size++)compare(pcm,size?40960:8192,directions[which],0);
 }
 /* 在边界索引、2的幂附近及最后一点核对证明涉及的范围与舍入。 */
 for(which=0;which<sizeof(directions)/sizeof(directions[0]);which++){
  float arrivals[16],lags[32];double low=0.;unsigned g;int valid=1;
  CHECK(!mic16_candidate_geometry(directions[which],arrivals,lags));
  for(c=0;c<16;c++){double shift=(double)arrivals[c]-10.;if(shift>0.)valid=0;if(shift<low)low=shift;}
  if(!valid)continue;g=(unsigned)ceil(-low)+2;
  {unsigned indices[7]={g,g+1,8191,8192,32767,32768,40959};
   for(i=0;i<7;i++)for(c=0;c<16;c++){
    double q=(double)indices[i]+((double)arrivals[c]-10.);
    CHECK(q>=2. && q<40960. && (unsigned)q==(unsigned)floor(q));
   }}
 }
 initialize(0);
 for(i=0;i<4;i++)compare(pcm,8192,invalid[i],1);
 invalid[4][0]=(double)bits(0x7fc00001u);compare(pcm,8192,invalid[4],1);
 compare(pcm,0,directions[0],1);compare(pcm,8191,directions[0],1);compare(pcm,40961,directions[0],1);
 compare(0,8192,directions[0],1);compare(pcm,8192,0,1);
 {const uint32_t bad[]={0x7fc00001u,0x7f800000u,0xff800000u,0x3f800001u,0xbf800001u};
  for(size=0;size<2;size++)for(i=0;i<5;i++){
   unsigned n=size?40960:8192;float old=pcm[n*16-1];pcm[n*16-1]=bits(bad[i]);compare(pcm,n,directions[0],1);pcm[n*16-1]=old;
  }}
 CHECK(mic16_candidate_beam_indexed(pcm,8192,directions[0],pcm,&i)==mic16_candidate_beam(pcm,8192,directions[0],pcm,&i));
 CHECK(mic16_candidate_beam_indexed(pcm,8192,directions[0],0,&i)==-1);
 CHECK(mic16_candidate_beam_indexed(pcm,8192,directions[0],b,0)==-1);
 printf("BEAM_INDEX_%s checks=%u cases=%u accepted=%u rejected=%u errors=%u full_output_bit_differences=0_ONLY_IF_PASS\n",
  errors?"FAIL":"PASS",checks,cases,accepted,rejected,errors);
 return errors?1:0;
}
