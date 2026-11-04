#include "mic16_spatial.h"
#include "mic16_spatial_tables.h"
extern double sin(double);
extern double cos(double);
extern double sqrt(double);
extern double acos(double);
extern double asin(double);
extern double atan2(double,double);
#define PI 3.14159265358979323846264338327950288
static int finite64(double x){return x==x && x<=1.7976931348623157e308 && x>=-1.7976931348623157e308;}
static double clipped(double x){return x< -1.?-1.:(x>1.?1.:x);}
static int bit(const unsigned char *a,int k){return (a[k/8]>>(k%8))&1;}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y ? y-x<na : x-y<nb;
}
const float *mic16_spatial_lags(void){return &m16_grid_lags[0][0];}
int mic16_spatial_signature(unsigned int g,unsigned int p,unsigned int b,float *ri){
    double phase;
    if(g>=325 || p>=32 || b<32 || b>838 || !ri)return -1;
    phase=2.*PI*(double)m16_grid_lags[g][p]*(double)b/4096.;
    ri[0]=(float)cos(phase);ri[1]=(float)sin(phase);
    return 0;
}
double mic16_spatial_angle(double az,double el,double bz,double bl){
    double a=az*(PI/180.),e=el*(PI/180.),b=bz*(PI/180.),f=bl*(PI/180.);
    double c=sin(e)*sin(f)+cos(e)*cos(f)*cos(a-b);
    return acos(clipped(c))*(180./PI);
}
int mic16_spatial_project(const float *x,float *maps,mic16_spatial_workspace *w){
    int i,z,g,p,b,t;
    if(!x || !maps || !w)return -1;
    if(overlap(x,M16_SP_PHAT_FLOATS*4ul,maps,M16_SP_MAP_FLOATS*4ul) ||
       overlap(x,M16_SP_PHAT_FLOATS*4ul,w,sizeof(*w)) ||
       overlap(maps,M16_SP_MAP_FLOATS*4ul,w,sizeof(*w)))return -1;
    for(i=0;i<M16_SP_PHAT_FLOATS;i++)if(!finite64(x[i]) || x[i]<-1.000001f || x[i]>1.000001f)return -1;
    for(z=0;z<24;z++){
        int start=z*34,end=start+34,n;
        if(end>807)end=807;n=end-start;
        for(g=0;g<325;g++){
            for(p=0;p<32;p++)for(b=0;b<n;b++)
                mic16_spatial_signature((unsigned int)g,(unsigned int)p,(unsigned int)(32+start+b),&w->signature[(p*34+b)*2]);
            for(t=0;t<20;t++)w->accum[t]=0.;
            for(p=0;p<32;p++)for(b=0;b<n;b++){
                double sr=w->signature[(p*34+b)*2],si=w->signature[(p*34+b)*2+1];
                for(t=0;t<20;t++){
                    int k=((t*807+start+b)*32+p)*2;
                    w->accum[t]+=sr*(double)x[k]+si*(double)x[k+1];
                }
            }
            for(t=0;t<20;t++)maps[(t*24+z)*325+g]=(float)w->accum[t]/(float)(32*n);
        }
    }
    return 0;
}
int mic16_spatial_votes(const float *maps,const double *levels,const double *floor,
                        double *votes,unsigned short *best_indices){
    int i,t,z,g;
    if(!maps || !levels || !floor || !votes || !best_indices)return -1;
    if(overlap(maps,M16_SP_MAP_FLOATS*4ul,votes,480ul*9*8) ||
       overlap(votes,480ul*9*8,levels,480ul*8) || overlap(votes,480ul*9*8,floor,24ul*8))return -1;
    for(i=0;i<M16_SP_MAP_FLOATS;i++)if(!finite64(maps[i]) || maps[i]<-2.f || maps[i]>2.f)return -1;
    for(i=0;i<480;i++)if(!finite64(levels[i]) || levels[i]<-1000. || levels[i]>1000.)return -1;
    for(i=0;i<24;i++)if(!finite64(floor[i]) || floor[i]<-1000. || floor[i]>1000.)return -1;
    for(t=0;t<20;t++)for(z=0;z<24;z++){
        const float *scores=maps+(t*24+z)*325;
        unsigned int best=0;float second=-3.f;double delta,*v=votes+(t*24+z)*9;
        int start=z*34,end=start+34;
        double center;
        if(end>807)end=807;
        center=(32.+(double)(start+end-1)/2.)*(39062.5/4096.);
        for(g=1;g<325;g++)if(scores[g]>scores[best])best=(unsigned int)g;
        for(g=0;g<325;g++)if(bit(m16_second15[best],g) && scores[g]>second)second=scores[g];
        delta=(double)scores[best]-(double)second;
        v[0]=t;v[1]=z;v[2]=center<1500.?0.:(center<3500.?1.:2.);v[3]=center;
        v[4]=-60.+5.*(best%25);v[5]=-30.+5.*(best/25);
        v[6]=levels[t*24+z]-floor[z];v[7]=(double)scores[best];v[8]=delta>0.?delta:0.;
        best_indices[t*24+z]=(unsigned short)best;
    }
    return 0;
}
/* 一维权重归约使用八路、每块128项的树形顺序，以匹配冻结NumPy 2.4.6的归约算法，来源许可证为BSD-3-Clause。此顺序不代表BLAS空间求和逐位相同。 */
static double weight_sum(const double *a,int n){
    int i,j;double s=-0.;
    if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
    if(n<=128){
        double lanes[8];for(j=0;j<8;j++)lanes[j]=a[j];
        for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)lanes[j]+=a[i+j];
        s=((lanes[0]+lanes[1])+(lanes[2]+lanes[3]))+((lanes[4]+lanes[5])+(lanes[6]+lanes[7]));
        for(;i<n;i++)s+=a[i];return s;
    }
    i=(n/2)/8*8;return weight_sum(a,i)+weight_sum(a+i,n-i);
}
static int better(const mic16_cluster *a,const mic16_cluster *b){
    if(a->frames!=b->frames)return a->frames>b->frames;
    if(a->bands!=b->bands)return a->bands>b->bands;
    if(a->votes!=b->votes)return a->votes>b->votes;
    return a->weight_sum>b->weight_sum;
}
int mic16_spatial_pool_sorted(const mic16_cluster *sorted,unsigned int n,mic16_pool *o){
    unsigned int i,j,k=0;
    if(!sorted || !o || n>325)return -1;
    if(overlap(sorted,n*sizeof(*sorted),o,sizeof(*o)))return -1;
    for(i=0;i<n;i++)if(!finite64(sorted[i].direction[0]) || !finite64(sorted[i].direction[1]) ||
        sorted[i].direction[0]<-90. || sorted[i].direction[0]>90. ||
        sorted[i].direction[1]<-90. || sorted[i].direction[1]>90.)return -1;
    o->count=0;
    for(i=0;i<n;i++){
        for(j=0;j<k;j++)if(mic16_spatial_angle(sorted[i].direction[0],sorted[i].direction[1],
            o->items[j].direction[0],o->items[j].direction[1])<10.)break;
        if(j<k)continue;
        o->items[k++]=sorted[i];if(k==16)break;
    }
    o->count=k;return 0;
}
int mic16_spatial_candidates(const double *v,mic16_pool *out,mic16_spatial_workspace *w){
    unsigned int seeds[325],seen[325]={0},seed_count=0,i,s,j;
    if(!v || !out || !w)return -1;
    if(overlap(v,480ul*9*8,out,sizeof(*out)) || overlap(v,480ul*9*8,w,sizeof(*w)) || overlap(out,sizeof(*out),w,sizeof(*w)))return -1;
    for(i=0;i<480;i++){
        const double *a=v+i*9;int ai,ei,g;
        for(j=0;j<9;j++)if(!finite64(a[j]))return -1;
        if(a[0]!=(double)(i/24) || a[1]!=(double)(i%24) || a[2]<0. || a[2]>2. || a[2]!=(double)(int)a[2])return -1;
        if(a[4]<-60. || a[4]>60. || a[5]<-30. || a[5]>30.)return -1;
        ai=(int)((a[4]+60.)/5.);ei=(int)((a[5]+30.)/5.);
        if(a[4]!=-60.+5.*ai || a[5]!=-30.+5.*ei)return -1;
        if(a[6]<-2000. || a[6]>2000. || a[7]<-2. || a[7]>2. || a[8]<0. || a[8]>4.)return -1;
        g=ei*25+ai;
        if(!seen[g]){seeds[seed_count++]=(unsigned int)g;seen[g]=1;}
    }
    for(s=0;s<seed_count;s++){
        unsigned int seed=seeds[s],frame_bits=0,band_bits=0,n=0;
        mic16_cluster *c=&w->clusters[s];double center[3]={0,0,0},norm,ws;
        for(i=0;i<480;i++){
            const double *a=v+i*9;int g=(int)((a[5]+30.)/5.)*25+(int)((a[4]+60.)/5.);
            if(!bit(m16_near10[seed],g))continue;
            w->weights[n++]=(a[7]>0.?a[7]:0.)*(a[8]>0.?a[8]:0.)*(a[6]>0.?a[6]:0.);
            frame_bits|=1u<<(i/24);band_bits|=1u<<(unsigned int)a[2];
        }
        ws=weight_sum(w->weights,(int)n);j=0;
        for(i=0;i<480;i++){
            const double *a=v+i*9;int g=(int)((a[5]+30.)/5.)*25+(int)((a[4]+60.)/5.);
            double weight;
            if(!bit(m16_near10[seed],g))continue;
            weight=ws<=1e-12?1.:w->weights[j];j++;
            center[0]+=(double)m16_grid_unit[g][0]*weight;
            center[1]+=(double)m16_grid_unit[g][1]*weight;
            center[2]+=(double)m16_grid_unit[g][2]*weight;
        }
        norm=sqrt((center[0]*center[0]+center[1]*center[1])+center[2]*center[2]);if(norm<1e-12)norm=1e-12;
        for(i=0;i<3;i++)center[i]/=norm;
        c->direction[0]=atan2(center[0],center[2])*(180./PI);
        c->direction[1]=asin(clipped(center[1]))*(180./PI);
        c->weight_sum=ws;c->seed_grid=seed;c->votes=n;c->frames=0;c->bands=0;
        for(i=0;i<20;i++)c->frames+=(frame_bits>>i)&1;
        for(i=0;i<3;i++)c->bands+=(band_bits>>i)&1;
    }
    /* 稳定插入排序：排名完全相同时保留最早插入的候选。 */
    for(i=1;i<seed_count;i++){
        mic16_cluster c=w->clusters[i];j=i;
        while(j>0 && better(&c,&w->clusters[j-1])){w->clusters[j]=w->clusters[j-1];j--;}
        w->clusters[j]=c;
    }
    return mic16_spatial_pool_sorted(w->clusters,seed_count,out);
}
