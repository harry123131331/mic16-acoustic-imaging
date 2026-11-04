/* 冻结FP32的PS端参考，不宣称PL加速或特征提取。 */
#include "mic16_frozen.h"
#include "mic16_frozen_weights.h"
extern float expf(float);
extern float logf(float);
extern float sqrtf(float);
extern double sin(double);
extern double cos(double);
extern double exp(double);

static float relu(float x) { return x > 0.0f ? x : 0.0f; }
static float absf_local(float x) { return x < 0.0f ? -x : x; }
static int finitef_local(float x) { return x == x && absf_local(x) <= 3.402823466e38f; }
static int finited_local(double x) { return x == x && x <= 1.7976931348623157e308 && x >= -1.7976931348623157e308; }
const char *mic16_frozen_checkpoint_sha256(void) { return MIC16_FROZEN_SHA256; }
double mic16_frozen_probability(float z) {
    double x=(double)z;
    if (x >= 0.0) return 1.0/(1.0+exp(-x));
    x=exp(x); return x/(1.0+x);
}

static void linear(const float *x, const float *w, const float *b,
                   int ni, int no, float *y, int activate) {
    int i,o;
    for (o=0;o<no;o++) {
        float sum=0.0f;
        for (i=0;i<ni;i++) sum += w[o*ni+i]*x[i];
        sum += b[o];
        y[o]=activate ? relu(sum) : sum;
    }
}
static float mean20(const float *x,int stride) {
    int t; float sum=0.0f;
    for(t=0;t<20;t++) sum+=x[t*stride];
    return sum/20.0f;
}
static float std20(const float *x,int stride) {
    int t; float mean=mean20(x,stride),sum=0.0f;
    for(t=0;t<20;t++) { float delta=x[t*stride]-mean; sum+=delta*delta; }
    return sqrtf(sum/20.0f);
}
static void statistics(const float *x,float *out) {
    int t,f;
    float entropy[20],flatness[20],band_std=0.0f,absdiff=0.0f;
    for(f=48;f<80;f++) band_std+=std20(x+f,130);
    for(t=1;t<20;t++) for(f=48;f<80;f++) absdiff+=absf_local(x[t*130+f]-x[(t-1)*130+f]);
    for(t=0;t<20;t++) {
        float lp[32],maximum=-3.402823466e38f,sum=0.0f,mean=0.0f,logsum,e=0.0f;
        for(f=0;f<32;f++) {
            lp[f]=x[t*130+48+f]*9.210340371976184f;
            if(lp[f]>maximum) maximum=lp[f];
            mean+=lp[f];
        }
        for(f=0;f<32;f++) sum+=expf(lp[f]-maximum);
        logsum=maximum+logf(sum);
        for(f=0;f<32;f++) { float logp=lp[f]-logsum; e-=expf(logp)*logp; }
        entropy[t]=e/3.4657359027997265f;
        flatness[t]=expf(mean/32.0f-logsum+3.4657359027997265f);
    }
    out[0]=std20(x+86,130); out[1]=band_std/32.0f; out[2]=absdiff/(19.0f*32.0f);
    out[3]=mean20(entropy,1); out[4]=std20(entropy,1);
    out[5]=mean20(flatness,1); out[6]=std20(flatness,1);
}

int mic16_frozen_score(const float *features,const double *directions,
                       const unsigned char *valid,float *logits,mic16_score_workspace *ws) {
    int c,t,o,i,k,layer,f,j,count;
    const int dilations[3]={1,2,4};
    const int wo[3]={W_TEMPORAL_0_WEIGHT,W_TEMPORAL_1_WEIGHT,W_TEMPORAL_2_WEIGHT};
    const int bo[3]={W_TEMPORAL_0_BIAS,W_TEMPORAL_1_BIAS,W_TEMPORAL_2_BIAS};
    const double radians=0.017453292519943295;
    const double neighbor_limit=cos((15.0+1e-9)*radians);
    if(!features || !directions || !valid || !logits || !ws) return -1;
    for(i=0;i<16*20*130;i++) if(!finitef_local(features[i])) return -1;
    for(i=0;i<32;i++) if(!finited_local(directions[i])) return -1;
    for(c=0;c<16;c++) if(valid[c]>1) return -1;
    for(c=0;c<16;c++) {
        const float *x=features+c*20*130;
        float *own=ws->own+c*71;
        double az=directions[c*2]*radians,el=directions[c*2+1]*radians;
        ws->unit[c*3]=cos(el)*sin(az); ws->unit[c*3+1]=sin(el); ws->unit[c*3+2]=cos(el)*cos(az);
        logits[c]=-20.0f;
        for(f=0;f<71;f++) own[f]=0.0f;
        if(!valid[c]) continue;
        for(o=0;o<32;o++) for(t=0;t<20;t++) {
            float original=0.0f,competition=0.0f;
            for(i=0;i<106;i++) original+=mic16_frozen_weights[W_PROJECT_WEIGHT+o*106+i]*x[t*130+i];
            original+=mic16_frozen_weights[W_PROJECT_BIAS+o];
            for(i=0;i<24;i++) competition+=mic16_frozen_weights[W_COMPETITION_PROJECT_WEIGHT+o*24+i]*x[t*130+106+i];
            ws->h0[o*20+t]=relu(original+competition);
        }
        for(layer=0;layer<3;layer++) {
            int dilation=dilations[layer];
            for(o=0;o<32;o++) for(t=0;t<20;t++) {
                float sum=0.0f;
                for(i=0;i<32;i++) for(k=0;k<3;k++) {
                    int ti=t+(k-2)*dilation;
                    if(ti>=0) sum+=mic16_frozen_weights[wo[layer]+(o*32+i)*3+k]*ws->h0[i*20+ti];
                }
                sum+=mic16_frozen_weights[bo[layer]+o];
                ws->h1[o*20+t]=relu(ws->h0[o*20+t]+sum);
            }
            for(i=0;i<640;i++) ws->h0[i]=ws->h1[i];
        }
        for(o=0;o<32;o++) {
            float maximum=0.0f;
            own[o]=mean20(ws->h0+o*20,1);
            for(t=0;t<20;t++) if(ws->h0[o*20+t]>maximum) maximum=ws->h0[o*20+t];
            own[32+o]=maximum;
        }
        statistics(x,own+64);
    }
    for(c=0;c<16;c++) for(j=0;j<16;j++) {
        double dot=ws->unit[c*3]*ws->unit[j*3]+ws->unit[c*3+1]*ws->unit[j*3+1]+ws->unit[c*3+2]*ws->unit[j*3+2];
        ws->neighbors[c*16+j]=(unsigned char)(c!=j && valid[c] && valid[j] && dot>=neighbor_limit);
    }
    for(c=0;c<16;c++) {
        if(!valid[c]) continue;
        count=0;
        for(f=0;f<71;f++) {
            ws->context[f]=ws->own[c*71+f]; ws->context[71+f]=0.0f; ws->context[142+f]=-3.402823466e38f;
        }
        for(j=0;j<16;j++) {
            if(!ws->neighbors[c*16+j]) continue;
            count++;
            for(f=0;f<71;f++) {
                float value=ws->own[j*71+f];
                ws->context[71+f]+=value;
                if(value>ws->context[142+f]) ws->context[142+f]=value;
            }
        }
        for(f=0;f<71;f++) {
            ws->context[71+f]=count ? ws->own[c*71+f]-ws->context[71+f]/count : 0.0f;
            ws->context[142+f]=count ? ws->own[c*71+f]-ws->context[142+f] : 0.0f;
        }
        ws->context[213]=count/15.0f; ws->context[214]=count ? 1.0f : 0.0f;
        linear(ws->context,mic16_frozen_weights+W_HEAD_0_WEIGHT,mic16_frozen_weights+W_HEAD_0_BIAS,215,64,ws->fc64,1);
        linear(ws->fc64,mic16_frozen_weights+W_HEAD_2_WEIGHT,mic16_frozen_weights+W_HEAD_2_BIAS,64,32,ws->fc32,1);
        linear(ws->fc32,mic16_frozen_weights+W_HEAD_4_WEIGHT,mic16_frozen_weights+W_HEAD_4_BIAS,32,1,logits+c,0);
        if(!finitef_local(logits[c])) return -1;
    }
    return 0;
}
