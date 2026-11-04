#include "../../mic16_cooperate.h"
#include "mic16_extended.h"
#include "mic16_time_map.h"
extern double cos(double),sin(double),hypot(double,double),log10(double);
extern float sqrtf(float);
#define PI 3.14159265358979323846264338327950288
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double minimum_power(double p){return p>1e-20?p:1e-20;}
static int ca(int p){return p<16?p:(p<24?p-16:p-24);}
static int cb(int p){return p<16?(p+1)%16:(p<24?p-8:p-20);}
/* 此时物理88维特征已全部生成，PHAT不再读取；借用同一存储容纳四帧块。 */
int mic16_statistics18_pair_cache(const double *d,unsigned count,float *out,mic16_extended_workspace *w,
    mic16_feature_reuse_report *report){
    unsigned c,t,z,b,p,k,i,start,u,frames,f,j;float arrivals[16],lags[32];
    mic16_statistics_reuse_storage *cache;
    if(!d || !out || !w || count<2 || count>16)return -1;
    cache=&w->physical.statistics_cache;
    for(c=0;c<count;c++){
        if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -2;
        for(b=0;b<257;b++)for(p=0;p<32;p++){
            if(p==0&&(b&15u)==0)M16_COOPERATE();
            double lag=(double)(arrivals[cb((int)p)]-arrivals[ca((int)p)])/39062.5;
            double phase=2.*PI*(b*31.25)*lag;
            cache->signatures[((c*257+b)*32+p)*2]=cos(phase);
            cache->signatures[((c*257+b)*32+p)*2+1]=sin(phase);
        }
    }
    if(report)report->pair_normalizations=0;
    for(start=0;start<67;start+=4){
        frames=67-start<4?67-start:4;
        for(u=0;u<frames;u++)for(b=1;b<257;b++)for(p=0;p<32;p++){
            if(p==0&&(b&15u)==1)M16_COOPERATE();
            unsigned a=(((start+u)*257+b)*16+(unsigned)ca((int)p))*2;
            unsigned q=(((start+u)*257+b)*16+(unsigned)cb((int)p))*2;
            unsigned s=((u*256+b-1)*32+p)*2;
            double ar=w->observed[a],ai=w->observed[a+1],br=w->observed[q],bi=w->observed[q+1];
            double re=ar*br+ai*bi,im=ai*br-ar*bi,mag=hypot(re,im);
            if(mag>1e-8){re/=mag;im/=mag;}else{re=0.;im=0.;}
            cache->pairs[s]=re;cache->pairs[s+1]=im;
        }
        if(report)report->pair_normalizations+=frames*256u*32u;
        for(c=0;c<count;c++)for(u=0;u<frames;u++){
            t=start+u;
            {double band[64][5];for(z=0;z<64;z++)for(k=0;k<5;k++)band[z][k]=0.;
            for(b=1;b<257;b++){
                if((b&15u)==1)M16_COOPERATE();
                double sr=0.,si=0.,lr=0.,li=0.;
                for(p=0;p<32;p++){
                    unsigned s=((u*256+b-1)*32+p)*2,v=((c*257+b)*32+p)*2;
                    double re=cache->pairs[s],im=cache->pairs[s+1];
                    double cr=re*cache->signatures[v]+im*cache->signatures[v+1];
                    double ci=im*cache->signatures[v]-re*cache->signatures[v+1];
                    if(p<16){sr+=cr;si+=ci;}else{lr+=cr;li+=ci;}
                }
                z=(b-1)/4;
                band[z][0]+=sr/16.;band[z][1]+=si/16.;band[z][2]+=lr/16.;band[z][3]+=li/16.;
                band[z][4]+=hypot((sr+lr)/32.,(si+li)/32.);
            }
            for(z=0;z<64;z++){
                float *row=&w->spectral10[(t*64+z)*10];
                double power=w->beam_power[(c*67+t)*64+z],other=power;
                for(k=0;k<5;k++)row[k]=(float)(band[z][k]/4.);
                if(count>1){other=0.;for(i=0;i<count;i++)if(i!=c && w->beam_power[(i*67+t)*64+z]>other)other=w->beam_power[(i*67+t)*64+z];}
                row[5]=(float)(clip(10.*log10(minimum_power(power)),-100.,80.)/40.);
                row[6]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->channel_power[t*64+z])),-40.,20.)/20.);
                row[7]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(other)),-40.,20.)/20.);
                row[8]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->quiet_power[z])),-20.,40.)/20.);
                row[9]=(float)((z*4+2.5)/256.);
            }}
            /* 严格保留原FP32均值、方差和归约顺序，只改变候选间调度。 */
            for(f=0;f<9;f++){
                float mean=0.f,var=0.f;
                for(z=0;z<64;z++)mean+=w->spectral10[(t*64+z)*10+f];
                mean/=64.f;
                for(z=0;z<64;z++){float delta=w->spectral10[(t*64+z)*10+f]-mean;var+=delta*delta;}
                cache->summaries[(c*67+t)*18+f]=mean;
                cache->summaries[(c*67+t)*18+9+f]=sqrtf(var/64.f);
            }
        }
    }
    for(c=0;c<count;c++){
        for(i=0;i<67u*18u;i++)w->summary18[i]=cache->summaries[c*67*18+i];
        for(t=0;t<20;t++)for(f=0;f<18;f++){
            float sum=0.f;unsigned n=0;
            for(j=0;j<67;j++)if(m16_stft_to_frame[j]==t){sum+=w->summary18[j*18+f];n++;}
            out[(c*20+t)*18+f]=sum/(float)n;
        }
    }
    for(i=0;i<257u*32u*2u;i++)w->signature[i]=cache->signatures[(count-1)*257*32*2+i];
    return 0;
}
