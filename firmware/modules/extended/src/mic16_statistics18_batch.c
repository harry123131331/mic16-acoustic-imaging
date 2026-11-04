#include "../../mic16_cooperate.h"
#include "mic16_stft_batch.h"
/* 三个固定消费者只写其它工作区字段，不改resampled；回调和批次均同步完成。 */
typedef struct {mic16_extended_workspace *w;unsigned c;} stft_context;
static int save_observed(unsigned t,const double *s,void *context){
    stft_context *v=(stft_context *)context;unsigned b,c=v->c;mic16_extended_workspace *w=v->w;
    for(b=0;b<257;b++){
        unsigned int q=((t*257+b)*16+c)*2;
        w->observed[q]=s[b*2];w->observed[q+1]=s[b*2+1];
    }
    return 0;
}
static int save_quiet(unsigned t,const double *s,void *context){
    stft_context *v=(stft_context *)context;unsigned z,k,b;mic16_extended_workspace *w=v->w;
    for(z=0;z<64;z++)for(k=0;k<4;k++){
        b=1+z*4+k;w->quiet_rows[t*64+z]+=s[b*2]*s[b*2]+s[b*2+1]*s[b*2+1];
    }
    return 0;
}
static int save_beam_power(unsigned t,const double *s,void *context){
    stft_context *v=(stft_context *)context;unsigned z,k,b,c=v->c;mic16_extended_workspace *w=v->w;
    for(z=0;z<64;z++){
        double power=0.;for(k=0;k<4;k++){b=1+z*4+k;power+=s[b*2]*s[b*2]+s[b*2+1]*s[b*2+1];}
        w->beam_power[(c*67+t)*64+z]=power/4.;
    }
    return 0;
}

/* 来源：原统计实现；旧入口保留用于对照。 */
#include "mic16_extended.h"
#include "mic16_time_map.h"
extern double sin(double);
extern double cos(double);
extern double hypot(double,double);
extern double log10(double);
extern float sqrtf(float);
#define PI 3.14159265358979323846264338327950288
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double minimum_power(double p){return p>1e-20?p:1e-20;}
static int ca(int p){return p<16?p:(p<24?p-16:p-24);}
static int cb(int p){return p<16?(p+1)%16:(p<24?p-8:p-20);}
static void compress(float *out,mic16_extended_workspace *w){
    int t,f,z,j;
    for(t=0;t<67;t++)for(f=0;f<9;f++){
        float mean=0.f,var=0.f;
        /* 频带是冻结FP32数据[67,64,10]中的非连续轴。 */
        for(z=0;z<64;z++)mean+=w->spectral10[(t*64+z)*10+f];mean/=64.f;
        for(z=0;z<64;z++){float d=w->spectral10[(t*64+z)*10+f]-mean;var+=d*d;}
        w->summary18[t*18+f]=mean;w->summary18[t*18+9+f]=sqrtf(var/64.f);
    }
    for(t=0;t<20;t++)for(f=0;f<18;f++){
        float sum=0.f;unsigned int n=0;
        for(j=0;j<67;j++)if(m16_stft_to_frame[j]==t){sum+=w->summary18[j*18+f];n++;}
        out[t*18+f]=sum/(float)n;
    }
}
int mic16_statistics18_batched(const float *pcm,const float *quiet,const double *d,unsigned int count,float *out,mic16_extended_workspace *w,
    unsigned int reuse_pairs,unsigned int beams_ready,mic16_feature_reuse_report *report){
    unsigned int i,c,t,z,b,p,k,guard;float arrivals[16],lags[32];
    if(!pcm || !quiet || !d || !out || !w || count<1 || count>16 || reuse_pairs>1 || beams_ready>1)return -1;
    for(i=0;i<40960*16;i++)if(!(pcm[i]>=-1.f && pcm[i]<=1.f))return -1;
    for(i=0;i<8192*16;i++)if(!(quiet[i]>=-1.f && quiet[i]<=1.f))return -1;
    for(c=0;c<count;c++)if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -1;
    for(c=0;c<16;c++){
        if(mic16_resample_channel(pcm,40960,16,c,w->resampled)!=16778)return -2;
        {stft_context context={w,c};if(mic16_stft512_visit(w->resampled,16778,67,w,save_observed,&context))return -2;}
    }
    for(i=0;i<15*64;i++)w->quiet_rows[i]=0.;
    for(c=0;c<16;c++){
        if(mic16_resample_channel(quiet,8192,16,c,w->resampled)!=3356)return -2;
        {stft_context context={w,c};if(mic16_stft512_visit(w->resampled,3356,15,w,save_quiet,&context))return -2;}
    }
    for(z=0;z<64;z++){
        double a[15];int j;
        for(t=0;t<15;t++)a[t]=w->quiet_rows[t*64+z]/64.;
        for(t=1;t<15;t++){double v=a[t];j=(int)t;while(j>0 && a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;}
        w->quiet_power[z]=a[7];
    }
    for(t=0;t<67;t++)for(z=0;z<64;z++){
        double power=0.;
        for(k=0;k<4;k++)for(c=0;c<16;c++){
            unsigned int q=((t*257+1+z*4+k)*16+c)*2;
            power+=w->observed[q]*w->observed[q]+w->observed[q+1]*w->observed[q+1];
        }
        w->channel_power[t*64+z]=power/64.;
    }
    if(!beams_ready){
    for(c=0;c<count;c++){
        if(mic16_candidate_beam(pcm,40960,d+c*2,w->physical.beam,&guard) ||
            mic16_resample_channel(w->physical.beam,40960,1,0,w->resampled)!=16778)return -2;
        {stft_context context={w,c};if(mic16_stft512_visit(w->resampled,16778,67,w,save_beam_power,&context))return -2;}
    }
    }
    if(reuse_pairs && count>1)return mic16_statistics18_pair_cache(d,count,out,w,report);
    if(report)report->pair_normalizations=67u*256u*32u*count;
    for(c=0;c<count;c++){
        if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -2;
        for(b=0;b<257;b++)for(p=0;p<32;p++){
            double lag=(double)(arrivals[cb((int)p)]-arrivals[ca((int)p)])/39062.5;
            double phase=2.*PI*(b*31.25)*lag;
            w->signature[(b*32+p)*2]=cos(phase);w->signature[(b*32+p)*2+1]=sin(phase);
        }
        for(t=0;t<67;t++){
            double band[64][5];for(z=0;z<64;z++)for(k=0;k<5;k++)band[z][k]=0.;
            for(b=1;b<257;b++){
                if((b&15u)==1)M16_COOPERATE();
                double sr=0.,si=0.,lr=0.,li=0.;
                for(p=0;p<32;p++){
                    unsigned int a=((t*257+b)*16+(unsigned int)ca((int)p))*2;
                    unsigned int q=((t*257+b)*16+(unsigned int)cb((int)p))*2,s=(b*32+p)*2;
                    double ar=w->observed[a],ai=w->observed[a+1],br=w->observed[q],bi=w->observed[q+1];
                    double re=ar*br+ai*bi,im=ai*br-ar*bi,mag=hypot(re,im),cr,ci;
                    if(mag>1e-8){re/=mag;im/=mag;}else{re=0.;im=0.;}
                    cr=re*w->signature[s]+im*w->signature[s+1];ci=im*w->signature[s]-re*w->signature[s+1];
                    if(p<16){sr+=cr;si+=ci;}else{lr+=cr;li+=ci;}
                }
                z=(b-1)/4;
                band[z][0]+=sr/16.;band[z][1]+=si/16.;band[z][2]+=lr/16.;band[z][3]+=li/16.;
                band[z][4]+=hypot((sr+lr)/32.,(si+li)/32.);
            }
            for(z=0;z<64;z++){
                float *f=&w->spectral10[(t*64+z)*10];double power=w->beam_power[(c*67+t)*64+z],other=power;
                for(k=0;k<5;k++)f[k]=(float)(band[z][k]/4.);
                if(count>1){other=0.;for(i=0;i<count;i++)if(i!=c && w->beam_power[(i*67+t)*64+z]>other)other=w->beam_power[(i*67+t)*64+z];}
                f[5]=(float)(clip(10.*log10(minimum_power(power)),-100.,80.)/40.);
                f[6]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->channel_power[t*64+z])),-40.,20.)/20.);
                f[7]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(other)),-40.,20.)/20.);
                f[8]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->quiet_power[z])),-20.,40.)/20.);
                f[9]=(float)((z*4+2.5)/256.);
            }
        }
        compress(out+c*20*18,w);
    }
    return 0;
}

int mic16_statistics_beam_power_batched(const float *beam,unsigned int c,mic16_extended_workspace *w){
    unsigned t,z,k,b;
    if(!beam || !w || c>=16 || mic16_resample_channel(beam,40960,1,0,w->resampled)!=16778)return -1;
    {stft_context context={w,c};if(mic16_stft512_visit(w->resampled,16778,67,w,save_beam_power,&context))return -1;}
    return 0;
}
