#include "mic16_cooperate.h"
#include "mic16_extended.h"
#include "mic16_time_map.h"
extern double cos(double),sin(double),hypot(double,double),log10(double);
extern float sqrtf(float);
#define PI 3.14159265358979323846264338327950288
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double minimum_power(double p){return p>1e-20?p:1e-20;}
static int ca(int p){return p<16?p:(p<24?p-16:p-24);}
static int cb(int p){return p<16?(p+1)%16:(p<24?p-8:p-20);}

#include "mic16_statistics_neon.h"
#include <stdint.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
/* 四个候选共用同一通道对观测；签名连续存储，避免按候选重复读观测。
 * 布局为[候选组,频点,通道对,实/虚,4候选]，无音频持久副本。 */
static float signatures4[4][257][32][2][4] __attribute__((aligned(64)));
unsigned m16_statistics_neon_mode,m16_statistics_neon_calls[4];
int m16_statistics_neon_select(unsigned mode){unsigned i;if(mode>1)return -1;m16_statistics_neon_mode=mode;for(i=0;i<4;i++)m16_statistics_neon_calls[i]=0;return 0;}
static inline float clean(float f){union{float f;uint32_t u;}v;v.f=f;if((v.u&0x7f800000u)==0)v.u&=0x80000000u;return v.f;}
static void rotate4(unsigned group,unsigned b,const double *pairs,float out[4][4]){
 unsigned p;
#ifdef __ARM_NEON
 float32x4_t sr=vdupq_n_f32(0.f),si=sr,lr=sr,li=sr;
 for(p=0;p<32;p++){
  float32x4_t re=vdupq_n_f32(clean((float)pairs[p*2])),im=vdupq_n_f32(clean((float)pairs[p*2+1]));
  float32x4_t r=vld1q_f32(signatures4[group][b][p][0]),s=vld1q_f32(signatures4[group][b][p][1]);
  float32x4_t cr=vaddq_f32(vmulq_f32(re,r),vmulq_f32(im,s));
  float32x4_t ci=vsubq_f32(vmulq_f32(im,r),vmulq_f32(re,s));
  if(p<16){sr=vaddq_f32(sr,cr);si=vaddq_f32(si,ci);}else{lr=vaddq_f32(lr,cr);li=vaddq_f32(li,ci);}
 }
 vst1q_f32(out[0],sr);vst1q_f32(out[1],si);vst1q_f32(out[2],lr);vst1q_f32(out[3],li);m16_statistics_neon_calls[3]++;
#else
 unsigned lane,j;for(j=0;j<4;j++)for(lane=0;lane<4;lane++)out[j][lane]=0.f;
 for(p=0;p<32;p++)for(lane=0;lane<4;lane++){
  float re=clean((float)pairs[p*2]),im=clean((float)pairs[p*2+1]),r=signatures4[group][b][p][0][lane],s=signatures4[group][b][p][1][lane];
  float cr=clean(clean(re*r)+clean(im*s)),ci=clean(clean(im*r)-clean(re*s));unsigned j=p<16?0:2;
  out[j][lane]=clean(out[j][lane]+cr);out[j+1][lane]=clean(out[j+1][lane]+ci);
 }
#endif
 m16_statistics_neon_calls[2]++;
}

/* 此时物理88维特征已全部生成，PHAT不再读取；借用同一存储容纳四帧块。 */
int m16_statistics_neon(const double *d,unsigned count,float *out,mic16_extended_workspace *w,
    mic16_feature_reuse_report *report){
    unsigned c,t,z,b,p,k,i,start,u,frames,f,j,base,lane,lanes;float arrivals[16],lags[32];
    mic16_statistics_reuse_storage *cache;
    if(!d || !out || !w || count<2 || count>16)return -1;
    cache=&w->physical.statistics_cache;
    m16_statistics_neon_calls[0]++;
    for(c=0;c<count;c++){
        if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -2;
        for(b=0;b<257;b++)for(p=0;p<32;p++){
            if(p==0&&(b&15u)==0)M16_COOPERATE();
            double lag=(double)(arrivals[cb((int)p)]-arrivals[ca((int)p)])/39062.5;
            double phase=2.*PI*(b*31.25)*lag;
            cache->signatures[((c*257+b)*32+p)*2]=cos(phase);
            cache->signatures[((c*257+b)*32+p)*2+1]=sin(phase);
            signatures4[c/4][b][p][0][c%4]=clean((float)cache->signatures[((c*257+b)*32+p)*2]);
            signatures4[c/4][b][p][1][c%4]=clean((float)cache->signatures[((c*257+b)*32+p)*2+1]);
        }
    }
    for(c=count;c<((count+3)/4)*4;c++)for(b=0;b<257;b++)for(p=0;p<32;p++){
        signatures4[c/4][b][p][0][c%4]=signatures4[c/4][b][p][1][c%4]=0.f;
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
        for(base=0;base<count;base+=4)for(u=0;u<frames;u++){
            double band[4][64][5];lanes=count-base<4?count-base:4;t=start+u;
            for(lane=0;lane<lanes;lane++)for(z=0;z<64;z++)for(k=0;k<5;k++)band[lane][z][k]=0.;
            for(b=1;b<257;b++){
                float sums[4][4];if((b&15u)==1)M16_COOPERATE();
                rotate4(base/4,b,cache->pairs+((u*256+b-1)*32)*2,sums);z=(b-1)/4;
                for(lane=0;lane<lanes;lane++){
                    double sr=sums[0][lane],si=sums[1][lane],lr=sums[2][lane],li=sums[3][lane];
                    band[lane][z][0]+=sr/16.;band[lane][z][1]+=si/16.;band[lane][z][2]+=lr/16.;band[lane][z][3]+=li/16.;
                    band[lane][z][4]+=hypot((sr+lr)/32.,(si+li)/32.);
                }
            }
            for(lane=0;lane<lanes;lane++){c=base+lane;m16_statistics_neon_calls[1]++;
            for(z=0;z<64;z++){
                float *row=&w->spectral10[(t*64+z)*10];
                double power=w->beam_power[(c*67+t)*64+z],other=power;
                for(k=0;k<5;k++)row[k]=(float)(band[lane][z][k]/4.);
                if(count>1){other=0.;for(i=0;i<count;i++)if(i!=c && w->beam_power[(i*67+t)*64+z]>other)other=w->beam_power[(i*67+t)*64+z];}
                row[5]=(float)(clip(10.*log10(minimum_power(power)),-100.,80.)/40.);
                row[6]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->channel_power[t*64+z])),-40.,20.)/20.);
                row[7]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(other)),-40.,20.)/20.);
                row[8]=(float)(clip(10.*log10(minimum_power(power)/minimum_power(w->quiet_power[z])),-20.,40.)/20.);
                row[9]=(float)((z*4+2.5)/256.);
            }
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

#ifdef M16_STATISTICS_NEON_ARM_WRAP
#define ENTRY __wrap_mic16_statistics18_pair_cache
#define ORIGINAL __real_mic16_statistics18_pair_cache
#else
#define ENTRY mic16_statistics18_pair_cache
#define ORIGINAL m16_original_statistics18_pair_cache
#endif
extern int ORIGINAL(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
int ENTRY(const double *d,unsigned n,float *out,mic16_extended_workspace *w,mic16_feature_reuse_report *r){
 return m16_statistics_neon_mode?m16_statistics_neon(d,n,out,w,r):ORIGINAL(d,n,out,w,r);
}
