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

#include "mic16_statistics_tile_batch.h"
#include <stdint.h>
#include <string.h>
/* 工作区均在DDR；160KiB频带累加区支持观测块跨四组候选复用，不占用ARM栈。
 * 系数按硬件连续布局保存；FP64签名仍保留，维持原调用后的工作区语义。 */
static uint64_t signature_words[4][256][32][4] __attribute__((aligned(64)));
static double tile_bands[4][16][64][5] __attribute__((aligned(64)));
static m16_tile_batch_backend backend;
static unsigned busy;
m16_tile_batch_report m16_tile_batch_counts;
int m16_tile_batch_bind(const m16_tile_batch_backend *value){
 if(busy || !value || !value->arena || ((uintptr_t)value->arena&63u) ||
    !value->available || !value->execute || !value->available(value->context))return -1;
 backend=*value;return 0;
}
static float clean(float f){union{float f;uint32_t u;}v;v.f=f;if((v.u&0x7f800000u)==0)v.u&=0x80000000u;return v.f;}
static uint64_t pack_pair(double re,double im){
 union{float f;uint32_t u;}a,b;a.f=clean((float)re);b.f=clean((float)im);
 return (uint64_t)a.u|((uint64_t)b.u<<32);
}
static double unpack(uint64_t word,unsigned imaginary){
 union{float f;uint32_t u;}v;v.u=(uint32_t)(imaginary?word>>32:word);return (double)v.f;
}

/* 此时物理88维特征已全部生成，PHAT不再读取；借用同一存储容纳四帧块。 */
static int compute(const double *d,unsigned count,float *out,mic16_extended_workspace *w,
    mic16_feature_reuse_report *report){
    unsigned c,t,z,b,p,k,i,start,u,frames,f,j,base,lane,lanes,first,bb;float arrivals[16],lags[32];
    mic16_statistics_reuse_storage *cache;
    if(!d || !out || !w || count<2 || count>16)return -1;
    cache=&w->physical.statistics_cache;
    m16_tile_batch_counts.calls++;
    for(c=0;c<count;c++){
        if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -2;
        for(b=0;b<257;b++)for(p=0;p<32;p++){
            if(p==0&&(b&15u)==0)M16_COOPERATE();
            double lag=(double)(arrivals[cb((int)p)]-arrivals[ca((int)p)])/39062.5;
            double phase=2.*PI*(b*31.25)*lag;
            cache->signatures[((c*257+b)*32+p)*2]=cos(phase);
            cache->signatures[((c*257+b)*32+p)*2+1]=sin(phase);
            if(b)signature_words[c/4][b-1][p][c%4]=pack_pair(
                cache->signatures[((c*257+b)*32+p)*2],cache->signatures[((c*257+b)*32+p)*2+1]);
        }
    }
    for(c=count;c<((count+3)/4)*4;c++)for(b=0;b<256;b++)for(p=0;p<32;p++)
        signature_words[c/4][b][p][c%4]=0;

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
        memset(tile_bands,0,sizeof(tile_bands));
        for(first=1;first<257;first+=8){
            if(!backend.available(backend.context))return -3;
            /* 每个观测块只打包一次，再顺序供全部候选组读取。 */
            for(u=0;u<frames;u++)for(bb=0;bb<8;bb++)for(p=0;p<32;p++){
                const double *pair=cache->pairs+((u*256+first+bb-1)*32+p)*2;
                backend.arena[(u*8+bb)*32+p]=pack_pair(pair[0],pair[1]);
            }
            for(base=0;base<count;base+=4){
                int rc;lanes=count-base<4?count-base:4;
                if(!backend.available(backend.context))return -3;
                memcpy(backend.arena+1024,&signature_words[base/4][first-1][0][0],8192);
                rc=backend.execute(backend.context,frames,8,lanes,base==0);
                if(rc){m16_tile_batch_counts.backend_errors++;return rc<0?rc:-4;}
                m16_tile_batch_counts.batches++;
                m16_tile_batch_counts.rotation_groups+=frames*8;
                for(u=0;u<frames;u++)for(bb=0;bb<8;bb++)for(lane=0;lane<lanes;lane++){
                    uint64_t a=backend.arena[2048+((u*8+bb)*4+lane)*2];
                    uint64_t q=backend.arena[2048+((u*8+bb)*4+lane)*2+1];
                    double sr=unpack(a,0),si=unpack(a,1),lr=unpack(q,0),li=unpack(q,1);
                    double *band=tile_bands[u][base+lane][(first+bb-1)/4];
                    /* 同一结果仍按原频点次序累加；只交错互不依赖的结果。 */
                    if(!(sr>=-33. && sr<=33. && si>=-33. && si<=33. &&
                         lr>=-33. && lr<=33. && li>=-33. && li<=33.))return -5;
                    band[0]+=sr/16.;band[1]+=si/16.;band[2]+=lr/16.;band[3]+=li/16.;
                    band[4]+=hypot((sr+lr)/32.,(si+li)/32.);
                }
                M16_COOPERATE();
            }
        }
        for(base=0;base<count;base+=4)for(u=0;u<frames;u++){
            lanes=count-base<4?count-base:4;t=start+u;
            for(lane=0;lane<lanes;lane++){c=base+lane;m16_tile_batch_counts.rows++;
            for(z=0;z<64;z++){
                float *row=&w->spectral10[(t*64+z)*10];
                double power=w->beam_power[(c*67+t)*64+z],other=power;
                for(k=0;k<5;k++)row[k]=(float)(tile_bands[u][c][z][k]/4.);
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

int m16_tile_batch_compute(const double *d,unsigned n,float *out,mic16_extended_workspace *w,
    mic16_feature_reuse_report *report){
 int rc;
 if(busy || !backend.arena || !backend.available(backend.context))return -3;
 busy=1;m16_tile_batch_counts=(m16_tile_batch_report){0};
 rc=compute(d,n,out,w,report);busy=0;return rc;
}
