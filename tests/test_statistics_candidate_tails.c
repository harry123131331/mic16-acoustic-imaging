#include "mic16_statistics_neon.h"
#include "mic16_pipeline_unit.h"
#include <stdint.h>
extern int printf(const char *,...),memcmp(const void *,const void *,unsigned long long);
extern int m16_original_statistics18_pair_cache(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
static mic16_extended_workspace w;static float old[16*20*18],current[16*20*18];static double dirs[32];
int measured_pipeline_observe(const float *a,const float *b,mic16_pipeline_observation *c,mic16_pipeline_workspace *d){(void)a;(void)b;(void)c;(void)d;return -1;}
static unsigned state=20261022u;static unsigned next(void){state=state*1664525u+1013904223u;return state;}
int main(void){unsigned pattern,n,i,errors=0,checks=0;double maximum=0.;
 for(pattern=0;pattern<4;pattern++){
  for(i=0;i<67*257*16*2;i++)w.observed[i]=pattern==0?0.:((int)(next()>>16)-32768)*(pattern==1?0x1p-40:pattern==2?0x1p-28:0x1p-15);
  for(i=0;i<16*67*64;i++)w.beam_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-20;
  for(i=0;i<67*64;i++)w.channel_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-20;
  for(i=0;i<64;i++)w.quiet_power[i]=pattern==0?0.:((next()>>16)+1)*0x1p-24;
  for(i=0;i<16;i++){dirs[i*2]=-55.+i*7.;dirs[i*2+1]=-20.+i*2.;}
  for(n=2;n<=16;n++){
   mic16_feature_reuse_report a={0},b={0};int ra=m16_original_statistics18_pair_cache(dirs,n,old,&w,&a),rb=m16_statistics_neon(dirs,n,current,&w,&b);
   double maximum_case=0.;checks++;errors+=ra!=rb || ra!=0 || a.pair_normalizations!=b.pair_normalizations;
   for(i=0;i<n*20*18;i++){double d=(double)old[i]-current[i];if(d<0.)d=-d;if(d>maximum_case)maximum_case=d;}
   if(maximum_case>maximum)maximum=maximum_case;errors+=maximum_case>5e-5;
   printf("STAT_TAIL pattern=%u count=%u difference=%.17g\n",pattern,n,maximum_case);
  }
 }
 checks+=4;errors+=m16_statistics_neon(dirs,1,current,&w,0)!=-1;errors+=m16_statistics_neon(0,2,current,&w,0)!=-1;errors+=m16_statistics_neon(dirs,2,0,&w,0)!=-1;errors+=m16_statistics_neon_select(2)!=-1;
 printf("STAT_TAIL_COMPLETE checks=%u errors=%u max=%.17g\n",checks,errors,maximum);return errors?1:0;
}