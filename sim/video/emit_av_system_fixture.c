/* 合成几何与周期样本，仅由主机LLVM执行；不是学习模型输出，也不是真实相机标定。 */
#include "mic16_av_policy.h"
#include <stddef.h>
extern double atan(double),cos(double);
extern void *memset(void *,int,size_t);
extern int printf(const char *,...);
static unsigned which,writes;
static uint32_t rd(void *ctx,uint32_t offset) {
    (void)ctx;return offset==0?M16_AV_MAGIC:offset==4?M16_AV_VERSION:2;
}
static void wr(void *ctx,uint32_t offset,uint32_t value) {
    (void)ctx;printf("AV_SYSTEM_WRITE %u %08x %08x\n",which,offset,value);writes++;
}
int main(void) {
    m16_av_calibration c;mic16_cycle_output event;
    int reason,rc;uint32_t omitted;unsigned i;double az,el;
    memset(&c,0,sizeof(c));memset(&event,0,sizeof(event));
    c.calibrated=1;c.calibration_id=17;c.mount_id=2;c.video_mode_id=1;
    c.width=800;c.height=480;c.fx=400;c.fy=400;c.cx=400;c.cy=240;
    c.rotation[0]=c.rotation[4]=c.rotation[8]=1;c.allow_direction_only=1;
    event.cycle=1;event.count=2;event.confirmed_count=1;
    for(i=0;i<2;i++) {
        double x=i?560:240,y=i?300:80;
        az=atan((x-c.cx)/c.fx);el=atan((y-c.cy)*cos(az)/c.fy);
        event.tracks[i].azimuth=az/0.01745329251994329577;
        event.tracks[i].elevation=el/0.01745329251994329577;
        event.tracks[i].source_id=i?0x22:0x11;event.tracks[i].confirmed_this_cycle=i==0;
    }
    for(which=0;which<4;which++) {
        event.cycle=which+1;c.calibrated=which!=1;writes=0;
        rc=m16_av_publish_checked(&c,&event,2,1,1,1000,which==3?18000:1000,
            which==2?23:1000,0,rd,wr,&reason,&omitted);
        if(rc || writes!=21 || omitted || reason!=(which==1?M16_AV_INVALID:which==3?M16_AV_STALE:0))return 1;
        printf("AV_SYSTEM_CASE %u reason=%d writes=%u synthetic_only=1\n",which,reason,writes);
    }
    printf("AV_SYSTEM_FIXTURE_PASS cases=4 writes=84 model_forwards=0\n");return 0;
}
