#include "mic16_av.h"
#ifdef M16_AV_LLVM_HOST_NO_SDK
#include <stddef.h>
extern double sin(double),cos(double),floor(double),fabs(double);
extern void *memset(void *,int,size_t);
#else
#include <math.h>
#include <string.h>
#endif
static int finite_d(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
int m16_av_validate(const m16_av_calibration *c){
    unsigned i,j,k; double v,det;
    if(!c || c->calibrated!=1 || !c->calibration_id || !c->mount_id || !c->video_mode_id ||
       !c->width || !c->height || c->width>4096 || c->height>4096 || c->allow_direction_only>1 ||
       !finite_d(c->fx) || !finite_d(c->fy) || c->fx<=0 || c->fy<=0 ||
       !finite_d(c->cx) || !finite_d(c->cy))return M16_AV_INVALID;
    for(i=0;i<5;i++)if(!finite_d(c->distortion[i]))return M16_AV_INVALID;
    for(i=0;i<9;i++)if(!finite_d(c->rotation[i]))return M16_AV_INVALID;
    for(i=0;i<3;i++)if(!finite_d(c->translation_m[i]))return M16_AV_INVALID;
    for(i=0;i<3;i++)for(j=0;j<3;j++){
        v=0;for(k=0;k<3;k++)v+=c->rotation[k*3+i]*c->rotation[k*3+j];
        if(fabs(v-(i==j?1.:0.))>1e-6)return M16_AV_INVALID;
    }
    det=c->rotation[0]*(c->rotation[4]*c->rotation[8]-c->rotation[5]*c->rotation[7])
       -c->rotation[1]*(c->rotation[3]*c->rotation[8]-c->rotation[5]*c->rotation[6])
       +c->rotation[2]*(c->rotation[3]*c->rotation[7]-c->rotation[4]*c->rotation[6]);
    return fabs(det-1.)<=1e-6?0:M16_AV_INVALID;
}
int m16_av_project(const m16_av_calibration *c,double az,double el,double range,m16_av_pixel *o){
    double a,e,u[3],p[3],x,y,r2,rad,xd,yd;unsigned i,j;
    if(o)memset(o,0,sizeof(*o));
    if(!o || m16_av_validate(c) || !finite_d(az)||!finite_d(el)||!finite_d(range)||range<0 ||
       az < -60. || az > 60. || el < -30. || el > 30.)return M16_AV_INVALID;
    if(range==0 && !c->allow_direction_only)return M16_AV_NEEDS_RANGE;
    a=az*0.01745329251994329577;e=el*0.01745329251994329577;
    u[0]=cos(e)*sin(a);u[1]=sin(e);u[2]=cos(e)*cos(a);
    for(i=0;i<3;i++){
        p[i]=0;for(j=0;j<3;j++)p[i]+=c->rotation[3*i+j]*u[j]*(range>0?range:1.);
        if(range>0)p[i]+=c->translation_m[i];
    }
    if(!finite_d(p[2]) || p[2]<=1e-9)return M16_AV_BEHIND;
    x=p[0]/p[2];y=p[1]/p[2];r2=x*x+y*y;
    rad=1+c->distortion[0]*r2+c->distortion[1]*r2*r2+c->distortion[4]*r2*r2*r2;
    xd=x*rad+2*c->distortion[2]*x*y+c->distortion[3]*(r2+2*x*x);
    yd=y*rad+c->distortion[2]*(r2+2*y*y)+2*c->distortion[3]*x*y;
    x=c->fx*xd+c->cx;y=c->fy*yd+c->cy;
    if(!finite_d(x)||!finite_d(y)||x<0||y<0||x>(double)c->width-1||y>(double)c->height-1)return M16_AV_OUTSIDE;
    o->x=x;o->y=y;o->approximate=(range==0);return 0;
}
int m16_av_build(const m16_av_calibration *c,const mic16_cycle_output *cycle,
                uint32_t mount,uint32_t mode,uint64_t end,uint64_t now,uint32_t age,
                m16_av_frame *f,uint32_t *omitted){
    unsigned i,j; m16_av_pixel p; int rc;
    if(f)memset(f,0,sizeof(*f));
    if(omitted)*omitted=0;
    if(!f || !omitted || !cycle || m16_av_validate(c) || mount!=c->mount_id || mode!=c->video_mode_id ||
       cycle->count<0 || cycle->count>M16_AV_POINTS || cycle->cycle<0 || !age || age>5000)return M16_AV_INVALID;
    if(now<end || now-end>=age)return M16_AV_STALE;
    for(i=0;i<(unsigned)cycle->count;i++){
        if(!cycle->tracks[i].source_id || cycle->tracks[i].source_id>65535 ||
           (cycle->tracks[i].confirmed_this_cycle!=0 && cycle->tracks[i].confirmed_this_cycle!=1))return M16_AV_INVALID;
        for(j=0;j<i;j++)if(cycle->tracks[i].source_id==cycle->tracks[j].source_id)return M16_AV_INVALID;
    }
    f->flags=3;f->remaining_ms=age-(uint32_t)(now-end);f->cycle_id=(uint32_t)cycle->cycle;f->calibration_id=c->calibration_id;
    for(i=0;i<(unsigned)cycle->count;i++){
        rc=m16_av_project(c,cycle->tracks[i].azimuth,cycle->tracks[i].elevation,0,&p);
        if(rc){(*omitted)++;continue;}
        j=(unsigned)floor(p.x+.5);if(j>=c->width){(*omitted)++;continue;}
        f->points[i].xy=j|((uint32_t)floor(p.y+.5)<<16);
        f->points[i].attr=cycle->tracks[i].source_id|M16_AV_POINT_VALID|
            (cycle->tracks[i].confirmed_this_cycle?M16_AV_POINT_CURRENT:0);
    }
    return 0;
}
int m16_av_submit(const m16_av_frame *f,void *ctx,m16_av_read32 rd,m16_av_write32 wr){
    unsigned i;uint32_t status;
    if(!f || !rd || !wr || (f->flags&~3u) ||
       ((f->flags&3u)==3u && (!f->remaining_ms || f->remaining_ms>5000 || !f->calibration_id)))return -1;
    if(rd(ctx,0)!=M16_AV_MAGIC || rd(ctx,4)!=M16_AV_VERSION)return -1;
    status=rd(ctx,8);if(status&1u)return 1;if(!(status&2u))return -1;
    wr(ctx,0x10,f->flags);wr(ctx,0x14,f->remaining_ms);
    wr(ctx,0x18,f->cycle_id);wr(ctx,0x1c,f->calibration_id);
    for(i=0;i<M16_AV_POINTS;i++){
        wr(ctx,0x40+8*i,f->points[i].xy);wr(ctx,0x44+8*i,f->points[i].attr);
    }
    wr(ctx,0x0c,1);return 0;
}
