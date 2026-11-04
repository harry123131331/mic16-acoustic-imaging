#include "../../../modules/mic16_cooperate.h"
#include "mic16_scanner_hls.h"
#include "mic16_spatial_tables.h"
#include <stddef.h>
/* 此保护带不是检测门限，也不是全输入域精度证明；用于扩大CPU重算范围。
 * 不能确定峰值/竞争峰时退回该频带的原CPU精算，保留后续投票与候选逻辑。 */
#define PEAK_GUARD 0.0002f
static int allowed(unsigned a,unsigned b){return (m16_second15[a][b/8]>>(b%8))&1;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<na:x-y<nb;
}
static void exact_grid(const float *x,float *maps,unsigned z,unsigned g,mic16_spatial_workspace *w){
    unsigned p,b,t,start=z*34,n=start+34>807?807-start:34;
    for(p=0;p<32;p++)for(b=0;b<n;b++)mic16_spatial_signature(g,p,32+start+b,&w->signature[(p*34+b)*2]);
    /* 每个时间帧仍严格按p、b顺序累加；只移动彼此独立的20条累计值。
     * 同步调用期间工作区独占，协作服务不访问这些中间值。 */
    double a0=0.,a1=0.,a2=0.,a3=0.,a4=0.,a5=0.,a6=0.,a7=0.,a8=0.,a9=0.,a10=0.,a11=0.,a12=0.,a13=0.,a14=0.,a15=0.,a16=0.,a17=0.,a18=0.,a19=0.;
    for(p=0;p<32;p++)for(b=0;b<n;b++){
        if(b==0&&(p&3u)==0)M16_COOPERATE();
        double sr=w->signature[(p*34+b)*2],si=w->signature[(p*34+b)*2+1];
        unsigned k=((start+b)*32+p)*2;
        a0+=sr*(double)x[k]+si*(double)x[k+1];
        a1+=sr*(double)x[k+51648u]+si*(double)x[k+51648u+1];
        a2+=sr*(double)x[k+103296u]+si*(double)x[k+103296u+1];
        a3+=sr*(double)x[k+154944u]+si*(double)x[k+154944u+1];
        a4+=sr*(double)x[k+206592u]+si*(double)x[k+206592u+1];
        a5+=sr*(double)x[k+258240u]+si*(double)x[k+258240u+1];
        a6+=sr*(double)x[k+309888u]+si*(double)x[k+309888u+1];
        a7+=sr*(double)x[k+361536u]+si*(double)x[k+361536u+1];
        a8+=sr*(double)x[k+413184u]+si*(double)x[k+413184u+1];
        a9+=sr*(double)x[k+464832u]+si*(double)x[k+464832u+1];
        a10+=sr*(double)x[k+516480u]+si*(double)x[k+516480u+1];
        a11+=sr*(double)x[k+568128u]+si*(double)x[k+568128u+1];
        a12+=sr*(double)x[k+619776u]+si*(double)x[k+619776u+1];
        a13+=sr*(double)x[k+671424u]+si*(double)x[k+671424u+1];
        a14+=sr*(double)x[k+723072u]+si*(double)x[k+723072u+1];
        a15+=sr*(double)x[k+774720u]+si*(double)x[k+774720u+1];
        a16+=sr*(double)x[k+826368u]+si*(double)x[k+826368u+1];
        a17+=sr*(double)x[k+878016u]+si*(double)x[k+878016u+1];
        a18+=sr*(double)x[k+929664u]+si*(double)x[k+929664u+1];
        a19+=sr*(double)x[k+981312u]+si*(double)x[k+981312u+1];
    }
    w->accum[0]=a0;
    w->accum[1]=a1;
    w->accum[2]=a2;
    w->accum[3]=a3;
    w->accum[4]=a4;
    w->accum[5]=a5;
    w->accum[6]=a6;
    w->accum[7]=a7;
    w->accum[8]=a8;
    w->accum[9]=a9;
    w->accum[10]=a10;
    w->accum[11]=a11;
    w->accum[12]=a12;
    w->accum[13]=a13;
    w->accum[14]=a14;
    w->accum[15]=a15;
    w->accum[16]=a16;
    w->accum[17]=a17;
    w->accum[18]=a18;
    w->accum[19]=a19;
    for(t=0;t<20;t++)maps[(t*24+z)*325+g]=(float)w->accum[t]/(float)(32*n);
}
int m16_scanner_refine_accumulators(const float *x,float *maps,mic16_spatial_workspace *w,m16_scanner_refinement *r){
    unsigned i,z,t,g,a,b,full,zero=1;unsigned char selected[325];
    if(!x || !maps || !w || !r || overlap(x,M16_SP_PHAT_FLOATS*4ul,maps,M16_SP_MAP_FLOATS*4ul) ||
       overlap(x,M16_SP_PHAT_FLOATS*4ul,w,sizeof(*w)) || overlap(maps,M16_SP_MAP_FLOATS*4ul,w,sizeof(*w)) ||
       overlap(r,sizeof(*r),x,M16_SP_PHAT_FLOATS*4ul) || overlap(r,sizeof(*r),maps,M16_SP_MAP_FLOATS*4ul) ||
       overlap(r,sizeof(*r),w,sizeof(*w)))return -1;
    for(i=0;i<M16_SP_PHAT_FLOATS;i++){
        if(x[i]!=x[i] || x[i]<-1.000001f || x[i]>1.000001f)return -1;
        if(x[i]!=0.f)zero=0;
    }
    for(i=0;i<M16_SP_MAP_FLOATS;i++)if(maps[i]!=maps[i] || maps[i]<-2.f || maps[i]>2.f)return -1;
    r->refined_grids=r->full_zones=0;r->zero_input=zero;
    if(zero){for(i=0;i<M16_SP_MAP_FLOATS;i++)maps[i]=0.f;return 0;}
    for(z=0;z<24;z++){
        for(g=0;g<325;g++)selected[g]=0;
        for(t=0;t<20;t++){
            float *s=maps+(t*24+z)*325;a=0;b=325;
            for(g=1;g<325;g++)if(s[g]>s[a])a=g;
            for(g=0;g<325;g++)if(allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
            if(b==325)return -1;
            for(g=0;g<325;g++)if(s[g]>=s[a]-2.f*PEAK_GUARD ||
                (allowed(a,g) && s[g]>=s[b]-2.f*PEAK_GUARD))selected[g]=1;
        }
        for(g=0;g<325;g++)if(selected[g]){exact_grid(x,maps,z,g,w);r->refined_grids++;}
        full=0;
        for(t=0;t<20 && !full;t++){
            const float *s=maps+(t*24+z)*325;a=0;b=325;
            for(g=1;g<325;g++)if(s[g]>s[a])a=g;
            for(g=0;g<325;g++)if(allowed(a,g) && (b==325 || s[g]>s[b]))b=g;
            if(!selected[a] || b==325 || !selected[b]){full=1;break;}
            for(g=0;g<325;g++)if(!selected[g] &&
                (s[g]+PEAK_GUARD>=s[a] || (allowed(a,g) && s[g]+PEAK_GUARD>=s[b]))){full=1;break;}
        }
        if(full){
            r->full_zones++;for(g=0;g<325;g++)if(!selected[g]){exact_grid(x,maps,z,g,w);r->refined_grids++;}
        }
    }
    return 0;
}
