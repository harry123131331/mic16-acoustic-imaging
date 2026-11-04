#include "statistics_tile.h"
#include <stdlib.h>
static float clean(float f){union{float f;uint32_t u;}v;v.f=f;if(!(v.u&0x7f800000u))v.u&=0x80000000u;return v.f;}
static uint64_t pack(float a,float b){union{float f;uint32_t u;}x,y;x.f=a;y.f=b;return (uint64_t)x.u|((uint64_t)y.u<<32);}
static float part(uint64_t w,unsigned j){union{float f;uint32_t u;}v;v.u=(uint32_t)(w>>(32*j));return v.f;}
/* 仅验证替换边界，逐频点打包不作为生产数据流或性能实现。 */
void m16_statistics_tile_rotate_probe(const double *pairs,const float *coeff,float out[4][4]){
 uint64_t arena[2304]={0};unsigned p,c;
 for(p=0;p<32;p++){
  arena[p]=pack(clean((float)pairs[p*2]),clean((float)pairs[p*2+1]));
  for(c=0;c<4;c++)arena[1024+p*4+c]=pack(coeff[(p*2)*4+c],coeff[(p*2+1)*4+c]);
 }
 if(m16_statistics_tile(arena,2304,0,1024,2048,1,1,4))abort();
 for(c=0;c<4;c++){
  out[0][c]=part(arena[2048+c*2],0);out[1][c]=part(arena[2048+c*2],1);
  out[2][c]=part(arena[2048+c*2+1],0);out[3][c]=part(arena[2048+c*2+1],1);
 }
}
