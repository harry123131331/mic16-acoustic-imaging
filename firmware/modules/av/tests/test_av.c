/* 只测试合成几何和协议，不代表真实相机标定或模型执行。 */
#include "mic16_av.h"
#ifdef M16_AV_LLVM_HOST_NO_SDK
#include <stddef.h>
extern double atan(double),cos(double),fabs(double);
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
#else
#include <math.h>
#include <stdio.h>
#include <string.h>
#endif
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL line %u\n",(unsigned)__LINE__);return 1;}}while(0)
static uint32_t bus[64],writes;
static uint32_t rd(void *ctx,uint32_t off){(void)ctx;return bus[off/4];}
static void wr(void *ctx,uint32_t off,uint32_t v){(void)ctx;bus[off/4]=v;writes++;}
int main(void){
    m16_av_calibration c,bad;
    m16_av_pixel p;
    mic16_cycle_output cycle;
    m16_av_frame f,fixture;uint32_t omitted;unsigned i;
    double x,y,a,e;
    memset(&c,0,sizeof(c));memset(&cycle,0,sizeof(cycle));
    c.calibrated=1;c.calibration_id=17;c.mount_id=23;c.video_mode_id=1;c.width=64;c.height=48;
    c.fx=40;c.fy=40;c.cx=32;c.cy=24;c.allow_direction_only=1;
    c.rotation[0]=c.rotation[4]=c.rotation[8]=1;
    CHECK(m16_av_validate(&c)==0);
    CHECK(m16_av_project(&c,0,0,0,&p)==0 && p.x==32 && p.y==24 && p.approximate==1);
    cycle.cycle=42;cycle.count=8;cycle.confirmed_count=7;
    for(i=0;i<8;i++){
        x=8+16*(i%4);y=i<4?10:32;
        a=atan((x-c.cx)/c.fx);e=atan((y-c.cy)*cos(a)/c.fy);
        cycle.tracks[i].source_id=16+i;cycle.tracks[i].azimuth=a/0.01745329251994329577;
        cycle.tracks[i].elevation=e/0.01745329251994329577;cycle.tracks[i].confirmed_this_cycle=i<7;
    }
    CHECK(m16_av_build(&c,&cycle,23,1,990,1000,110,&f,&omitted)==0 && omitted==0 && f.remaining_ms==100);
    /* Y在高半字，检查字段时避免窄化或回绕。 */
    for(i=0;i<8;i++){
        CHECK((f.points[i].xy&65535u)==8+16*(i%4));
        CHECK((f.points[i].xy>>16)==(i<4?10u:32u));
        CHECK((f.points[i].attr&65535u)==16+i);
        CHECK(!!(f.points[i].attr&M16_AV_POINT_CURRENT)==(i<7));
    }
    fixture=f;
    CHECK(m16_av_build(&c,&cycle,23,1,990,1100,110,&f,&omitted)==M16_AV_STALE && f.flags==0);
    CHECK(m16_av_build(&c,&cycle,23,1,1001,1000,110,&f,&omitted)==M16_AV_STALE);
    CHECK(m16_av_build(&c,&cycle,24,1,990,1000,110,&f,&omitted)==M16_AV_INVALID);
    CHECK(m16_av_build(&c,&cycle,23,2,990,1000,110,&f,&omitted)==M16_AV_INVALID);
    cycle.tracks[7].source_id=16;
    CHECK(m16_av_build(&c,&cycle,23,1,990,1000,110,&f,&omitted)==M16_AV_INVALID && f.flags==0);
    cycle.tracks[7].source_id=23;cycle.count=9;
    CHECK(m16_av_build(&c,&cycle,23,1,990,1000,110,&f,&omitted)==M16_AV_INVALID);
    bad=c;bad.calibrated=0;CHECK(m16_av_validate(&bad)==M16_AV_INVALID);
    bad=c;bad.rotation[0]=-1;CHECK(m16_av_validate(&bad)==M16_AV_INVALID);
    bad=c;bad.rotation[1]=.1;CHECK(m16_av_validate(&bad)==M16_AV_INVALID);
    bad=c;bad.fx=0;CHECK(m16_av_validate(&bad)==M16_AV_INVALID);
    bad=c;bad.allow_direction_only=0;CHECK(m16_av_project(&bad,0,0,0,&p)==M16_AV_NEEDS_RANGE);
    bad=c;bad.translation_m[0]=.1;
    CHECK(m16_av_project(&bad,0,0,2,&p)==0 && fabs(p.x-34)<1e-12 && p.approximate==0);
    CHECK(m16_av_project(&bad,0,0,0,&p)==0 && p.x==32 && p.approximate==1);
    bad=c;bad.rotation[0]=bad.rotation[8]=-1;
    CHECK(m16_av_project(&bad,0,0,0,&p)==M16_AV_BEHIND);
    CHECK(m16_av_project(&c,60,0,0,&p)==M16_AV_OUTSIDE);
    CHECK(m16_av_project(&c,61,0,0,&p)==M16_AV_INVALID);
    CHECK(m16_av_project(&c,0,31,0,&p)==M16_AV_INVALID);
    bad=c;bad.distortion[0]=.1;a=atan(.25);e=atan(.1*cos(a));
    CHECK(m16_av_project(&bad,a/0.01745329251994329577,e/0.01745329251994329577,0,&p)==0);
    CHECK(fabs(p.x-42.0725)<1e-10 && fabs(p.y-28.029)<1e-10);
    CHECK(m16_av_project(&c,0,0,-1,&p)==M16_AV_INVALID);
    bus[0]=M16_AV_MAGIC;bus[1]=M16_AV_VERSION;bus[2]=3;writes=0;
    CHECK(m16_av_submit(&fixture,0,rd,wr)==1 && writes==0);
    bus[2]=0;CHECK(m16_av_submit(&fixture,0,rd,wr)==-1 && writes==0);
    bus[2]=2;CHECK(m16_av_submit(&fixture,0,rd,wr)==0 && writes==21 && bus[3]==1);
    CHECK(bus[4]==3 && bus[5]==100 && bus[6]==42 && bus[7]==17);
    for(i=0;i<8;i++)CHECK(bus[16+2*i]==fixture.points[i].xy && bus[17+2*i]==fixture.points[i].attr);
    for(i=0;i<32;i++)printf("AV_FIXTURE %08x\n",bus[i]);
    printf("AV_C_PASS checks=%u fixture=synthetic_only\n",checks);return 0;
}
