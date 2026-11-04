#include "mic16_scanner_hls.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern int memcmp(const void *,const void *,size_t);
static float x[M16_SP_PHAT_FLOATS],baseline[M16_SP_MAP_FLOATS],accelerated[M16_SP_MAP_FLOATS];
static mic16_spatial_workspace cpu,refiner;
static double levels[480],floor_db[24],before_votes[480*9],after_votes[480*9];
static unsigned short before_indices[480],after_indices[480];
static unsigned checks,errors;
#define CHECK(c) do{checks++;if(!(c)){errors++;printf("SCANNER_REFINE_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
int main(void){unsigned i,state=20261020u;m16_scanner_refinement r;
    for(i=0;i<M16_SP_PHAT_FLOATS;i++){state=1664525u*state+1013904223u;x[i]=((int)((state>>16)&65535)-32768)*0x1p-16f;}
    CHECK(mic16_spatial_project(x,baseline,&cpu)==0);
    for(i=0;i<M16_SP_MAP_FLOATS;i++){state=1664525u*state+1013904223u;accelerated[i]=baseline[i]+((int)((state>>16)&65535)-32768)*0x1p-32f;}
    CHECK(m16_scanner_refine(x,accelerated,&refiner,&r)==0 && !r.zero_input);
    CHECK(mic16_spatial_votes(baseline,levels,floor_db,before_votes,before_indices)==0);
    CHECK(mic16_spatial_votes(accelerated,levels,floor_db,after_votes,after_indices)==0);
    CHECK(memcmp(before_indices,after_indices,sizeof(before_indices))==0);
    CHECK(memcmp(before_votes,after_votes,sizeof(before_votes))==0);
    CHECK(r.refined_grids>0 && r.refined_grids<=24u*325u);
    CHECK(m16_scanner_refine(x,x,&refiner,&r)<0);
    for(i=0;i<M16_SP_PHAT_FLOATS;i++)x[i]=0;
    CHECK(m16_scanner_refine(x,accelerated,&refiner,&r)==0 && r.zero_input && !r.refined_grids);
    for(i=0;i<M16_SP_MAP_FLOATS;i++)if(accelerated[i]!=0){errors++;break;}checks++;
    printf("SCANNER_REFINE_UNIT checks=%u errors=%u REAL_CPU_VOTES_WITH_CONTROLLED_MAP_PERTURBATION\n",checks,errors);return errors?1:0;
}
