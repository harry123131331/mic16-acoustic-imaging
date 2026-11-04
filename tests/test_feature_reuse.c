#include "mic16_pipeline.h"
#include "mic16_enhancement.h"
#include <stddef.h>
extern int printf(const char *,...);
extern void *memset(void *,int,size_t);
extern void *memcpy(void *,const void *,size_t);
extern int memcmp(const void *,const void *,size_t);
extern double sin(double),cos(double),hypot(double,double);
extern long clock(void);
typedef struct {
    mic16_spectral_workspace spectral;mic16_spectral_frame frame;mic16_spectral_reference reference;
    float phat[20*807*32*2];double levels[20*24],powers[807],scratch[2048];
    float beam[40960],quiet_beam[8192],signature[32*34*2];
    double bands[20*32],beam_level[20],quiet_level[4],errors[480];
} prior_physical_layout;
typedef char same_physical_size[(sizeof(prior_physical_layout)==sizeof(mic16_physical_workspace))?1:-1];
typedef char same_phat_offset[(offsetof(prior_physical_layout,phat)==offsetof(mic16_physical_workspace,phat))?1:-1];
typedef char same_levels_offset[(offsetof(prior_physical_layout,levels)==offsetof(mic16_physical_workspace,levels))?1:-1];
typedef char cache_fits[(sizeof(mic16_statistics_reuse_storage)<=sizeof(((mic16_physical_workspace *)0)->phat))?1:-1];
typedef struct {unsigned char before[64];mic16_extended_workspace w;unsigned char after[64];} guarded_extended;
static guarded_extended reference_ext,current_ext;
static float pcm[40960*16],quiet[8192*16];
static float baseline_features[16*20*130],current_features[16*20*130];
static double directions[32],baseline_directions[32],current_directions[32];
static unsigned char baseline_valid[16],current_valid[16];
static mic16_pipeline_workspace baseline_work,current_work;
static mic16_pipeline_observation baseline_output,current_output;
static unsigned checks,errors;static unsigned spectral_calls,statistics_beam_calls,hypot_calls;
static unsigned long long tick;
/* 强制两个当前轨迹仅用于数值衔接核验，不代表夹具的声学真值。 */
static m16_enh_workspace enhancement_work;
static mic16_cycle_output enhancement_tracks;
typedef struct {
    unsigned id,candidate,samples;
    float features[67*64*10],mask[67*64];
    double spectrum[67*257*2],wdas[16778],enhanced[16778];
} saved_enhancement;
static saved_enhancement enhancement_baseline[2];
static unsigned enhancement_mode,enhancement_calls;
#define CHECK(x) do{checks++;if(!(x)){errors++;printf("FEATURE_REUSE_ERROR line=%u check=%u\n",(unsigned)__LINE__,checks);}}while(0)
double feature_test_hypot(double a,double b){hypot_calls++;return hypot(a,b);}
int feature_test_spectral(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){spectral_calls++;return mic16_spectral_process(x,o,w);}
int feature_test_candidate_beam(const float *x,unsigned n,const double *d,float *out,unsigned *guard){statistics_beam_calls++;return mic16_candidate_beam(x,n,d,out,guard);}
static void reset_counts(void){spectral_calls=statistics_beam_calls=hypot_calls=0;}
static unsigned long long read_tick(void *p){(void)p;return ++tick;}
static void guards(guarded_extended *g){memset(g->before,0x6d,64);memset(g->after,0xb3,64);}
static void check_guards(const guarded_extended *g){unsigned i,ok=1;for(i=0;i<64;i++)if(g->before[i]!=0x6d || g->after[i]!=0xb3)ok=0;CHECK(ok);}
static void fill(unsigned fixture){unsigned i,s=20261020u+fixture;
    for(i=0;i<40960*16;i++){unsigned n=i/16,c=i%16;s=s*1664525u+1013904223u;
        pcm[i]=(float)(.11*sin(.11*(n-.21*c))+.04*cos(.41*(n+.13*c))+.001*((int)(s>>16)-32768)/32768.);}
    for(i=0;i<8192*16;i++)quiet[i]=(float)((fixture?.0017:.001)*sin(.19*(i/16)+.27*(i%16)+(fixture?.17:0.)));
}
static void pipeline_cases(void){unsigned fixture;mic16_pipeline_profile p;mic16_profile_clock c={0,read_tick};
    for(fixture=0;fixture<2;fixture++){
        fill(fixture);memset(&baseline_work,0,sizeof(baseline_work));memset(&current_work,0,sizeof(current_work));
        memset(&baseline_output,0,sizeof(baseline_output));memset(&current_output,0,sizeof(current_output));
        CHECK(mic16_pipeline_observe(pcm,quiet,&baseline_output,&baseline_work)==0);
        CHECK(mic16_pipeline_observe_reused_timed(pcm,quiet,&current_output,&current_work,&c,&p)==0);
        CHECK(p.completed_mask==63u && p.attempted_mask==63u && p.clock_ok && p.reads==12u);
        CHECK(!memcmp(&baseline_output,&current_output,sizeof(baseline_output)));
        CHECK(!memcmp(baseline_work.maps,current_work.maps,sizeof(baseline_work.maps)));
        CHECK(!memcmp(baseline_work.votes,current_work.votes,sizeof(baseline_work.votes)));
        CHECK(!memcmp(baseline_work.best_indices,current_work.best_indices,sizeof(baseline_work.best_indices)));
        CHECK(!memcmp(&baseline_work.pool,&current_work.pool,sizeof(baseline_work.pool)));
        CHECK(!memcmp(baseline_work.extended.physical88,current_work.extended.physical88,baseline_work.pool.count*20u*88u*4u));
        CHECK(!memcmp(baseline_work.extended.statistics18,current_work.extended.statistics18,baseline_work.pool.count*20u*18u*4u));
        printf("FEATURE_REUSE_PIPELINE fixture=%u candidates=%u output_bit_differences=0 ONLY_IF_NO_ERRORS\n",fixture,baseline_work.pool.count);
    }
}
static int enhancement_sink(void *context,unsigned id,unsigned candidate,const float *features,
    const float *mask,const double *spectrum,const double *wdas,const double *enhanced,unsigned samples){
    saved_enhancement *s;(void)context;
    if(enhancement_calls>=2)return -1;s=enhancement_baseline+enhancement_calls++;
    if(!enhancement_mode){
        s->id=id;s->candidate=candidate;s->samples=samples;
        memcpy(s->features,features,sizeof(s->features));memcpy(s->mask,mask,sizeof(s->mask));
        memcpy(s->spectrum,spectrum,sizeof(s->spectrum));memcpy(s->wdas,wdas,sizeof(s->wdas));
        memcpy(s->enhanced,enhanced,sizeof(s->enhanced));
    }else{
        CHECK(id==s->id && candidate==s->candidate && samples==s->samples);
        CHECK(!memcmp(s->features,features,sizeof(s->features)));
        CHECK(!memcmp(s->mask,mask,sizeof(s->mask)));
        CHECK(!memcmp(s->spectrum,spectrum,sizeof(s->spectrum)));
        CHECK(!memcmp(s->wdas,wdas,sizeof(s->wdas)));
        CHECK(!memcmp(s->enhanced,enhanced,sizeof(s->enhanced)));
    }
    return 0;
}
static void enhancement_interop(void){
    unsigned i,n=0;memset(&enhancement_tracks,0,sizeof(enhancement_tracks));
    for(i=0;i<16 && n<2;i++)if(baseline_output.valid[i]){
        mic16_track *tr=enhancement_tracks.tracks+n;
        tr->source_id=17+n;tr->azimuth=baseline_output.directions[i*2];
        tr->elevation=baseline_output.directions[i*2+1];tr->score=.9;tr->confirmed_this_cycle=1;n++;
    }
    CHECK(n==2);enhancement_tracks.count=enhancement_tracks.confirmed_count=(int)n;
    enhancement_mode=0;enhancement_calls=0;
    CHECK(m16_enh_window(pcm,quiet,baseline_output.directions,baseline_output.valid,
        &enhancement_tracks,&baseline_work.extended,&enhancement_work,enhancement_sink,0)==2);
    CHECK(enhancement_calls==2);enhancement_mode=1;enhancement_calls=0;
    CHECK(m16_enh_window(pcm,quiet,current_output.directions,current_output.valid,
        &enhancement_tracks,&current_work.extended,&enhancement_work,enhancement_sink,0)==2);
    CHECK(enhancement_calls==2);
    printf("FEATURE_REUSE_ENHANCEMENT sources=2 samples_per_source=16778 compared=features_mask_spectrum_wdas_audio bit_differences=0 ONLY_IF_NO_ERRORS\n");
}
static void ablations(void){
    unsigned counts[5]={1,2,4,8,16},flags[4]={M16_REUSE_COMMON,M16_REUSE_PAIRS,M16_REUSE_BEAMS,M16_REUSE_ALL};
    unsigned a,b,c,count,flag,old_hypot,old_spectral,old_beams;long start,old_clock,new_clock;
    mic16_feature_reuse_report report;mic16_prepared_spectral prepared;
    fill(0);
    for(c=0;c<16;c++){directions[c*2]=-50.+7.*c;directions[c*2+1]=-15.+10.*(c%4);}
    for(a=0;a<5;a++){
        count=counts[a];guards(&reference_ext);guards(&current_ext);reset_counts();start=clock();
        CHECK(mic16_features130(pcm,quiet,directions,count,baseline_work.votes,baseline_features,baseline_directions,baseline_valid,&reference_ext.w)==0);
        old_clock=clock()-start;old_hypot=hypot_calls;old_spectral=spectral_calls;old_beams=statistics_beam_calls;
        CHECK(old_hypot==count*67u*256u*33u && old_spectral==24u && old_beams==count);
        for(b=0;b<4;b++){
            flag=flags[b];memcpy(&current_ext.w.physical,&reference_ext.w.physical,sizeof(current_ext.w.physical));
            prepared.pcm=pcm;prepared.reference=quiet;prepared.owner=&current_ext.w.physical;
            reset_counts();start=clock();
            CHECK(mic16_features130_reused(pcm,quiet,directions,count,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,flag,&report)==0);
            new_clock=clock()-start;
            CHECK(!memcmp(baseline_features,current_features,sizeof(baseline_features)));
            CHECK(!memcmp(baseline_directions,current_directions,sizeof(baseline_directions)));
            CHECK(!memcmp(baseline_valid,current_valid,sizeof(baseline_valid)));
            CHECK(!memcmp(reference_ext.w.physical88,current_ext.w.physical88,count*20u*88u*4u));
            CHECK(!memcmp(reference_ext.w.statistics18,current_ext.w.statistics18,count*20u*18u*4u));
            CHECK(!memcmp(reference_ext.w.observed,current_ext.w.observed,sizeof(reference_ext.w.observed)));
            CHECK(!memcmp(reference_ext.w.beam_power,current_ext.w.beam_power,count*67u*64u*8u));
            CHECK(!memcmp(reference_ext.w.quiet_power,current_ext.w.quiet_power,sizeof(reference_ext.w.quiet_power)));
            CHECK(spectral_calls==((flag&M16_REUSE_COMMON)?0u:24u));
            CHECK(statistics_beam_calls==((flag&M16_REUSE_BEAMS)?0u:count));
            CHECK(hypot_calls==(((flag&M16_REUSE_PAIRS) && count>1)?67u*256u*32u+count*67u*256u:old_hypot));
            CHECK(report.common_frames_reused==((flag&M16_REUSE_COMMON)?24u:0u));
            CHECK(report.beam_transforms_reused==((flag&M16_REUSE_BEAMS)?count:0u));
            CHECK(report.pair_normalizations==67u*256u*32u*(((flag&M16_REUSE_PAIRS) && count>1)?1u:count));
            check_guards(&current_ext);
            printf("FEATURE_REUSE_ABLATION count=%u flags=%u before_spectral=%u after_spectral=%u before_stats_beams=%u after_stats_beams=%u before_hypot=%u after_hypot=%u before_host_clock=%ld after_host_clock=%ld output_bit_differences=0 ONLY_IF_NO_ERRORS\n",
                count,flag,old_spectral,spectral_calls,old_beams,statistics_beam_calls,old_hypot,hypot_calls,old_clock,new_clock);
        }
        check_guards(&reference_ext);
    }
    memcpy(&current_ext.w.physical,&reference_ext.w.physical,sizeof(current_ext.w.physical));
    prepared.pcm=pcm;prepared.reference=quiet;prepared.owner=&current_ext.w.physical;
    current_features[0]=123.f;
    CHECK(mic16_features130_reused(pcm,quiet,directions,0,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,7,&report)<0);
    CHECK(mic16_features130_reused(pcm,quiet,directions,17,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,7,&report)<0);
    CHECK(mic16_features130_reused(pcm,quiet,directions,1,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,8,&report)<0);
    CHECK(mic16_features130_reused(pcm,quiet,directions,1,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,0,1,&report)<0);
    prepared.reference=pcm;
    CHECK(mic16_features130_reused(pcm,quiet,directions,1,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,1,&report)<0);
    prepared.reference=quiet;current_ext.w.physical.reference.finalized=0;
    CHECK(mic16_features130_reused(pcm,quiet,directions,1,baseline_work.votes,current_features,current_directions,current_valid,&current_ext.w,&prepared,1,&report)<0);
    CHECK(current_features[0]==123.f);check_guards(&current_ext);
}
int main(void){
    CHECK(sizeof(prior_physical_layout)==sizeof(mic16_physical_workspace));
    printf("FEATURE_REUSE_MEMORY physical_before=%u physical_after=%u reuse_storage=%u no_heap=1\n",(unsigned)sizeof(prior_physical_layout),(unsigned)sizeof(mic16_physical_workspace),(unsigned)sizeof(mic16_statistics_reuse_storage));
    pipeline_cases();enhancement_interop();ablations();
    printf("FEATURE_REUSE_%s checks=%u errors=%u ARM_TIMING_MEASURED=0 BOARD_OPERATIONS=0\n",errors?"FAIL":"PASS",checks,errors);
    return errors?1:0;
}
