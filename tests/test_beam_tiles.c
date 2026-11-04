#include "mic16_beam_tiles.h"
#include <stddef.h>
extern int printf(const char *,...),memcmp(const void *,const void *,size_t);
extern int m16_original_candidate_beam_phase_fast(const float *,unsigned,const double *,float *,unsigned *);
unsigned m16_feature_fast_mode,m16_feature_fast_calls[3],cooperate_calls;void m16_cooperate(void){cooperate_calls++;}
static float x[40960*16],a[40960],b[40960];
static double dirs[10][2]={{0.,0.},{-60.,-30.},{60.,30.},{-35.,5.},{30.,0.},{-40.,10.},{0.,-5.},{40.,10.},{1e-7,1e-4},{-59.9,29.9}};
int main(void){unsigned scenario,n,i,d,checks=0,errors=0,state=20261022u;union{float f;unsigned u;}v;
 for(scenario=0;scenario<6;scenario++)for(n=8192;n<=40960;n+=32768){
  for(i=0;i<n*16;i++){state=state*1664525u+1013904223u;
   if(scenario==4)v.u=(state&0x80000000u)|((state>>16)&8191u);else if(scenario==5)v.u=state&0x80000000u;
   x[i]=scenario==0?((int)(state>>16)-32768)*0x1p-15f:scenario==1?0.f:scenario==2?(i<16 || i>=n*16-16?1.f:0.f):scenario==3?(i&1?-1.f:1.f):v.f;
  }
  for(d=0;d<10;d++){unsigned ga=123,gb=123,ca,cb;int ra,rb;cooperate_calls=0;ra=m16_original_candidate_beam_phase_fast(x,n,dirs[d],a,&ga);ca=cooperate_calls;
   cooperate_calls=0;rb=m16_beam_tiles(x,n,dirs[d],b,&gb);cb=cooperate_calls;checks++;
   if(ra!=rb || ga!=gb || (ra==0 && memcmp(a,b,n*4u)) || ca!=cb){printf("BEAM_TILES_DIFF case=%u n=%u direction=%u rc=%d/%d guard=%u/%u cooperate=%u/%u\n",scenario,n,d,ra,rb,ga,gb,ca,cb);errors++;}
  }
 }
 v.u=0x7fc00001u;x[0]=v.f;b[0]=123.;{unsigned g=123;checks++;errors+=m16_beam_tiles(x,40960,dirs[0],b,&g)!=-1 || b[0]!=123. || g!=123;}
 printf("BEAM_TILES_%s checks=%u errors=%u\n",errors?"FAIL":"PASS",checks,errors);return errors?1:0;
}
