#include "mic16_scanner_accumulators.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
static float x[M16_SP_PHAT_FLOATS],coarse[M16_SP_MAP_FLOATS],a[M16_SP_MAP_FLOATS],b[M16_SP_MAP_FLOATS];
static mic16_spatial_workspace oracle,wa,wb;
static unsigned checks,errors,hooks,scenario;
void m16_cooperate(void){hooks++;}
#define CHECK(c) do{checks++;if(!(c)){errors++;printf("SCANNER_ACCUM_ERROR case=%u line=%u\n",scenario,(unsigned)__LINE__);}}while(0)
static void compare(unsigned fallback){
 m16_scanner_refinement ra={0,0,0},rb={0,0,0};unsigned ha,hb;
 memcpy(a,coarse,sizeof(a));memcpy(b,coarse,sizeof(b));memset(&wa,0,sizeof(wa));memset(&wb,0,sizeof(wb));
 hooks=0;CHECK(m16_scanner_refine(x,a,&wa,&ra)==0);ha=hooks;
 hooks=0;CHECK(m16_scanner_refine_accumulators(x,b,&wb,&rb)==0);hb=hooks;
 CHECK(memcmp(a,b,sizeof(a))==0);CHECK(memcmp(&wa,&wb,sizeof(wa))==0);CHECK(memcmp(&ra,&rb,sizeof(ra))==0);CHECK(ha==hb);
 if(fallback)CHECK(ra.full_zones==24 && ra.refined_grids==24u*325u);
 printf("SCANNER_ACCUM_CASE case=%u refined=%u fallback=%u hooks=%u\n",scenario,ra.refined_grids,ra.full_zones,ha);
}
int main(void){
 unsigned i,t,z,p,j,state=20261021u;union{unsigned u;float f;} bad;
 scenario=0;for(i=0;i<M16_SP_MAP_FLOATS;i++)coarse[i]=.2f;compare(0);
 scenario=1;for(i=0;i<M16_SP_PHAT_FLOATS;i++){state=1664525u*state+1013904223u;x[i]=((int)((state>>16)&65535)-32768)*0x1p-16f;}
 CHECK(mic16_spatial_project(x,coarse,&oracle)==0);
 for(i=0;i<M16_SP_MAP_FLOATS;i++){state=1664525u*state+1013904223u;coarse[i]+=((int)((state>>16)&65535)-32768)*0x1p-32f;}
 compare(0);
 scenario=2;for(i=0;i<M16_SP_MAP_FLOATS;i++)coarse[i]=.2f;
 for(t=0;t<20;t++)for(z=0;z<24;z++){coarse[(t*24+z)*325]=1.f;coarse[(t*24+z)*325+324]=.9f;}
 compare(1);
 scenario=3;for(t=0;t<20;t++)for(j=0;j<807;j++)for(p=0;p<32;p++){
  float ri[2];if(mic16_spatial_signature(162,p,32+j,ri))return 3;i=((t*807+j)*32+p)*2;x[i]=ri[0];x[i+1]=ri[1];
 }
 CHECK(mic16_spatial_project(x,coarse,&oracle)==0);compare(0);
 scenario=4;bad.u=0x7fc00001u;x[13]=bad.f;memcpy(a,coarse,sizeof(a));memcpy(b,coarse,sizeof(b));
 {m16_scanner_refinement ra={17,18,19},rb=ra;
  CHECK(m16_scanner_refine(x,a,&wa,&ra)==-1);CHECK(m16_scanner_refine_accumulators(x,b,&wb,&rb)==-1);
  CHECK(memcmp(a,coarse,sizeof(a))==0 && memcmp(b,coarse,sizeof(b))==0);CHECK(ra.refined_grids==17 && rb.refined_grids==17);
  CHECK(m16_scanner_refine_accumulators(x,x,&wb,&rb)==-1);CHECK(m16_scanner_refine_accumulators(0,b,&wb,&rb)==-1);
 }
 scenario=5;x[13]=0.f;bad.u=0x7f800000u;a[11]=bad.f;b[11]=bad.f;
 {m16_scanner_refinement ra={17,18,19},rb=ra;
  CHECK(m16_scanner_refine(x,a,&wa,&ra)==-1);CHECK(m16_scanner_refine_accumulators(x,b,&wb,&rb)==-1);
  CHECK(memcmp(a,b,sizeof(a))==0 && ra.refined_grids==17 && rb.refined_grids==17);
 }
 printf("SCANNER_ACCUMULATORS_PASS checks=%u cases=6 errors=%u BIT_EXACT_MAP_WORKSPACE_REPORT_AND_CHECKPOINT_COUNT\n",checks,errors);
 return errors?1:0;
}
