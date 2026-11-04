#include "mic16_beam_tiles.h"
#include "mic16_cooperate.h"
#include <stdint.h>
extern double floor(double),ceil(double);
#ifdef M16_BEAM_TILES_ARM_WRAP
#define ORIGINAL __real_mic16_candidate_beam_phase_fast
#define ENTRY __wrap_mic16_candidate_beam_phase_fast
#else
#define ORIGINAL m16_original_candidate_beam_phase_fast
#define ENTRY mic16_candidate_beam_phase_fast
#endif
extern int ORIGINAL(const float *,unsigned,const double *,float *,unsigned *);
unsigned m16_beam_tiles_enabled,m16_beam_tiles_calls[3];
int m16_beam_tiles_select(unsigned m){unsigned i;if(m>1)return -1;m16_beam_tiles_enabled=m;for(i=0;i<3;i++)m16_beam_tiles_calls[i]=0;return 0;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;}
/* 相邻4个插值只读取5个端点，原逐样本版本读取8个；每个插值仍按原FP64次序计算并转FP32。
 * 分数为0时不能读取第5点：该点可能刚好在输入末尾之外。
 * 分数非0且移位<=0时，整数偏移<=-1，故第5点仍不超过本组最后一个合法样本。 */
static inline void sample4(const float *x,unsigned i,unsigned c,int offset,double f,float *out){
 const float *p=x+((unsigned)((int)i+offset))*16+c;
 double a=p[0],b=p[16],d=p[32],e=p[48];
 if(f!=0.){double z=p[64];out[0]=(float)(a+(b-a)*f);out[1]=(float)(b+(d-b)*f);out[2]=(float)(d+(e-d)*f);out[3]=(float)(e+(z-e)*f);}
 else{out[0]=(float)a;out[1]=(float)b;out[2]=(float)d;out[3]=(float)e;}
}
static inline float sample1(const float *x,unsigned i,unsigned c,int offset,double f){
 unsigned lo=(unsigned)((int)i+offset);double a=x[lo*16+c];if(f!=0.)a+=((double)x[(lo+1)*16+c]-(double)x[lo*16+c])*f;return (float)a;
}
int m16_beam_tiles(const float *restrict x,unsigned n,const double *d,float *restrict out,unsigned *guard){
 float arrival[16],lags[32];double shifts[16],fractions[16],minshift=0.;int offsets[16];unsigned i,c,g,j;
 if(!x || !out || !guard || (n!=8192 && n!=40960) || mic16_candidate_geometry(d,arrival,lags))return -1;
 if(overlap(x,(unsigned long)n*16*4,out,(unsigned long)n*4))return -1;
 for(i=0;i<n*16;i++)if(!(x[i]>=-1.f && x[i]<=1.f))return -1;
 for(c=0;c<16;c++){double s=(double)arrival[c]-10.;if(s>0.)return -1;if(s<minshift)minshift=s;shifts[c]=s;}
 g=(unsigned)ceil(-minshift)+2;
 for(c=0;c<16;c++){
  double s=shifts[c],scaled;if(s< -40960.)goto fallback;scaled=s*0x1p37;
  if(scaled!=(double)(int64_t)scaled)goto fallback;offsets[c]=(int)floor(s);fractions[c]=s-offsets[c];
 }
 m16_beam_tiles_calls[0]++;m16_feature_fast_calls[0]++;
 M16_COOPERATE();
 for(i=0;i<g && i<n;i++)out[i]=0.f;
 for(;i+3<n;i+=4){float r[8][4];
  if((i&127u)<4u)M16_COOPERATE();
  for(c=0;c<8;c++){float a[4],b[4];sample4(x,i,c,offsets[c],fractions[c],a);sample4(x,i,c+8,offsets[c+8],fractions[c+8],b);
   for(j=0;j<4;j++)r[c][j]=a[j]+b[j];}
  for(j=0;j<4;j++){float sum=((r[0][j]+r[1][j])+(r[2][j]+r[3][j]))+((r[4][j]+r[5][j])+(r[6][j]+r[7][j]));out[i+j]=sum/16.f;}
  m16_beam_tiles_calls[1]++;
 }
 for(;i<n;i++){float r[8];for(c=0;c<8;c++)r[c]=sample1(x,i,c,offsets[c],fractions[c])+sample1(x,i,c+8,offsets[c+8],fractions[c+8]);
  out[i]=(((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7])))/16.f;}
 *guard=g;return 0;
fallback:m16_beam_tiles_calls[2]++;return ORIGINAL(x,n,d,out,guard);
}
int ENTRY(const float *x,unsigned n,const double *d,float *out,unsigned *guard){return m16_beam_tiles_enabled?m16_beam_tiles(x,n,d,out,guard):ORIGINAL(x,n,d,out,guard);}
