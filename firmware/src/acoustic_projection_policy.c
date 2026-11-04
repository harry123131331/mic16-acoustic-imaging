#include "acoustic_projection_policy.h"
#ifdef M16_AV_LLVM_HOST_NO_SDK
#include <stddef.h>
extern double sin(double),cos(double),sqrt(double),floor(double),fabs(double);
extern void *memset(void *,int,size_t);
#else
#include <math.h>
#include <string.h>
#endif
static int finite_value(double x){return x==x && fabs(x)<=1.7976931348623157e308;}
static int valid_policy(const acoustic_projection_policy *p){
    unsigned i,j;double norm;
    if(!p || !p->policy_id || p->mode>ACOUSTIC_KNOWN_PLANE || p->range_count>M16_AV_POINTS)return 0;
    if(p->mode==ACOUSTIC_KNOWN_PLANE){
        if(p->assumption_acknowledged!=1 || !finite_value(p->offset_m) || p->offset_m<=0)return 0;
        norm=0;for(i=0;i<3;i++){if(!finite_value(p->normal[i]))return 0;norm+=p->normal[i]*p->normal[i];}
        if(fabs(norm-1.)>1e-6)return 0;
    }
    if(p->mode==ACOUSTIC_MEASURED_RANGE)for(i=0;i<p->range_count;i++){
        if(!p->source_ids[i] || !finite_value(p->ranges_m[i]) || p->ranges_m[i]<=0)return 0;
        for(j=0;j<i;j++)if(p->source_ids[i]==p->source_ids[j])return 0;
    }
    return 1;
}
int acoustic_build_overlay(const m16_av_calibration *c,const acoustic_projection_policy *p,
    const mic16_cycle_output *cycle,uint32_t mount,uint32_t mode,uint64_t end,uint64_t now,
    uint32_t age,m16_av_frame *f,acoustic_projection_report *report){
    unsigned i,j;int rc;double range,az,el,den; m16_av_pixel pixel;
    if(f)memset(f,0,sizeof(*f));if(report)memset(report,0,sizeof(*report));
    if(!f || !report || !cycle || m16_av_validate(c) || !valid_policy(p) ||
       mount!=c->mount_id || mode!=c->video_mode_id || cycle->cycle<0 ||
       cycle->count<0 || cycle->count>M16_AV_POINTS || !age || age>5000)return M16_AV_INVALID;
    if(now<end || now-end>=age)return M16_AV_STALE;
    report->policy_id=p->policy_id;report->mode=p->mode;
    for(i=0;i<(unsigned)cycle->count;i++){
        if(!cycle->tracks[i].source_id || cycle->tracks[i].source_id>65535 ||
           (cycle->tracks[i].confirmed_this_cycle!=0 && cycle->tracks[i].confirmed_this_cycle!=1) ||
           !finite_value(cycle->tracks[i].azimuth) || !finite_value(cycle->tracks[i].elevation) ||
           fabs(cycle->tracks[i].azimuth)>60. || fabs(cycle->tracks[i].elevation)>30.)return M16_AV_INVALID;
        for(j=0;j<i;j++)if(cycle->tracks[i].source_id==cycle->tracks[j].source_id)return M16_AV_INVALID;
    }
    for(i=0;i<(unsigned)cycle->count;i++){
        range=0;rc=0;
        if(p->mode==ACOUSTIC_MEASURED_RANGE){
            for(j=0;j<p->range_count;j++)if(p->source_ids[j]==cycle->tracks[i].source_id){range=p->ranges_m[j];break;}
            if(j==p->range_count)rc=M16_AV_NEEDS_RANGE;
        }else if(p->mode==ACOUSTIC_KNOWN_PLANE){
            az=cycle->tracks[i].azimuth*0.01745329251994329577;
            el=cycle->tracks[i].elevation*0.01745329251994329577;
            den=p->normal[0]*cos(el)*sin(az)+p->normal[1]*sin(el)+p->normal[2]*cos(el)*cos(az);
            if(den<=1e-9)rc=M16_AV_NEEDS_RANGE;else range=p->offset_m/den;
        }
        if(!rc)rc=m16_av_project(c,cycle->tracks[i].azimuth,cycle->tracks[i].elevation,range,&pixel);
        report->point_reason[i]=rc;report->used_range_m[i]=range;
        if(rc){report->omitted++;continue;}
        f->points[i].xy=(uint32_t)floor(pixel.x+.5)|((uint32_t)floor(pixel.y+.5)<<16);
        f->points[i].attr=cycle->tracks[i].source_id|M16_AV_POINT_VALID|
            (cycle->tracks[i].confirmed_this_cycle?M16_AV_POINT_CURRENT:0);
        if(p->mode!=ACOUSTIC_MEASURED_RANGE)report->approximate_points++;
    }
    f->flags=3;f->remaining_ms=age-(uint32_t)(now-end);f->cycle_id=(uint32_t)cycle->cycle;
    f->calibration_id=c->calibration_id;return 0;
}
int acoustic_publish_overlay(const m16_av_calibration *c,const acoustic_projection_policy *p,
    const mic16_cycle_output *cycle,uint32_t mount,uint32_t mode,uint32_t verified,
    uint64_t end,uint64_t now,uint32_t age,void *ctx,m16_av_read32 rd,m16_av_write32 wr,
    int *reason,acoustic_projection_report *report){
    m16_av_frame frame;int rc;
    if(!reason || !report)return -1;
    memset(&frame,0,sizeof(frame));memset(report,0,sizeof(*report));
    rc=verified==1?acoustic_build_overlay(c,p,cycle,mount,mode,end,now,age,&frame,report):M16_AV_INVALID;
    *reason=rc;if(rc)memset(&frame,0,sizeof(frame));
    return m16_av_submit(&frame,ctx,rd,wr);
}
