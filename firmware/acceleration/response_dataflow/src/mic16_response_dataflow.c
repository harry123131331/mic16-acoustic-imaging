#include "mic16_response_dataflow.h"
#include "mic16_cooperate.h"
extern double sin(double),cos(double);
#define PI 3.14159265358979323846264338327950288
static float frame_major[807*32][20*2] __attribute__((aligned(64)));
static const mic16_physical_workspace *owner;
unsigned m16_response_dataflow_mode,m16_response_dataflow_calls[3];
void m16_response_dataflow_begin(void){unsigned i;owner=0;for(i=0;i<3;i++)m16_response_dataflow_calls[i]=0;}
int m16_response_dataflow_select(unsigned mode){if(mode>1)return -1;m16_response_dataflow_mode=mode;m16_response_dataflow_begin();return 0;}
void m16_response_dataflow(const float *lags,float *out,mic16_physical_workspace *w){
 unsigned i,j,z,p,b,t;
 if(owner!=w){
  /* 每32个复数一块，转置只做一次；下一个候选直接复用。 */
  for(i=0;i<807u*32u;i+=32){M16_COOPERATE();for(t=0;t<20;t++)for(j=i;j<i+32;j++){
   frame_major[j][t*2]=w->phat[(t*807u*32u+j)*2];frame_major[j][t*2+1]=w->phat[(t*807u*32u+j)*2+1];
  }}
  owner=w;m16_response_dataflow_calls[0]++;
 }
 m16_response_dataflow_calls[1]++;
 for(z=0;z<24;z++){
  unsigned start=z*34,n=807-start;if(n>34)n=34;M16_COOPERATE();
  for(p=0;p<32;p++)for(b=0;b<n;b++){
   double phase=2.*PI*(double)lags[p]*(double)(32+start+b)/4096.;
   w->signature[(p*34+b)*2]=(float)cos(phase);w->signature[(p*34+b)*2+1]=(float)sin(phase);
  }
  /* 四个独立FP64累加器交错，不重排任一帧的p/b顺序，不使用FMA或快速数学。 */
  for(t=0;t<20;t+=4){
   double a0=0.,a1=0.,a2=0.,a3=0.;
   for(p=0;p<32;p++)for(b=0;b<n;b++){
    const float *v=frame_major[(start+b)*32+p]+t*2;
    double sr=w->signature[(p*34+b)*2],si=w->signature[(p*34+b)*2+1];
    if(!b && !(p&3u))M16_COOPERATE();
    a0+=sr*(double)v[0]+si*(double)v[1];a1+=sr*(double)v[2]+si*(double)v[3];
    a2+=sr*(double)v[4]+si*(double)v[5];a3+=sr*(double)v[6]+si*(double)v[7];
   }
   out[t*88+z]=(float)a0/(float)(32*n);out[(t+1)*88+z]=(float)a1/(float)(32*n);
   out[(t+2)*88+z]=(float)a2/(float)(32*n);out[(t+3)*88+z]=(float)a3/(float)(32*n);
   m16_response_dataflow_calls[2]++;
  }
 }
}
