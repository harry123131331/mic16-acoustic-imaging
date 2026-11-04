#include "mic16_extended.h"
extern double sin(double);
extern double cos(double);
extern double acos(double);
extern double fma(double,double,double);
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
int mic16_contrast24(const float *x,const double *d,const unsigned char *valid,float *out,unsigned char *peers){
    double u[16][3];int c,j,t,z,i;
    if(!x || !d || !valid || !out || !peers)return -1;
    for(i=0;i<16*20*106;i++)if(!finite64(x[i]))return -1;
    for(c=0;c<16;c++){
        double a=d[c*2]*0.017453292519943295,e=d[c*2+1]*0.017453292519943295;
        if(valid[c]>1 || !finite64(a) || !finite64(e) || d[c*2]<=-90. || d[c*2]>=90. || d[c*2+1]<=-90. || d[c*2+1]>=90.)return -1;
        u[c][0]=cos(e)*sin(a);u[c][1]=sin(e);u[c][2]=cos(e)*cos(a);
    }
    for(c=0;c<16;c++)for(j=0;j<16;j++){
        /* 显式FP64融合乘加顺序匹配冻结本地BLAS三维点积；神经网络仍使用不融合乘加的FP32评分器。 */
        double dot=fma(u[c][2],u[j][2],fma(u[c][1],u[j][1],u[c][0]*u[j][0]));
        double angle=acos(dot>1.?1.:(dot< -1.?-1.:dot))*57.295779513082323;
        peers[c*16+j]=(unsigned char)(valid[c] && valid[j] && angle>15.);
    }
    for(c=0;c<16;c++)for(t=0;t<20;t++)for(z=0;z<24;z++){
        float best=-3.402823466e38f;int exists=0;
        for(j=0;j<16;j++)if(peers[c*16+j]){float r=x[(j*20+t)*106+z];if(!exists || r>best)best=r;exists=1;}
        out[(c*20+t)*24+z]=exists?x[(c*20+t)*106+z]-best:0.f;
    }
    return 0;
}
int mic16_features130(const float *x,const float *q,const double *d,unsigned int count,const double *votes,
                      float *out,double *model_directions,unsigned char *valid,mic16_extended_workspace *w){
    unsigned int c,t,f;
    if(!x || !q || !d || !votes || !out || !model_directions || !valid || !w || count<1 || count>16)return -1;
    {const void *inputs[4]={x,q,d,votes},*outputs[4]={out,model_directions,valid,w};
     unsigned long in_size[4]={40960ul*16*4,8192ul*16*4,count*2ul*8,480ul*9*8};
     unsigned long out_size[4]={16ul*20*130*4,32ul*8,16,sizeof(*w)};unsigned int i,j;
     for(i=0;i<4;i++)for(j=0;j<4;j++)if(overlap(inputs[i],in_size[i],outputs[j],out_size[j]))return -1;
     for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(overlap(outputs[i],out_size[i],outputs[j],out_size[j]))return -1;}
    if(mic16_physical_features(x,q,d,count,votes,w->physical88,&w->physical))return -1;
    if(mic16_statistics18(x,q,d,count,w->statistics18,w))return -2;
    for(c=0;c<16;c++){
        valid[c]=(unsigned char)(c<count);
        model_directions[c*2]=c<count?(double)(float)d[c*2]:0.;model_directions[c*2+1]=c<count?(double)(float)d[c*2+1]:0.;
        for(t=0;t<20;t++)for(f=0;f<106;f++){
            float value=0.f;
            if(c<count)value=f<88?w->physical88[(c*20+t)*88+f]:w->statistics18[(c*20+t)*18+f-88];
            w->features106[(c*20+t)*106+f]=value;
        }
    }
    if(mic16_contrast24(w->features106,model_directions,valid,w->contrasts24,w->peers))return -3;
    for(c=0;c<16;c++)for(t=0;t<20;t++)for(f=0;f<130;f++)out[(c*20+t)*130+f]=
        f<106?w->features106[(c*20+t)*106+f]:w->contrasts24[(c*20+t)*24+f-106];
    return 0;
}
