#include "mic16_cooperate.h"
#include "mic16_dataflow_fast.h"
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
/* 固定网格的FP32签名按原cos/sin表达式惰性生成；冷启动开销单独计时。
 * 该区仅为运行时DDR缓存，不生成大型源码常量或音频副本。 */
static float signature_cache[24][325][32*34*2] __attribute__((aligned(64)));
static unsigned char signature_valid[24][325];
static float frame_major[807*32][20*2] __attribute__((aligned(64)));
void m16_dataflow_clear_signatures(void){unsigned z,g;for(z=0;z<24;z++)for(g=0;g<325;g++)signature_valid[z][g]=0;}
static void exact_grid(const float *unused,float *maps,unsigned z,unsigned g,mic16_spatial_workspace *w){
    unsigned p,b,t,start=z*34,n=start+34>807?807-start:34;
    const float *signature=signature_cache[z][g];(void)unused;
    if(!signature_valid[z][g]){
        for(p=0;p<32;p++)for(b=0;b<n;b++)mic16_spatial_signature(g,p,32+start+b,&signature_cache[z][g][(p*34+b)*2]);
        signature_valid[z][g]=1;m16_dataflow_calls[0]++;
    }else m16_dataflow_calls[1]++;
    /* 每帧仍按原p、b次序做FP64乘加；一次只持有4个累计值，避免20个FP64累计值溢出寄存器。
     * 4个时间帧在DDR中相邻，原始PHAT仍保留供后续特征使用。 */
    for(t=0;t<20;t+=4){
        double a0=0.,a1=0.,a2=0.,a3=0.;
        for(p=0;p<32;p++)for(b=0;b<n;b++){
            const float *v=&frame_major[(start+b)*32+p][t*2];
            double sr=signature[(p*34+b)*2],si=signature[(p*34+b)*2+1];
            if(b==0&&(p&3u)==0)M16_COOPERATE();
            a0+=sr*(double)v[0]+si*(double)v[1];
            a1+=sr*(double)v[2]+si*(double)v[3];
            a2+=sr*(double)v[4]+si*(double)v[5];
            a3+=sr*(double)v[6]+si*(double)v[7];
        }
        w->accum[t]=a0;w->accum[t+1]=a1;w->accum[t+2]=a2;w->accum[t+3]=a3;
    }
    /* 保留原工作区最终内容，方便完整副作用对照。 */
    for(p=0;p<32;p++)for(b=0;b<n;b++){
        w->signature[(p*34+b)*2]=signature[(p*34+b)*2];
        w->signature[(p*34+b)*2+1]=signature[(p*34+b)*2+1];
    }
    for(t=0;t<20;t++)maps[(t*24+z)*325+g]=(float)w->accum[t]/(float)(32*n);
}
int m16_scanner_refine_dataflow(const float *x,float *maps,mic16_spatial_workspace *w,m16_scanner_refinement *r){
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
    /* 按频点/通道对/时间帧转置一次；分块32个复数，兼顾原读流与新写流的缓存局部性。 */
    for(i=0;i<807u*32u;i+=32){
        unsigned j;M16_COOPERATE();
        for(t=0;t<20;t++)for(j=i;j<i+32;j++){
            frame_major[j][t*2]=x[(t*807u*32u+j)*2];
            frame_major[j][t*2+1]=x[(t*807u*32u+j)*2+1];
        }
    }
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
