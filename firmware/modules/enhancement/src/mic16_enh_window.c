#include "../../mic16_cooperate.h"
#include "mic16_enhancement.h"
#if defined(M16_ENH_PREDICTION_IMPLEMENTATION) && M16_ENH_PREDICTION_IMPLEMENTATION
#include "mic16_enh_prediction.h"
#endif
#include "mic16_resample_tables.h"
extern double sin(double);
extern double cos(double);
extern double sqrt(double);
extern double hypot(double,double);
extern double floor(double);
extern double log10(double);
#define PI 3.14159265358979323846264338327950288
static int finite64(double x){return x==x && x>=-1.7976931348623157e308 && x<=1.7976931348623157e308;}
static double clip(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static double power_floor(double x){return x>1e-20?x:1e-20;}
static int ca(int p){return p<16?p:(p<24?p-16:p-24);}
static int cb(int p){return p<16?(p+1)%16:(p<24?p-8:p-20);}
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
static float sumf(const float *a,int n){
    float r[8],s=-0.f;int i,j;
    if(n<8){for(i=0;i<n;i++)s+=a[i];return s;}
    for(j=0;j<8;j++)r[j]=a[j];
    for(i=8;i<n-n%8;i+=8)for(j=0;j<8;j++)r[j]+=a[i+j];
    s=((r[0]+r[1])+(r[2]+r[3]))+((r[4]+r[5])+(r[6]+r[7]));
    for(;i<n;i++)s+=a[i];
    return s;
}
static void fft(m16_enh_workspace *w,int n,int inverse){
    unsigned int i,j=0,bit,len,base,k;
    for(i=1;i<(unsigned int)n;i++){
        bit=(unsigned int)n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){double s=w->fft_re[i];w->fft_re[i]=w->fft_re[j];w->fft_re[j]=s;
            s=w->fft_im[i];w->fft_im[i]=w->fft_im[j];w->fft_im[j]=s;}
    }
    for(len=2;len<=(unsigned int)n;len<<=1){
        unsigned int half=len>>1,step=4096/len;
        for(base=0;base<(unsigned int)n;base+=len)for(k=0;k<half;k++){
            unsigned int a=base+k,b=a+half,q=k*step;
            double wi=inverse?-w->twiddle_im[q]:w->twiddle_im[q];
            double re=w->twiddle_re[q]*w->fft_re[b]-wi*w->fft_im[b];
            double im=w->twiddle_re[q]*w->fft_im[b]+wi*w->fft_re[b];
            double ar=w->fft_re[a],ai=w->fft_im[a];
            w->fft_re[a]=ar+re;w->fft_im[a]=ai+im;
            w->fft_re[b]=ar-re;w->fft_im[b]=ai-im;
        }
    }
    if(inverse)for(i=0;i<(unsigned int)n;i++){w->fft_re[i]/=(double)n;w->fft_im[i]/=(double)n;}
}
static void channel_weights(const float *pcm,m16_enh_workspace *w){
    int t,c,i,p,k;
    for(i=0;i<2048;i++){w->twiddle_re[i]=cos(2.*PI*i/4096.);w->twiddle_im[i]=-sin(2.*PI*i/4096.);}
    for(t=0;t<20;t++){M16_COOPERATE();
        float reliability[32],acc[16]={0},counts[16]={0};
        for(c=0;c<16;c++){
            float mean=0.f,mean2=0.f;
            for(i=0;i<2048;i++){mean+=pcm[(t*2048+i)*16+c];}
            mean/=2048.f;
            for(i=0;i<2048;i++){float v=pcm[(t*2048+i)*16+c]-mean;w->fft_re[i]=(double)v;mean2+=v;}
            mean2/=2048.f;
            for(i=0;i<2048;i++)w->fft_re[i]=(double)((float)w->fft_re[i]-mean2);
            for(i=2048;i<4096;i++)w->fft_re[i]=0.;
            for(i=0;i<4096;i++)w->fft_im[i]=0.;
            fft(w,4096,0);
            for(i=0;i<2049;i++){w->gcc_spectra[(c*2049+i)*2]=w->fft_re[i];w->gcc_spectra[(c*2049+i)*2+1]=w->fft_im[i];}
        }
        for(p=0;p<32;p++){M16_COOPERATE();
            float curve[41],side[41],variance[41],energy[41];int best=0,n=0;
            float mean,var,psr,sharp,second,rv;
            for(i=0;i<2049;i++){
                int a=(ca(p)*2049+i)*2,b=(cb(p)*2049+i)*2;
                double ar=w->gcc_spectra[a],ai=w->gcc_spectra[a+1],br=w->gcc_spectra[b],bi=w->gcc_spectra[b+1];
                double re=ar*br+ai*bi,im=ai*br-ar*bi,den=hypot(re,im)+1e-8;
                w->fft_re[i]=re/den;w->fft_im[i]=im/den;
            }
            for(i=2049;i<4096;i++){w->fft_re[i]=w->fft_re[4096-i];w->fft_im[i]=-w->fft_im[4096-i];}
            fft(w,4096,1);
            for(k=0;k<41;k++){i=(4096+20-k)%4096;curve[k]=(float)w->fft_re[i];if(curve[k]>curve[best])best=k;}
            for(k=0;k<41;k++){energy[k]=curve[k]*curve[k];if(k<best-1 || k>best+1)side[n++]=curve[k];}
            mean=sumf(side,n)/(float)n;second=side[0];
            for(k=0;k<n;k++){float d=side[k]-mean;variance[k]=d*d;if(side[k]>second)second=side[k];}
            var=sumf(variance,n)/(float)n;
            psr=(float)(((double)curve[best]-(double)mean)/(sqrt((double)var+1e-8)+1e-6));
            sharp=curve[best]-second;rv=(.02f*psr+.3f*sharp)+.7f;
            reliability[p]=sumf(energy,41)>1e-6f?(float)clip(rv,.05,1.):0.f;
        }
        for(p=0;p<32;p++){M16_COOPERATE();acc[ca(p)]+=reliability[p];acc[cb(p)]+=reliability[p];counts[ca(p)]+=1.f;counts[cb(p)]+=1.f;}
        for(c=0;c<16;c++)acc[c]/=counts[c];
        {float den=sumf(acc,16);
            if(den<=1e-8f){for(c=0;c<16;c++)acc[c]=1.f;den=16.f;}
            for(c=0;c<16;c++)w->weights[t*16+c]=acc[c]/den;}
    }
}
static int weighted_beam(const float *pcm,const double *d,m16_enh_workspace *w){
    float arrivals[16],lags[32],coefficient[16][6];int delay[16],c,k,m,i;
    if(mic16_candidate_geometry(d,arrivals,lags))return -1;
    for(c=0;c<16;c++){
        double compensation=10.-(double)arrivals[c],fraction;
        delay[c]=(int)floor(compensation);fraction=compensation-(double)delay[c];
        if(delay[c]<0 || delay[c]>20)return -1;
        for(k=0;k<6;k++){
            double v=1.;for(m=0;m<6;m++)if(m!=k)v*=(fraction-m)/(double)(k-m);
            coefficient[c][k]=(float)v;
        }
    }
    for(i=0;i<40960;i++){if((i&127)==0)M16_COOPERATE();
        double total=0.;
        for(c=0;c<16;c++){
            double y=0.;
            for(k=0;k<6;k++){int pos=i-delay[c]-k;if(pos>=0)y+=(double)coefficient[c][k]*(double)pcm[pos*16+c];}
            total+=y*(double)w->weights[(i/2048)*16+c];
        }
        w->beam[i]=total;
    }
    return 0;
}
static void resample_beam(m16_enh_workspace *w,mic16_extended_workspace *s){
    int j;
    for(j=0;j<16778;j++){if((j&127)==0)M16_COOPERATE();
        int time=(j+11)*625-625,lo=(time-12500+255)/256,hi=time/256,k;double value=0.;
        if(time-12500<0){lo=0;}
        if(lo<0){lo=0;}
        if(hi>=40960){hi=40959;}
        for(k=lo;k<=hi;k++){int h=time-k*256;if(h>=0 && h<=12500)value+=w->beam[k]*m16_resample_h[h];}
        s->resampled[j]=value;
    }
}
static int observed(const float *pcm,const float *quiet,mic16_extended_workspace *s){
    int c,t,b,z,k,i;
    for(c=0;c<16;c++){
        if(mic16_resample_channel(pcm,40960,16,(unsigned int)c,s->resampled)!=16778)return -1;
        for(t=0;t<67;t++){M16_COOPERATE();
            if(mic16_stft512_frame(s->resampled,16778,(unsigned int)t,s))return -1;
            for(b=0;b<257;b++){i=((t*257+b)*16+c)*2;s->observed[i]=s->spectrum[b*2];s->observed[i+1]=s->spectrum[b*2+1];}
        }
    }
    for(i=0;i<15*64;i++)s->quiet_rows[i]=0.;
    for(c=0;c<16;c++){
        if(mic16_resample_channel(quiet,8192,16,(unsigned int)c,s->resampled)!=3356)return -1;
        for(t=0;t<15;t++){
            if(mic16_stft512_frame(s->resampled,3356,(unsigned int)t,s))return -1;
            for(z=0;z<64;z++)for(k=0;k<4;k++){
                b=1+z*4+k;s->quiet_rows[t*64+z]+=s->spectrum[b*2]*s->spectrum[b*2]+s->spectrum[b*2+1]*s->spectrum[b*2+1];
            }
        }
    }
    for(z=0;z<64;z++){
        double a[15];int j;
        for(t=0;t<15;t++)a[t]=s->quiet_rows[t*64+z]/64.;
        for(t=1;t<15;t++){double v=a[t];j=t;while(j>0 && a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;}
        s->quiet_power[z]=a[7];
    }
    for(t=0;t<67;t++)for(z=0;z<64;z++){
        double power=0.;
        for(k=0;k<4;k++)for(c=0;c<16;c++){
            i=((t*257+1+z*4+k)*16+c)*2;power+=s->observed[i]*s->observed[i]+s->observed[i+1]*s->observed[i+1];
        }
        s->channel_power[t*64+z]=power/64.;
    }
    return 0;
}
static int beam_spectra(const float *pcm,const double *d,int c,int save,
                       mic16_extended_workspace *s,m16_enh_workspace *w){
    int t,b,z,k;
    if(weighted_beam(pcm,d,w))return -1;
    resample_beam(w,s);
    for(t=0;t<67;t++){M16_COOPERATE();
        if(mic16_stft512_frame(s->resampled,16778,(unsigned int)t,s))return -1;
        if(save)for(b=0;b<257*2;b++)w->spectrum[t*257*2+b]=s->spectrum[b];
        for(z=0;z<64;z++){
            double power=0.;for(k=0;k<4;k++){b=1+z*4+k;power+=s->spectrum[b*2]*s->spectrum[b*2]+s->spectrum[b*2+1]*s->spectrum[b*2+1];}
            s->beam_power[(c*67+t)*64+z]=power/4.;
        }
    }
    return 0;
}
static int feature10(const double *d,const unsigned char *valid,int c,
                     mic16_extended_workspace *s){
    int b,p,t,z,k,i;float arrivals[16],lags[32];
    if(mic16_candidate_geometry(d+c*2,arrivals,lags))return -1;
    for(b=0;b<257;b++)for(p=0;p<32;p++){M16_COOPERATE();
        double lag=(double)(arrivals[cb(p)]-arrivals[ca(p)])/39062.5;
        double phase=2.*PI*b*31.25*lag;
        s->signature[(b*32+p)*2]=cos(phase);s->signature[(b*32+p)*2+1]=sin(phase);
    }
    for(t=0;t<67;t++){M16_COOPERATE();
        double band[64][5];for(z=0;z<64;z++)for(k=0;k<5;k++)band[z][k]=0.;
        for(b=1;b<257;b++){if((b&15)==1)M16_COOPERATE();
            double sr=0.,si=0.,lr=0.,li=0.;
            for(p=0;p<32;p++){M16_COOPERATE();
                int a=((t*257+b)*16+ca(p))*2,q=((t*257+b)*16+cb(p))*2,v=(b*32+p)*2;
                double ar=s->observed[a],ai=s->observed[a+1],br=s->observed[q],bi=s->observed[q+1];
                double re=ar*br+ai*bi,im=ai*br-ar*bi,mag=hypot(re,im),cr,ci;
                if(mag>1e-8){re/=mag;im/=mag;}else{re=0.;im=0.;}
                cr=re*s->signature[v]+im*s->signature[v+1];ci=im*s->signature[v]-re*s->signature[v+1];
                if(p<16){sr+=cr;si+=ci;}else{lr+=cr;li+=ci;}
            }
            z=(b-1)/4;band[z][0]+=sr/16.;band[z][1]+=si/16.;band[z][2]+=lr/16.;band[z][3]+=li/16.;
            band[z][4]+=hypot((sr+lr)/32.,(si+li)/32.);
        }
        for(z=0;z<64;z++){
            float *f=s->spectral10+(t*64+z)*10;
            double power=s->beam_power[(c*67+t)*64+z],other=0.;int peers=0;
            for(k=0;k<5;k++)f[k]=(float)(band[z][k]/4.);
            for(i=0;i<16;i++)if(valid[i] && i!=c){double v=s->beam_power[(i*67+t)*64+z];if(v>other)other=v;peers++;}
            if(!peers)other=power;
            f[5]=(float)(clip(10.*log10(power_floor(power)),-100.,80.)/40.);
            f[6]=(float)(clip(10.*log10(power_floor(power)/power_floor(s->channel_power[t*64+z])),-40.,20.)/20.);
            f[7]=(float)(clip(10.*log10(power_floor(power)/power_floor(other)),-40.,20.)/20.);
            f[8]=(float)(clip(10.*log10(power_floor(power)/power_floor(s->quiet_power[z])),-20.,40.)/20.);
            f[9]=(float)((z*4+2.5)/256.);
        }
    }
    return 0;
}
#if !defined(M16_ENH_PREDICTION_IMPLEMENTATION) || !M16_ENH_PREDICTION_IMPLEMENTATION
static int synthesize(m16_enh_workspace *w,int masked,double *out){
    int i,t,b;
    for(i=0;i<17408;i++){w->signal[i]=0.;w->norm[i]=0.;}
    for(t=0;t<67;t++){M16_COOPERATE();
        for(b=0;b<257;b++){
            int band=b==0?0:(b-1)/4;
            double mask=masked?(double)w->mask[t*64+band]:1.;
            w->fft_re[b]=w->spectrum[(t*257+b)*2]*mask;w->fft_im[b]=w->spectrum[(t*257+b)*2+1]*mask;
        }
        w->fft_im[0]=0.;w->fft_im[256]=0.;
        for(b=257;b<512;b++){w->fft_re[b]=w->fft_re[512-b];w->fft_im[b]=-w->fft_im[512-b];}
        fft(w,512,1);
        for(i=0;i<512;i++){
            int pos=t*256+i;double hann=m16_periodic_hann512[i];
            w->signal[pos]+=w->fft_re[i]*hann;w->norm[pos]+=hann*hann;
        }
    }
    for(i=0;i<16778;i++){
        if(w->norm[256+i]<1e-8)return -1;
        out[i]=w->signal[256+i]/w->norm[256+i];if(!finite64(out[i]))return -1;
    }
    return 0;
}
#endif
#if defined(M16_ENH_PREDICTION_IMPLEMENTATION) && M16_ENH_PREDICTION_IMPLEMENTATION
int m16_enh_predict_window(const float *pcm,const float *quiet,const double *d,const unsigned char *valid,
    const mic16_cycle_output *tracks,mic16_extended_workspace *s,m16_enh_workspace *w,
    m16_enh_prediction_sink sink,void *context){
#else
int m16_enh_window(const float *pcm,const float *quiet,const double *d,const unsigned char *valid,
    const mic16_cycle_output *tracks,mic16_extended_workspace *s,m16_enh_workspace *w,
    m16_enh_sink sink,void *context){
#endif
    int i,j,c,n=0,selected[8],current[8];
    const void *a[7];unsigned long size[7];
    if(!pcm || !quiet || !d || !valid || !tracks || !s || !w || !sink)return -1;
    a[0]=pcm;a[1]=quiet;a[2]=d;a[3]=valid;a[4]=tracks;a[5]=s;a[6]=w;
    size[0]=40960ul*16*4;size[1]=8192ul*16*4;size[2]=32ul*8;size[3]=16;size[4]=sizeof(*tracks);size[5]=sizeof(*s);size[6]=sizeof(*w);
    for(i=0;i<7;i++)for(j=i+1;j<7;j++)if(overlap(a[i],size[i],a[j],size[j]))return -1;
    if(tracks->count<0 || tracks->count>8 || tracks->confirmed_count<0 || tracks->confirmed_count>tracks->count)return -1;
    for(i=0;i<40960*16;i++)if(!(pcm[i]>=-1.f && pcm[i]<=1.f))return -1;
    for(i=0;i<8192*16;i++)if(!(quiet[i]>=-1.f && quiet[i]<=1.f))return -1;
    for(c=0;c<16;c++){
        if(valid[c]>1 || !finite64(d[c*2]) || !finite64(d[c*2+1]))return -1;
        if(valid[c] && (d[c*2]<=-90. || d[c*2]>=90. || d[c*2+1]<=-90. || d[c*2+1]>=90.))return -1;
    }
    for(i=0;i<tracks->count;i++){
        const mic16_track *tr=tracks->tracks+i;double best=1e300;int choice=-1;
        if(!tr->source_id || (tr->confirmed_this_cycle!=0 && tr->confirmed_this_cycle!=1))return -1;
        for(j=0;j<i;j++)if(tracks->tracks[j].source_id==tr->source_id)return -1;
        if(!tr->confirmed_this_cycle)continue;
        if(!finite64(tr->score) || tr->score<.65 || tr->score>1. || !finite64(tr->azimuth) || !finite64(tr->elevation))return -1;
        for(c=0;c<16;c++)if(valid[c]){
            double dx=d[c*2]-tr->azimuth,dy=d[c*2+1]-tr->elevation,delta=dx*dx+dy*dy;
            if(delta<best){best=delta;choice=c;}
        }
        if(choice<0 || best>1e-8)return -1;
        for(j=0;j<n;j++)if(selected[j]==choice)return -1;
        selected[n]=choice;current[n]=i;n++;
    }
    if(n!=tracks->confirmed_count)return -1;
    if(!n)return 0;
    channel_weights(pcm,w);
    if(observed(pcm,quiet,s))return -2;
    for(c=0;c<16;c++)if(valid[c] && beam_spectra(pcm,d+c*2,c,0,s,w))return -3;
    for(i=0;i<n;i++){
        c=selected[i];
#if defined(M16_ENH_PREDICTION_IMPLEMENTATION) && M16_ENH_PREDICTION_IMPLEMENTATION
        /* 全部合法候选的加权波束功率已准备，预测分支不再次生成同一波束。 */
        if(feature10(d,valid,c,s))return -3;
#else
        if(beam_spectra(pcm,d+c*2,c,1,s,w) || feature10(d,valid,c,s))return -3;
#endif
        if(m16_enh_mask(s->spectral10,w->mask,&w->network))return -4;
#if defined(M16_ENH_PREDICTION_IMPLEMENTATION) && M16_ENH_PREDICTION_IMPLEMENTATION
        if(sink(context,tracks->tracks[current[i]].source_id,(unsigned int)c,w->mask))return -6;
#else
        if(synthesize(w,0,w->wdas) || synthesize(w,1,w->enhanced))return -5;
        if(sink(context,tracks->tracks[current[i]].source_id,(unsigned int)c,s->spectral10,w->mask,w->spectrum,
                w->wdas,w->enhanced,16778))return -6;
#endif
    }
    return n;
}
