#include "../../mic16_cooperate.h"
#include "mic16_continuous.h"
#include "mic16_physical.h"
#include "mic16_resample_tables.h"
extern double cos(double);
extern double sin(double);
extern double floor(double);
#define PI 3.14159265358979323846264338327950288
static int finite64(double x){return x==x && x>=-1.7976931348623157e308 && x<=1.7976931348623157e308;}
static int overlaps(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
static const m16_cont_prediction *record(const m16_cont_workspace *w,int64_t index){
    if(index<0 || (uint64_t)index>=w->windows || (uint64_t)index+3<w->windows)return 0;
    return &w->prediction[index%3];
}
static int find(const m16_cont_prediction *p,uint32_t id){
    unsigned i;if(p)for(i=0;i<p->count;i++)if(p->ids[i]==id)return (int)i;
    return -1;
}
static double sample(const m16_cont_workspace *w,int64_t n,unsigned c){
    if(n<0)return 0.;
    /* 调用方保证真实非负索引历史仍保留，禁止对缺口补零。 */
    return (double)w->pcm[((uint64_t)n/40960)%3][((uint64_t)n%40960)*16+c];
}
typedef struct {int delay[16];float coefficient[16][6];} steering;
static int geometry(const double *d,steering *s){
    float arrivals[16],lags[32];int c,k,m;
    if(mic16_candidate_geometry(d,arrivals,lags))return -1;
    for(c=0;c<16;c++){
        double v=10.-arrivals[c],fraction;
        s->delay[c]=(int)floor(v);fraction=v-s->delay[c];
        if(s->delay[c]<0 || s->delay[c]+5>=32)return -1;
        for(k=0;k<6;k++){
            double a=1.;for(m=0;m<6;m++)if(m!=k)a*=(fraction-m)/(double)(k-m);
            s->coefficient[c][k]=(float)a;
        }
    }
    return 0;
}
static double beam(const m16_cont_workspace *w,uint64_t n,const steering *s,const float *weights){
    int c,k;double total=0.;
    for(c=0;c<16;c++){
        double value=0.;for(k=0;k<6;k++)value+=(double)s->coefficient[c][k]*sample(w,(int64_t)n-s->delay[c]-k,(unsigned)c);
        total+=value*(double)weights[c];
    }
    return total;
}
static double fade(unsigned n){return .5-.5*cos(PI*(double)n/624.);}
static int raw_track(m16_cont_workspace *w,uint32_t id,uint64_t origin,uint64_t end){
    uint64_t b,n;unsigned i;
    for(i=0;i<(unsigned)(end-origin);i++)w->raw[i]=0.;
    for(b=origin/40960;b<=(end-1)/40960;b++){
        const m16_cont_prediction *now=record(w,(int64_t)b),*old=record(w,(int64_t)b-1);
        int ci=find(now,id),oi=find(old,id);steering cur,prev;
        uint64_t start=origin>b*40960?origin:b*40960,stop=end<(b+1)*40960?end:(b+1)*40960;
        if(ci>=0 && geometry(now->directions[ci],&cur))return -1;
        if(oi>=0 && geometry(old->directions[oi],&prev))return -1;
        for(n=start;n<stop;n++){if((n&127u)==0)M16_COOPERATE();
            unsigned local=(unsigned)(n-b*40960);double value=0.;
            if(ci>=0){
                value=beam(w,n,&cur,now->weights+(local/2048)*16);
                if(b && local<625){
                    double fraction=fade(local);
                    value=fraction*value+(oi>=0?(1.-fraction)*beam(w,n,&prev,old->weights+19*16):0.);
                }
            }else if(oi>=0 && local<625)value=(1.-fade(local))*beam(w,n,&prev,old->weights+19*16);
            w->raw[n-origin]=value;
        }
    }
    return 0;
}
static unsigned resample(m16_cont_workspace *w,unsigned n){
    unsigned j,count=(n*256u+624u)/625u;
    for(j=0;j<count;j++){if((j&127u)==0)M16_COOPERATE();
        int time=((int)j+10)*625,lo=(time-12500+255)/256,hi=time/256,k;
        double value=0.;if(time-12500<0 || lo<0)lo=0;if(hi>=(int)n)hi=(int)n-1;
        for(k=lo;k<=hi;k++){int h=time-k*256;if(h>=0 && h<=12500)value+=w->raw[k]*m16_resample_h[h];}
        w->resampled[j]=value;
    }
    return count;
}
static void fft(m16_cont_workspace *w,int inverse){
    unsigned i,j=0,bit,len,base,k;
    for(i=1;i<512;i++){
        bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double v=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=v;
            v=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=v;}
    }
    for(len=2;len<=512;len<<=1){
        unsigned half=len>>1,step=512/len;
        for(base=0;base<512;base+=len)for(k=0;k<half;k++){
            unsigned a=base+k,b=a+half,q=k*step;double wi=inverse?-w->twiddle_im[q]:w->twiddle_im[q];
            double re=w->twiddle_re[q]*w->fft_re[b]-wi*w->fft_im[b];
            double im=w->twiddle_re[q]*w->fft_im[b]+wi*w->fft_re[b];
            double ar=w->fft_re[a],ai=w->fft_im[a];
            w->fft_re[a]=ar+re;w->fft_im[a]=ai+im;w->fft_re[b]=ar-re;w->fft_im[b]=ai-im;
        }
    }
    if(inverse)for(i=0;i<512;i++){w->fft_re[i]/=512.;w->fft_im[i]/=512.;}
}
static double interpolated(const float *mask,double position,unsigned band){
    unsigned a,b;double f;
    if(position<0.){position=0.;}
    if(position>66.){position=66.;}
    a=(unsigned)floor(position);b=a<66?a+1:66;f=position-a;
    return (1.-f)*(double)mask[a*64+band]+f*(double)mask[b*64+band];
}
static int enhanced(m16_cont_workspace *w,uint32_t id,uint64_t origin,unsigned count,int final){
    unsigned i,t,b,frames=(count+255)/256+1,total=(frames-1)*256+512;
    if(total>M16_CONT_OLA)return -1;
    for(i=0;i<total;i++){w->signal[i]=0.;w->norm[i]=0.;}
    for(t=0;t<frames;t++){M16_COOPERATE();
        uint64_t center=(origin/625+t)*625,window=center/40960;
        const m16_cont_prediction *now,*old;int ci,oi;double masks[64];unsigned local;
        if(final && window>=w->windows)window=w->windows-1;
        local=(unsigned)(center-window*40960);now=record(w,(int64_t)window);old=record(w,(int64_t)window-1);
        ci=find(now,id);oi=find(old,id);
        for(b=0;b<64;b++){
            masks[b]=1.;
            if(ci>=0){
                masks[b]=interpolated(now->masks[ci],(double)local/625.,b);
                if(oi>=0 && local<625){double f=fade(local);masks[b]=(1.-f)*interpolated(old->masks[oi],(40960.+local)/625.,b)+f*masks[b];}
            }else if(oi>=0 && local<625)masks[b]=interpolated(old->masks[oi],(40960.+local)/625.,b);
        }
        for(i=0;i<512;i++){
            int pos=(int)(t*256+i)-256;
            w->fft_re[i]=(pos>=0 && pos<(int)count?w->resampled[pos]:0.)*m16_periodic_hann512[i];w->fft_im[i]=0.;
        }
        fft(w,0);
        for(b=0;b<=256;b++){double m=masks[b?((b-1)/4):0];w->fft_re[b]*=m;w->fft_im[b]*=m;}
        w->fft_im[0]=w->fft_im[256]=0.;
        for(b=257;b<512;b++){w->fft_re[b]=w->fft_re[512-b];w->fft_im[b]=-w->fft_im[512-b];}
        fft(w,1);
        for(i=0;i<512;i++){unsigned pos=t*256+i;double h=m16_periodic_hann512[i];w->signal[pos]+=w->fft_re[i]*h;w->norm[pos]+=h*h;}
    }
    return 0;
}
static int emit(m16_cont_workspace *w,unsigned index,int final,m16_cont_sink sink,void *context){
    uint64_t start=(uint64_t)index*40960,end=start+40960,origin,stop,offset,begin,finish;
    uint32_t ids[24];unsigned keep=final?2u:3u;
    unsigned count=0,first=w->windows>keep?w->windows-keep:0,i,j,samples,length,selection;
    m16_cont_info info;const m16_cont_prediction *current=record(w,index);
    if(w->emitted!=index)return -1;
    origin=start<2048?0:((start-2048)/625)*625;
    stop=end+2048<(uint64_t)w->windows*40960?end+2048:(uint64_t)w->windows*40960;
    if(origin<(uint64_t)first*40960 || (origin && origin-(uint64_t)first*40960<32) || stop-origin>M16_CONT_RAW)return -1;
    begin=(start*256+624)/625;finish=(end*256+624)/625;offset=(origin/625)*256;
    samples=(unsigned)(finish-begin);selection=(unsigned)(begin-offset);
    for(i=first;i<w->windows;i++){
        const m16_cont_prediction *p=record(w,i);
        for(j=0;j<p->count;j++){
            unsigned k;for(k=0;k<count;k++)if(ids[k]==p->ids[j])break;
            if(k==count){if(count>=24)return -1;ids[count++]=p->ids[j];}
        }
    }
    for(i=1;i<count;i++){uint32_t id=ids[i];j=i;while(j && ids[j-1]>id){ids[j]=ids[j-1];j--;}ids[j]=id;}
    info.epoch=w->epoch;info.window=index;info.stream_count=count;info.final=(unsigned)final;
    info.output_start=begin;info.output_end=finish;info.input_start=start;info.input_end=end;info.available_input=(uint64_t)w->windows*40960;
    for(i=0;i<count;i++){
        if(raw_track(w,ids[i],origin,stop))return -1;
        length=resample(w,(unsigned)(stop-origin));
        if(length>M16_CONT_RESAMPLED || selection+samples>length || enhanced(w,ids[i],origin,length,final))return -1;
        for(j=0;j<samples;j++){
            unsigned pos=256+selection+j;
            if(w->norm[pos]<1e-8)return -1;
            w->wdas[j]=w->resampled[selection+j];w->enhanced[j]=w->signal[pos]/w->norm[pos];
            if(!finite64(w->enhanced[j]))return -1;
        }
        info.source_id=ids[i];info.ordinal=i;info.current_confirmed=find(current,ids[i])>=0;
        if(sink(context,&info,w->wdas,w->enhanced,samples))return -2;
    }
    for(i=0;i<(unsigned)(stop-origin);i++)w->raw[i]=sample(w,(int64_t)(origin+i),0);
    length=resample(w,(unsigned)(stop-origin));if(selection+samples>length)return -1;
    for(i=0;i<samples;i++)w->wdas[i]=w->resampled[selection+i];
    info.source_id=0;info.ordinal=count;info.current_confirmed=0;
    if(sink(context,&info,w->wdas,w->wdas,samples))return -2;
    w->emitted++;return (int)count;
}
int m16_cont_init(m16_cont_workspace *w,uint32_t epoch){
    unsigned i;if(!w || !epoch)return -1;
    w->epoch=epoch;w->windows=w->emitted=w->closed=w->failed=0;
    for(i=0;i<256;i++){w->twiddle_re[i]=cos(2.*PI*i/512.);w->twiddle_im[i]=-sin(2.*PI*i/512.);}
    return 0;
}
static int valid_prediction(const m16_cont_prediction *p){
    unsigned i,j;
    if(p->count>8)return 0;
    for(i=0;i<p->count;i++){
        steering s;if(!p->ids[i])return 0;
        for(j=0;j<i;j++)if(p->ids[i]==p->ids[j])return 0;
        for(j=0;j<2;j++)if(!finite64(p->directions[i][j]) || p->directions[i][j]<=-90. || p->directions[i][j]>=90.)return 0;
        if(geometry(p->directions[i],&s))return 0;
        for(j=0;j<67*64;j++)if(!(p->masks[i][j]>=.8f-1e-6f && p->masks[i][j]<=1.2f+1e-6f))return 0;
    }
    if(p->count)for(i=0;i<20;i++){
        double sum=0.;for(j=0;j<16;j++){float v=p->weights[i*16+j];if(!(v>=0.f && v<=1.f))return 0;sum+=v;}
        if(sum<1.-1e-6 || sum>1.+1e-6)return 0;
    }
    return 1;
}
int m16_cont_push(m16_cont_workspace *w,const float *pcm,const m16_cont_prediction *p,uint64_t first,m16_cont_sink sink,void *context){
    unsigned i;int code;
    if(!w)return -1;
    if(!w->epoch || w->failed || w->closed)return -1;
    if(!pcm || !p || !sink || w->windows==0xffffffffu || first!=(uint64_t)w->windows*40960 ||
       overlaps(w,sizeof(*w),pcm,40960ul*16*4) || overlaps(w,sizeof(*w),p,sizeof(*p)) || !valid_prediction(p))goto bad;
    for(i=0;i<40960*16;i++)if(!(pcm[i]>=-1.f && pcm[i]<=1.f))goto bad;
    for(i=0;i<40960*16;i++)w->pcm[w->windows%3][i]=pcm[i];
    w->prediction[w->windows%3]=*p;w->windows++;
    if(w->windows<2)return 0;
    code=emit(w,w->windows-2,0,sink,context);if(code<0)goto bad;return code;
bad:w->failed=1;return -1;
}
int m16_cont_finish(m16_cont_workspace *w,m16_cont_sink sink,void *context){
    int code=0;if(!w || !w->epoch || w->failed || w->closed || !sink)return -1;
    if(w->windows)code=emit(w,w->windows-1,1,sink,context);
    if(code<0){w->failed=1;return code;}w->closed=1;return code;
}
