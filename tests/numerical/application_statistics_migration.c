/* 原六类数值窗：原FP64基准→完整作用域加速→原基准恢复。不是房间准确率或ARM计时。 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "mic16_application_acceleration.h"
#include "mic16_stft_batch.h"
#include "mic16_statistics_neon.h"
static float x[40960*16],q[8192*16],wave[3][41152];
static mic16_pipeline_workspace w;
static mic16_pipeline_observation o,saved;
static unsigned short best[480];
static const unsigned probes[10]={0,23,24,55,63,87,88,105,106,129};
int m16_application_test_environment(unsigned *f,unsigned *c){*f=0;*c=15u<<20;return 1;}
static unsigned next(unsigned *s){*s=*s*1664525u+1013904223u;return *s;}
static float noise(unsigned *s){return ((int)(next(s)>>16)-32768)*0x1p-15f;}
static unsigned hash(const float *a,unsigned n){unsigned i,h=2166136261u;union{float f;unsigned u;}v;for(i=0;i<n;i++){v.f=a[i];h=(h^v.u)*16777619u;}return h;}
static double difference(double a,double b){double d=a-b;return d<0.?-d:d;}
static void prepare(unsigned scenario){
 unsigned i,c,s,state=202610220u+scenario,quiet_state=412001u+scenario,ns=scenario==0?1:scenario==2?3:scenario==4?0:2;
 double dirs[3][2]={{-35.,5.},{30.,0.},{40.,10.}},gains[3]={.08,.05,.035};float arrival[3][16],lags[32];
 if(scenario==0){dirs[0][0]=-25.;dirs[0][1]=10.;}
 if(scenario==2){dirs[0][0]=-40.;dirs[0][1]=10.;dirs[1][0]=0.;dirs[1][1]=-5.;}
 if(scenario==3){gains[0]=.1;gains[1]=.001;}
 for(s=0;s<ns;s++){
  mic16_candidate_geometry(dirs[s],arrival[s],lags);
  for(i=0;i<41152;i++)wave[s][i]=scenario==5?(float)sin(2.*3.14159265358979323846*(s?2200.:1000.)*i/39062.5):noise(&state);
 }
 for(i=0;i<8192*16;i++)q[i]=noise(&quiet_state)*1e-4f;
 for(i=0;i<40960;i++)for(c=0;c<16;c++){
  double v=(double)noise(&state)*(scenario==3?1e-4:.001);
  for(s=0;s<ns;s++){double t=(double)i+64.-(double)arrival[s][c];unsigned lo=(unsigned)floor(t);double f=t-lo;
   v+=gains[s]*((double)wave[s][lo]+((double)wave[s][lo+1]-(double)wave[s][lo])*f);}
  x[i*16+c]=(float)v;
 }
}
int main(void){unsigned scenario,mode,errors=0,statistics_total=0;
 for(scenario=0;scenario<6;scenario++){
  unsigned h,hq;prepare(scenario);h=hash(x,40960*16);hq=hash(q,8192*16);
  for(mode=0;mode<3;mode++){
   unsigned i,t,k,idx=0,gate=0;double md=0.,ml=0.,ms=0.,mp=0.,mf=0.;int rc;
   rc=mode==1?__wrap_mic16_pipeline_observe_batched_timed(x,q,&o,&w,0,0):mic16_pipeline_observe_batched_timed(x,q,&o,&w,0,0);
   if(rc){printf("SCOPE_MIGRATION_ERROR case=%u mode=%u rc=%d\n",scenario,mode,rc);return 2;}
   if(!mode){memcpy(&saved,&o,sizeof(o));memcpy(best,w.best_indices,sizeof(best));}
   else if(mode==2){gate=memcmp(&saved,&o,sizeof(o))!=0;}
   else{
    for(i=0;i<480;i++)idx+=best[i]!=w.best_indices[i];
    gate+=o.candidate_count!=saved.candidate_count;
    for(i=0;i<32;i++){double a=difference(o.directions[i],saved.directions[i]);if(a>md)md=a;gate+=!isfinite(o.directions[i]) || !isfinite(saved.directions[i]);}
    for(i=0;i<16;i++){
     double a=difference(o.logits[i],saved.logits[i]),b=difference(o.scores[i],saved.scores[i]);if(a>ml)ml=a;if(b>ms)ms=b;
     gate+=!isfinite(o.logits[i]) || !isfinite(o.scores[i]) || !isfinite(saved.logits[i]) || !isfinite(saved.scores[i]);
     gate+=o.valid[i]!=saved.valid[i] || ((o.scores[i]>=.65)!=(saved.scores[i]>=.65));
     for(t=0;t<20;t++)for(k=0;k<10;k++){unsigned pos=(i*20+t)*130+probes[k];a=difference(o.features[pos],saved.features[pos]);if(a>mp)mp=a;}
    }
    for(i=0;i<16*20*130;i++){double a=difference(o.features[i],saved.features[i]);if(a>mf)mf=a;gate+=!isfinite(o.features[i]) || !isfinite(saved.features[i]);}
    if(o.candidate_count>1 && !g_mic16_application_acceleration.statistics[2])gate++;
    statistics_total+=g_mic16_application_acceleration.statistics[2];
   }
   gate+=idx || md>1e-4 || ml>1e-4 || ms>3e-5 || mp>5e-5;
   gate+=h!=hash(x,40960*16) || hq!=hash(q,8192*16) || m16_statistics_neon_mode;
   errors+=gate;
   printf("SCOPE_MIGRATION case=%u mode=%u count=%u indices=%u errors=%u direction=%.17g logit=%.17g score=%.17g probes=%.17g features=%.17g pcm=%u reference=%u\n",scenario,mode,o.candidate_count,idx,gate,md,ml,ms,mp,mf,h,hq);
  }
 }
 printf("SCOPE_MIGRATION_COMPLETE errors=%u statistics_groups=%u NOT_ROOM_ACCURACY_OR_ARM_TIMING\n",errors,statistics_total);
 return errors || !statistics_total?1:0;
}
