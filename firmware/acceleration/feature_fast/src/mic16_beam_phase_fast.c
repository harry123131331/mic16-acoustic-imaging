/* 索引偏移及小数部分每通道只算一次；不可证明精确的方向退回原函数。
 * 输入为FP32到FP64的原插值；各采样点内的16路树形求和次序保持。 */
#include "mic16_feature_fast.h"
#include "mic16_cooperate.h"
#include <stdint.h>
extern double floor(double),ceil(double);
#ifdef M16_FAST_ARM_WRAP
#define ORIGINAL_BEAM __real_mic16_candidate_beam_indexed
#else
#define ORIGINAL_BEAM m16_original_candidate_beam_indexed
#endif
extern int ORIGINAL_BEAM(const float *,unsigned,const double *,float *,unsigned *);
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
 __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
 return x<=y?y-x<na:x-y<nb;
}
static inline float sample(const float *x,unsigned i,unsigned c,int offset,double fraction){
 unsigned lo=(unsigned)((int)i+offset);double y=(double)x[lo*16+c];
 if(fraction!=0.)y+=((double)x[(lo+1)*16+c]-(double)x[lo*16+c])*fraction;
 return (float)y;
}
int mic16_candidate_beam_phase_fast(const float * restrict x,unsigned n,const double *d,float * restrict beam,unsigned *guard){
 float arrival[16],lags[32];double shifts[16],fractions[16],minshift=0.;int offsets[16];unsigned i,c,g;
 if(!x || !beam || !guard || (n!=8192 && n!=40960) || mic16_candidate_geometry(d,arrival,lags))return -1;
 if(overlap(x,(unsigned long)n*16*4,beam,(unsigned long)n*4))return -1;
 for(i=0;i<n*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
 for(c=0;c<16;c++){
  double shift=(double)arrival[c]-10.;if(shift>0.)return -1;if(shift<minshift)minshift=shift;shifts[c]=shift;
 }
 g=(unsigned)ceil(-minshift)+2;
 for(c=0;c<16;c++){
  double shift=shifts[c],scaled;
  /* |shift|<=40960且小数是2^-37的整数倍时，i+shift在全部合法样本范围内精确。
   * 最大q<2^16，FP64的最粗间距为2^-37；不满足时保留原逐样本舍入。 */
  if(shift< -40960.)goto fallback;
  scaled=shift*0x1p37;
  if(scaled!=(double)(int64_t)scaled)goto fallback;
  offsets[c]=(int)floor(shift);fractions[c]=shift-(double)offsets[c];
 }
 m16_feature_fast_calls[0]++;
 for(i=0;i<n;i++){
  if((i&127u)==0)M16_COOPERATE();
  if(i<g){beam[i]=0.f;continue;}
  float r0=sample(x,i,0,offsets[0],fractions[0])+sample(x,i,8,offsets[8],fractions[8]);
  float r1=sample(x,i,1,offsets[1],fractions[1])+sample(x,i,9,offsets[9],fractions[9]);
  float r2=sample(x,i,2,offsets[2],fractions[2])+sample(x,i,10,offsets[10],fractions[10]);
  float r3=sample(x,i,3,offsets[3],fractions[3])+sample(x,i,11,offsets[11],fractions[11]);
  float r4=sample(x,i,4,offsets[4],fractions[4])+sample(x,i,12,offsets[12],fractions[12]);
  float r5=sample(x,i,5,offsets[5],fractions[5])+sample(x,i,13,offsets[13],fractions[13]);
  float r6=sample(x,i,6,offsets[6],fractions[6])+sample(x,i,14,offsets[14],fractions[14]);
  float r7=sample(x,i,7,offsets[7],fractions[7])+sample(x,i,15,offsets[15],fractions[15]);
  float s=((r0+r1)+(r2+r3))+((r4+r5)+(r6+r7));beam[i]=s/16.f;
 }
 *guard=g;return 0;
fallback:
 m16_feature_fast_calls[1]++;return ORIGINAL_BEAM(x,n,d,beam,guard);
}
