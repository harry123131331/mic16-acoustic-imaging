/* 索引优化显式分支，来源及唯一改动见beam_index安装记录。 */
#include "mic16_beam_index.h"
#include "mic16_extended.h"
static int overlap(const void *a,unsigned long na,const void *b,unsigned long nb){
    __UINTPTR_TYPE__ x=(__UINTPTR_TYPE__)a,y=(__UINTPTR_TYPE__)b;
    return x<=y?y-x<na:x-y<nb;
}
typedef struct {mic16_extended_workspace *work;unsigned mask;} beam_context;
static int save_beam(void *context,unsigned c,const float *beam,unsigned samples){
    beam_context *b=(beam_context *)context;
    if(c>=16 || samples!=40960 || (b->mask&(1u<<c)) || mic16_statistics_beam_power(beam,c,b->work))return -1;
    b->mask|=1u<<c;return 0;
}
int mic16_features130_indexed(const float *x,const float *q,const double *d,unsigned int count,const double *votes,
                      float *out,double *model_directions,unsigned char *valid,mic16_extended_workspace *w,
    const mic16_prepared_spectral *prepared,unsigned int reuse_flags,mic16_feature_reuse_report *report){
    unsigned int c,t,f;beam_context beam={w,0};
    if(!x || !q || !d || !votes || !out || !model_directions || !valid || !w || count<1 || count>16 || reuse_flags>7u)return -1;
    {const void *inputs[4]={x,q,d,votes},*outputs[4]={out,model_directions,valid,w};
     unsigned long in_size[4]={40960ul*16*4,8192ul*16*4,count*2ul*8,480ul*9*8};
     unsigned long out_size[4]={16ul*20*130*4,32ul*8,16,sizeof(*w)};unsigned int i,j;
     for(i=0;i<4;i++)for(j=0;j<4;j++)if(overlap(inputs[i],in_size[i],outputs[j],out_size[j]))return -1;
     for(i=0;i<4;i++)for(j=i+1;j<4;j++)if(overlap(outputs[i],out_size[i],outputs[j],out_size[j]))return -1;}
    if(report){
        const void *a[8]={x,q,d,votes,out,model_directions,valid,w};
        unsigned long sizes[8]={40960ul*16*4,8192ul*16*4,count*2ul*8,480ul*9*8,16ul*20*130*4,32ul*8,16,sizeof(*w)};
        unsigned i;for(i=0;i<8;i++)if(overlap(report,sizeof(*report),a[i],sizes[i]))return -1;
    }
    if((reuse_flags&M16_REUSE_COMMON) && (!prepared || prepared->pcm!=x || prepared->reference!=q || prepared->owner!=&w->physical))return -1;
    if(report)report->common_frames_reused=report->pair_normalizations=report->beam_transforms_reused=0;
    if(reuse_flags&(M16_REUSE_COMMON|M16_REUSE_BEAMS)){
        if(mic16_physical_features_indexed(x,q,d,count,votes,w->physical88,&w->physical,
            reuse_flags&M16_REUSE_COMMON?prepared:0,reuse_flags&M16_REUSE_BEAMS?save_beam:0,&beam))return -1;
    }else if(mic16_physical_features(x,q,d,count,votes,w->physical88,&w->physical))return -1;
    if((reuse_flags&M16_REUSE_BEAMS) && beam.mask!=((1u<<count)-1u))return -1;
    if(report){report->common_frames_reused=reuse_flags&M16_REUSE_COMMON?24u:0u;
        report->beam_transforms_reused=reuse_flags&M16_REUSE_BEAMS?count:0u;}
    if(mic16_statistics18_reused(x,q,d,count,w->statistics18,w,
        (reuse_flags&M16_REUSE_PAIRS)?1u:0u,(reuse_flags&M16_REUSE_BEAMS)?1u:0u,report))return -2;

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
