#include "../../mic16_cooperate.h"
#include "mic16_enhancement.h"
#include "mic16_enh_weights.h"
extern float tanhf(float);
static int finite32(float x){return x==x && x>=-65504.f && x<=65504.f;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
float m16_enh_half_roundtrip(float x){
    union {float f;unsigned int u;} v;
    unsigned int sign,e,m,h,shift,round,base;
    v.f=x;sign=v.u&0x80000000u;e=(v.u>>23)&255u;m=v.u&0x7fffffu;
    if(e<102u){v.u=sign;return v.f;}
    if(e<113u){
        shift=126u-e;m|=0x800000u;base=m>>shift;
        round=m&((1u<<shift)-1u);
        if(round>(1u<<(shift-1u)) || (round==(1u<<(shift-1u)) && (base&1u)))base++;
        h=base;
        if(!h){v.u=sign;return v.f;}
        e=113u;while((h&1024u)==0u){h<<=1;e--;}
        v.u=sign|(e<<23)|((h&1023u)<<13);return v.f;
    }
    if(e<143u){
        base=v.u&0x7fffe000u;round=v.u&8191u;
        if(round>4096u || (round==4096u && (base&8192u)))base+=8192u;
        v.u=sign|base;return v.f;
    }
    return x; /* 调用方在进入本辅助函数前拒绝非有限值或溢出。 */
}
const char *m16_enh_checkpoint_sha256(void){return M16_ENH_CHECKPOINT_SHA256;}
int m16_enh_mask(const float *x,float *mask,m16_enh_network_workspace *w){
    int t,f,c,k,o,l,kt,kf,tt,ff,active=0;
    if(!x || !mask || !w)return -1;
    if(overlap(x,67ul*64*10*4,mask,67ul*64*4) ||
       overlap(x,67ul*64*10*4,w,sizeof(*w)) || overlap(mask,67ul*64*4,w,sizeof(*w)))return -1;
    for(k=0;k<67*64*10;k++)if(!finite32(x[k]))return -1;
    for(t=0;t<67;t++)for(f=0;f<64;f++)for(o=0;o<24;o++){
        if(f==0&&o==0)M16_COOPERATE();
        float sum=m16_enh_project_bias[o];
        for(c=0;c<10;c++)sum+=m16_enh_project_weight[o*10+c]*m16_enh_half_roundtrip(x[(t*64+f)*10+c]);
        w->h[0][(t*64+f)*24+o]=sum>0.f?sum:0.f;
    }
    for(l=0;l<3;l++){
        int dilation=1<<l;
        const float *dw=m16_enh_depth_weight+l*24*9,*db=m16_enh_depth_bias+l*24;
        const float *pw=m16_enh_point_weight+l*24*24,*pb=m16_enh_point_bias+l*24;
        for(t=0;t<67;t++)for(f=0;f<64;f++)for(c=0;c<24;c++){
            if(f==0&&c==0)M16_COOPERATE();
            float sum=db[c];
            for(kt=0;kt<3;kt++)for(kf=0;kf<3;kf++){
                tt=t+(kt-2)*dilation;ff=f+kf-1;
                if(tt>=0 && ff>=0 && ff<64)sum+=dw[c*9+kt*3+kf]*w->h[active][(tt*64+ff)*24+c];
            }
            w->depth[(t*64+f)*24+c]=sum>0.f?sum:0.f;
        }
        for(t=0;t<67;t++)for(f=0;f<64;f++)for(o=0;o<24;o++){
        if(f==0&&o==0)M16_COOPERATE();
            float sum=pb[o];
            for(c=0;c<24;c++)sum+=pw[o*24+c]*w->depth[(t*64+f)*24+c];
            sum+=w->h[active][(t*64+f)*24+o];
            w->h[1-active][(t*64+f)*24+o]=sum>0.f?sum:0.f;
        }
        active=1-active;
    }
    for(t=0;t<67;t++)for(f=0;f<64;f++){if(f==0)M16_COOPERATE();
        float sum=m16_enh_output_bias[0],base;
        for(c=0;c<24;c++)sum+=m16_enh_output_weight[c]*w->h[active][(t*64+f)*24+c];
        base=1.f+.5f*tanhf(sum);
        mask[t*64+f]=1.f+.4f*(base-1.f);
    }
    return 0;
}
