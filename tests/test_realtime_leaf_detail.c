#include "mic16_feature_hotspots.h"
#include <stdio.h>
/* 独立检查新增粗/细粒度开关，避免跳过内层后破坏计时栈。 */
static uint64_t clock_value;
static unsigned checks,errors;
static uint64_t tick(void *p){(void)p;return clock_value+=10;}
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("LEAF_DETAIL_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
int main(void){
 m16_realtime_leaf_report r;
 m16_realtime_leaf_reset(tick,0);m16_realtime_leaf_detail(0);
 m16_realtime_leaf_begin(M16_HOT_DETECT);
 m16_realtime_leaf_begin(M16_HOT_ROTATE_ONLY);m16_realtime_leaf_end(M16_HOT_ROTATE_ONLY);
 m16_realtime_leaf_begin(M16_HOT_BAND_MAGNITUDE);m16_realtime_leaf_end(M16_HOT_BAND_MAGNITUDE);
 m16_realtime_leaf_end(M16_HOT_DETECT);
 CHECK(!m16_realtime_leaf_finish(&r));CHECK(r.reads==2 && !r.depth);
 CHECK(!r.items[M16_HOT_ROTATE_ONLY].calls && !r.items[M16_HOT_BAND_MAGNITUDE].calls);
 CHECK(r.items[M16_HOT_DETECT].inclusive_ticks==10 && r.items[M16_HOT_DETECT].exclusive_ticks==10);
 m16_realtime_leaf_reset(tick,0);m16_realtime_leaf_detail(1);
 m16_realtime_leaf_begin(M16_HOT_DETECT);
 m16_realtime_leaf_begin(M16_HOT_ROTATE_ONLY);m16_realtime_leaf_end(M16_HOT_ROTATE_ONLY);
 m16_realtime_leaf_begin(M16_HOT_BAND_MAGNITUDE);m16_realtime_leaf_end(M16_HOT_BAND_MAGNITUDE);
 m16_realtime_leaf_end(M16_HOT_DETECT);
 CHECK(!m16_realtime_leaf_finish(&r));CHECK(r.reads==6 && r.max_depth==2);
 CHECK(r.items[M16_HOT_ROTATE_ONLY].inclusive_ticks==10 && r.items[M16_HOT_BAND_MAGNITUDE].inclusive_ticks==10);
 CHECK(r.items[M16_HOT_DETECT].inclusive_ticks==50 && r.items[M16_HOT_DETECT].exclusive_ticks==30);
 m16_realtime_leaf_reset(0,0);m16_realtime_leaf_detail(1);
 m16_realtime_leaf_begin(M16_HOT_DETECT);m16_realtime_leaf_end(M16_HOT_DETECT);
 CHECK(!m16_realtime_leaf_finish(&r));CHECK(!r.reads && !r.depth && !r.errors);
 printf("REALTIME_LEAF_DETAIL_%s checks=%u errors=%u\n",errors?"FAIL":"PASS",checks,errors);
 return errors?1:0;
}
