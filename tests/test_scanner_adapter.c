#include "mic16_scanner_hls.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
static m16_scanner_workspace w __attribute__((aligned(64)));
static float x[M16_SP_PHAT_FLOATS],maps[M16_SP_MAP_FLOATS],lags[325*32];
static unsigned checks,errors,starts,flushes,invalidates,busy,timeout,corrupt,running;
static unsigned reg_values[32];static uint64_t tick;
#define CHECK(c) do{checks++;if(!(c)){errors++;printf("SCANNER_UNIT_ERROR line=%u\n",(unsigned)__LINE__);}}while(0)
const float *mic16_spatial_lags(void){return lags;}
static uint32_t rd(void *c,unsigned offset){(void)c;(void)offset;if(busy)return 0;if(running && timeout)return 1;running=0;return starts?6:4;}
static void wr(void *c,unsigned offset,uint32_t value){unsigned i;(void)c;reg_values[offset/4]=value;
    if(!offset && value==1){starts++;running=1;
        if(!timeout){for(i=M16_SCAN_OUTPUT_OFFSET;i<M16_SCAN_WORDS;i++)w.arena.words[i]=0;
            if(corrupt)w.arena.after[0]=0;}}
}
static void fl(void *c,const void *p,unsigned n){(void)c;(void)p;(void)n;flushes++;}
static void inv(void *c,const void *p,unsigned n){(void)c;(void)p;(void)n;CHECK(!running);invalidates++;}
static uint64_t clk(void *c){(void)c;return ++tick;}
static void fence(void *c){(void)c;}
static m16_scanner_io io={0,rd,wr,fl,inv,clk,fence,0x02000000u,100};
static void reset(void){memset(&w,0,sizeof(w));memset(reg_values,0,sizeof(reg_values));starts=flushes=invalidates=busy=timeout=corrupt=running=0;tick=0;}
int main(void){int32_t raw=123;unsigned i;int rc;m16_scanner_report report;union{uint32_t u;float f;} bad;
    CHECK(m16_scanner_quantize(1.f,&raw)==0 && raw==16777216);
    CHECK(m16_scanner_quantize(-1.f,&raw)==0 && raw==-16777216);
    CHECK(m16_scanner_quantize(0x1p-25f,&raw)==0 && raw==0);
    CHECK(m16_scanner_quantize(0x1.8p-24f,&raw)==0 && raw==2);
    CHECK(m16_scanner_quantize(-0x1.8p-24f,&raw)==0 && raw==-2);
    CHECK(m16_scanner_pack(-1,1)==UINT64_C(0x07ffffff));
    bad.u=0x7fc00000;CHECK(m16_scanner_quantize(bad.f,&raw)<0);
    CHECK(m16_scanner_quantize(1.1f,&raw)<0);
    reset();CHECK(m16_scanner_init(&w,&io)==0);
    CHECK(w.arena.words[M16_SCAN_PHASE_OFFSET]==16777216u);
    CHECK(w.arena.words[M16_SCAN_STEP_OFFSET]==16777216u);
    rc=m16_scanner_project(&w,x,maps,&report);
    CHECK(rc==0 && starts==20 && invalidates==20 && flushes==21 && report.completed_frames==20);
    CHECK(reg_values[0x10/4]==io.arena_address && reg_values[0x14/4]==0);
    CHECK(reg_values[0x1c/4]==0 && reg_values[0x24/4]==M16_SCAN_PHASE_OFFSET);
    CHECK(reg_values[0x2c/4]==M16_SCAN_STEP_OFFSET && reg_values[0x34/4]==M16_SCAN_OUTPUT_OFFSET);
    for(i=0;i<M16_SP_MAP_FLOATS;i++)if(maps[i]!=0){errors++;break;}checks++;
    x[0]=bad.f;i=starts;CHECK(m16_scanner_project(&w,x,maps,&report)==M16_SCAN_INVALID && starts==i);x[0]=0;
    CHECK(m16_scanner_project(&w,x,x,&report)==M16_SCAN_INVALID);
    reset();CHECK(m16_scanner_init(&w,&io)==0);timeout=1;
    CHECK(m16_scanner_project(&w,x,maps,&report)==M16_SCAN_TIMEOUT && starts==1 && invalidates==0 && w.quarantined);
    CHECK(m16_scanner_init(&w,&io)==M16_SCAN_INVALID);
    CHECK(m16_scanner_project(&w,x,maps,&report)==M16_SCAN_INVALID && starts==1);
    reset();CHECK(m16_scanner_init(&w,&io)==0);busy=1;
    CHECK(m16_scanner_project(&w,x,maps,&report)==M16_SCAN_BUSY && !starts && w.quarantined);
    reset();CHECK(m16_scanner_init(&w,&io)==0);corrupt=1;
    CHECK(m16_scanner_project(&w,x,maps,&report)==M16_SCAN_GUARD && starts==1 && w.quarantined);
    printf("SCANNER_ADAPTER_UNIT checks=%u errors=%u REGISTER_AND_OWNERSHIP_MOCK_NOT_RTL_NUMERICS\n",checks,errors);
    return errors?1:0;
}
