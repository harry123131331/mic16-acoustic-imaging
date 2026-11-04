#include "acoustic_projection_policy.h"
#ifdef M16_AV_LLVM_HOST_NO_SDK
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern double fabs(double),atan(double),cos(double);
#else
#include <stdio.h>
#include <string.h>
#include <math.h>
#endif
static unsigned count;
#define CHECK(x) do{count++;if(!(x)){printf("FAILED_LINE %u\n",(unsigned)__LINE__);return 1;}}while(0)
static unsigned writes;static uint32_t bus[32];
static uint32_t rd(void *ctx,uint32_t offset){(void)ctx;return bus[offset/4];}
static void wr(void *ctx,uint32_t offset,uint32_t value){(void)ctx;bus[offset/4]=value;writes++;}
int main(void){
    m16_av_calibration c,bad;mic16_cycle_output cycle;
    acoustic_projection_policy policy,invalid;acoustic_projection_report report;
    m16_av_frame frame;int reason;unsigned i;double x,y,a,e;
    memset(&c,0,sizeof(c));memset(&cycle,0,sizeof(cycle));memset(&policy,0,sizeof(policy));
    c.calibrated=1;c.calibration_id=17;c.mount_id=23;c.video_mode_id=1;
    c.width=800;c.height=480;c.fx=c.fy=600;c.cx=400;c.cy=240;
    c.rotation[0]=c.rotation[4]=c.rotation[8]=1;
    /* 本例是合成相机坐标：向下为正，故相机低位对应t_y=-0.07米。 */
    c.translation_m[1]=-.07;c.translation_m[2]=-.1;
    cycle.cycle=1;cycle.count=1;cycle.tracks[0].source_id=9;cycle.tracks[0].confirmed_this_cycle=1;
    policy.policy_id=1;policy.mode=ACOUSTIC_KNOWN_PLANE;policy.assumption_acknowledged=1;
    policy.normal[2]=1;policy.offset_m=2;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0);
    CHECK(frame.points[0].xy==(400u|(218u<<16)) && report.approximate_points==1);
    CHECK(fabs(report.used_range_m[0]-2)<1e-12);
    cycle.tracks[0].azimuth=30;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0);
    CHECK(fabs(report.used_range_m[0]-2.3094010767585)<1e-12);
    invalid=policy;invalid.assumption_acknowledged=0;
    CHECK(acoustic_build_overlay(&c,&invalid,&cycle,23,1,100,101,500,&frame,&report)==M16_AV_INVALID && frame.flags==0);
    invalid=policy;invalid.normal[2]=2;
    CHECK(acoustic_build_overlay(&c,&invalid,&cycle,23,1,100,101,500,&frame,&report)==M16_AV_INVALID);
    invalid=policy;invalid.normal[0]=1;invalid.normal[2]=0;cycle.tracks[0].azimuth=0;
    CHECK(acoustic_build_overlay(&c,&invalid,&cycle,23,1,100,101,500,&frame,&report)==0 && report.omitted==1);
    policy.mode=ACOUSTIC_MEASURED_RANGE;policy.range_count=1;policy.source_ids[0]=9;policy.ranges_m[0]=1;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0 && report.approximate_points==0);
    CHECK((frame.points[0].xy>>16)==193);
    policy.source_ids[0]=10;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0 && report.point_reason[0]==M16_AV_NEEDS_RANGE);
    policy.mode=ACOUSTIC_DIRECTION_ONLY;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0 && report.omitted==1);
    c.allow_direction_only=1;
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==0 && (frame.points[0].xy>>16)==240);
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,600,500,&frame,&report)==M16_AV_STALE && frame.flags==0);
    cycle.count=2;cycle.tracks[1]=cycle.tracks[0];
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,100,101,500,&frame,&report)==M16_AV_INVALID && frame.flags==0);
    cycle.count=1;bad=c;bad.calibrated=0;
    CHECK(acoustic_build_overlay(&bad,&policy,&cycle,23,1,100,101,500,&frame,&report)==M16_AV_INVALID);
    bad=c;bad.rotation[0]=-1;
    CHECK(acoustic_build_overlay(&bad,&policy,&cycle,23,1,100,101,500,&frame,&report)==M16_AV_INVALID);
    bus[0]=M16_AV_MAGIC;bus[1]=M16_AV_VERSION;bus[2]=2;writes=0;
    CHECK(acoustic_publish_overlay(&c,&policy,&cycle,23,1,0,100,101,500,0,rd,wr,&reason,&report)==0 && reason==M16_AV_INVALID && bus[4]==0);
    bus[2]=3;writes=0;
    CHECK(acoustic_publish_overlay(&c,&policy,&cycle,23,1,1,100,101,500,0,rd,wr,&reason,&report)==1 && writes==0);
    /* 给现有RTL测试提供真实C计算的八个像素点；仍然只是合成标定。 */
    c.width=64;c.height=48;c.fx=c.fy=40;c.cx=32;c.cy=24;c.allow_direction_only=0;
    policy.mode=ACOUSTIC_KNOWN_PLANE;policy.normal[0]=policy.normal[1]=0;policy.normal[2]=1;
    cycle.count=8;cycle.cycle=42;
    for(i=0;i<8;i++){
        x=8+16*(i%4);y=i<4?10:32;
        a=atan((x-32)*1.9/(40*2));e=atan(((y-24)*1.9/40+.07)*cos(a)/2);
        cycle.tracks[i].source_id=16+i;cycle.tracks[i].azimuth=a/0.01745329251994329577;
        cycle.tracks[i].elevation=e/0.01745329251994329577;cycle.tracks[i].confirmed_this_cycle=i<7;
    }
    CHECK(acoustic_build_overlay(&c,&policy,&cycle,23,1,990,1000,110,&frame,&report)==0 && report.omitted==0);
    bus[2]=2;CHECK(m16_av_submit(&frame,0,rd,wr)==0);
    for(i=0;i<8;i++)CHECK((frame.points[i].xy&65535)==8+16*(i%4) && (frame.points[i].xy>>16)==(i<4?10u:32u));
    for(i=0;i<32;i++)printf("AV_FIXTURE %08x\n",bus[i]);
    printf("ACOUSTIC_PROJECTION_PASS checks=%u\n",count);return 0;
}
