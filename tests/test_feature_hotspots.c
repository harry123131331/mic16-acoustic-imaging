#include "mic16_feature_hotspots.h"
extern int printf(const char *,...);
static uint64_t now;
static unsigned checks,errors;
static uint64_t tick(void *unused){(void)unused;return now+=10;}
static uint64_t backwards(void *unused){(void)unused;return --now;}
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("HOTSPOT_UNIT_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
int main(void){
 mic16_hotspot_report r;unsigned i;uint64_t sum;
 mic16_hotspot_reset(tick,0);mic16_hotspot_begin(0);mic16_hotspot_begin(1);
 mic16_hotspot_end(1);mic16_hotspot_begin(2);mic16_hotspot_end(2);mic16_hotspot_end(0);
 CHECK(!mic16_hotspot_finish(&r));CHECK(r.reads==6 && r.depth==0 && r.max_depth==2);
 CHECK(r.items[0].inclusive_ticks==50 && r.items[0].exclusive_ticks==30);
 CHECK(r.items[1].inclusive_ticks==10 && r.items[2].inclusive_ticks==10);
 sum=0;for(i=0;i<M16_HOT_COUNT;i++)sum+=r.items[i].exclusive_ticks;
 CHECK(sum==r.items[0].inclusive_ticks && r.items[0].calls==1);
 mic16_hotspot_begin(0);CHECK(!mic16_hotspot_finish(&r) && r.reads==6);
 mic16_hotspot_reset(0,0);mic16_hotspot_begin(0);mic16_hotspot_end(0);CHECK(!mic16_hotspot_finish(&r) && !r.reads);
 mic16_hotspot_reset(tick,0);mic16_hotspot_end(0);CHECK(mic16_hotspot_finish(&r)<0 && r.errors);
 mic16_hotspot_reset(tick,0);mic16_hotspot_begin(M16_HOT_COUNT);CHECK(mic16_hotspot_finish(&r)<0 && r.errors);
 mic16_hotspot_reset(tick,0);mic16_hotspot_begin(0);mic16_hotspot_end(1);CHECK(mic16_hotspot_finish(&r)<0 && r.errors);
 mic16_hotspot_reset(tick,0);for(i=0;i<9;i++)mic16_hotspot_begin(0);CHECK(mic16_hotspot_finish(&r)<0 && r.max_depth==8);
 now=100;mic16_hotspot_reset(backwards,0);mic16_hotspot_begin(0);mic16_hotspot_end(0);CHECK(mic16_hotspot_finish(&r)<0 && r.errors);
 mic16_hotspot_reset(tick,0);mic16_hotspot_begin(0);CHECK(mic16_hotspot_finish(&r)<0 && r.depth==1);
 CHECK(mic16_hotspot_finish(0)<0);
 printf("HOTSPOT_UNIT_%s checks=%u errors=%u items=%u metadata_bytes=%u\n",errors?"FAIL":"PASS",checks,errors,M16_HOT_COUNT,(unsigned)sizeof(r));
 return errors?1:0;
}
