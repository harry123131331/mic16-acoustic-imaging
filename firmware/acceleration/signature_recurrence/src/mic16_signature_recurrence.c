#include "mic16_signature_recurrence.h"
#include "mic16_spatial_tables.h"
#include <stdint.h>
extern double sin(double),cos(double);
#define PI 3.14159265358979323846264338327950288
#ifdef M16_SIGNATURE_RECURRENCE_ARM_WRAP
#define ENTRY __wrap_mic16_spatial_signature
#define ORIGINAL __real_mic16_spatial_signature
#else
#define ENTRY mic16_spatial_signature
#define ORIGINAL m16_original_spatial_signature
#endif
extern int ORIGINAL(unsigned,unsigned,unsigned,float *);
static double rotor[325][32][2] __attribute__((aligned(64)));
static unsigned char ready[325][32];
/* 只缓存当前g/p/频带的34个值，不再复制整个签名张量。
 * 每块总从固定频带起点重算，任意调用顺序得到同一系数。 */
static struct {unsigned valid,g,p,z,start,end;float values[34*2];} block;
unsigned m16_signature_recurrence_enabled,m16_signature_recurrence_calls[5];
int m16_signature_recurrence_select(unsigned mode){unsigned i;if(mode>1)return -1;m16_signature_recurrence_enabled=mode;block.valid=0;for(i=0;i<5;i++)m16_signature_recurrence_calls[i]=0;return 0;}
void m16_signature_recurrence_clear(void){unsigned g,p;block.valid=0;for(g=0;g<325;g++)for(p=0;p<32;p++)ready[g][p]=0;}
/* 在FP32舍入区间边缘保留2^-40绝对保护带。这不是检测门限，
 * 也不单靠保护带宣称跨平台全域正确舍入；原离散域必须完整逐位核验。 */
/* 非2幂FP32值两边的半ULP相同；2幂处选较小的一侧，得到保守对称区间。
 * q的指数e决定半ULP=2^(e-151)，2幂处再除2；直接构造FP64指数。
 * 仅一次FP32转回FP64并求距离，避免每分量构造上下邻值的4次转换。 */
static int rounding_safe(double x,float *value){
 union{float f;uint32_t u;} q;union{double d;uint64_t u;} half,diff;uint32_t bits,exponent;
 q.f=(float)x;bits=q.u&0x7fffffffu;
 if(bits<0x38000000u || bits>0x3f800000u)return 0;
 exponent=(bits>>23)+872u-((bits&0x007fffffu)==0);
 half.u=(uint64_t)exponent<<52;diff.d=x-(double)q.f;diff.u&=UINT64_C(0x7fffffffffffffff);
 if(half.d-diff.d<=0x1p-40)return 0;*value=q.f;return 1;
}
static void build(unsigned g,unsigned p,unsigned z){
 unsigned i,start=32+z*34,n=807-z*34;double lag=(double)m16_grid_lags[g][p],re,im,cr,ci;
 if(n>34)n=34;block.valid=1;block.g=g;block.p=p;block.z=z;block.start=start;block.end=start+n;m16_signature_recurrence_calls[0]+=n;m16_signature_recurrence_calls[2]++;
 if(lag==0.){
  for(i=0;i<n;i++){double phase=2.*PI*lag*(double)(start+i)/4096.;block.values[i*2]=1.f;block.values[i*2+1]=(float)phase;}
  m16_signature_recurrence_calls[4]+=n;return;
 }
 if(!ready[g][p]){double step=2.*PI*lag/4096.;rotor[g][p][0]=cos(step);rotor[g][p][1]=sin(step);ready[g][p]=1;m16_signature_recurrence_calls[3]++;}
 cr=rotor[g][p][0];ci=rotor[g][p][1];
 {double phase=2.*PI*lag*(double)start/4096.;re=cos(phase);im=sin(phase);}
 block.values[0]=(float)re;block.values[1]=(float)im;
 for(i=1;i<n;i++){
  double nr=re*cr-im*ci,ni=re*ci+im*cr;float fr,fi;
  if(!(rounding_safe(nr,&fr) && rounding_safe(ni,&fi))){
   double phase=2.*PI*lag*(double)(start+i)/4096.;nr=cos(phase);ni=sin(phase);fr=(float)nr;fi=(float)ni;m16_signature_recurrence_calls[1]++;
  }
  re=nr;im=ni;block.values[i*2]=fr;block.values[i*2+1]=fi;
 }
}
int m16_signature_recurrence(unsigned g,unsigned p,unsigned b,float *ri){
 unsigned k;if(g>=325 || p>=32 || b<32 || b>838 || !ri)return -1;
 if(!block.valid || block.g!=g || block.p!=p || b<block.start || b>=block.end)build(g,p,(b-32)/34);
 k=b-block.start;ri[0]=block.values[k*2];ri[1]=block.values[k*2+1];return 0;
}
int ENTRY(unsigned g,unsigned p,unsigned b,float *ri){return m16_signature_recurrence_enabled?m16_signature_recurrence(g,p,b,ri):ORIGINAL(g,p,b,ri);}
