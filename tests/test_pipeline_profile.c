#include "mic16_pipeline.h"
extern int printf(const char *,...);
extern void *memset(void *,int,__SIZE_TYPE__);
extern int memcmp(const void *,const void *,__SIZE_TYPE__);
extern double sin(double);
extern double cos(double);
extern int baseline_pipeline_observe(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);

static float pcm[40960*16],reference[8192*16];
static mic16_pipeline_workspace baseline_work,current_work;
static mic16_pipeline_observation baseline_output,current_output;
static unsigned checks,errors;
typedef struct {unsigned long long tick;unsigned reads;int backwards;} fake_clock;
static unsigned long long clock_read(void *context){
    fake_clock *c=(fake_clock *)context;c->reads++;
    if(c->backwards)c->tick-=10;else c->tick+=10;
    return c->tick;
}
static void check(int condition){checks++;if(!condition){errors++;printf("PROFILE_CHECK_FAIL %u\n",checks);}}
static void same_result(void){
    check(!memcmp(&baseline_output,&current_output,sizeof(current_output)));
    check(!memcmp(baseline_work.maps,current_work.maps,sizeof(current_work.maps)));
    check(!memcmp(baseline_work.votes,current_work.votes,sizeof(current_work.votes)));
    check(!memcmp(baseline_work.best_indices,current_work.best_indices,sizeof(current_work.best_indices)));
    check(!memcmp(baseline_work.physical_directions,current_work.physical_directions,sizeof(current_work.physical_directions)));
    check(!memcmp(&baseline_work.pool,&current_work.pool,sizeof(current_work.pool)));
}
int main(void){
    unsigned n,c,i,case_id;unsigned int random=71;
    fake_clock timer={0,0,0};mic16_profile_clock clock={&timer,clock_read},bad_clock={&timer,0};
    mic16_pipeline_profile profile,untouched;int rc;
    /* 仅生成确定性数值夹具，不代表房间、真实源数或声学性能。 */
    for(n=0;n<40960;n++)for(c=0;c<16;c++){
        random=random*1664525u+1013904223u;
        pcm[n*16+c]=(float)(.11*sin(.11*(n-.21*c))+.04*cos(.41*(n+.13*c))+.001*((int)(random>>16)-32768)/32768.);
    }
    for(n=0;n<8192;n++)for(c=0;c<16;c++)reference[n*16+c]=(float)(.001*sin(.19*n+.27*c));
    memset(&profile,0x75,sizeof(profile));untouched=profile;current_output.candidate_count=77;
    check(mic16_pipeline_observe_timed(0,reference,&current_output,&current_work,&clock,&profile)==-1);
    check(mic16_pipeline_observe_timed(pcm,reference,&current_output,&current_work,0,&profile)==-1);
    check(mic16_pipeline_observe_timed(pcm,reference,&current_output,&current_work,&bad_clock,&profile)==-1);
    check(mic16_pipeline_observe_timed(pcm,reference,&current_output,&current_work,&clock,0)==-1);
    check(mic16_pipeline_observe_timed(pcm,reference,&current_output,&current_work,&clock,(mic16_pipeline_profile *)&current_output)==-1);
    check(!memcmp(&profile,&untouched,sizeof(profile)) && current_output.candidate_count==77 && timer.reads==0);
    for(case_id=0;case_id<2;case_id++){
        if(case_id)for(n=0;n<40960*16;n++)pcm[n]*=.6f;
        memset(&baseline_work,0,sizeof(baseline_work));memset(&current_work,0,sizeof(current_work));
        memset(&baseline_output,0,sizeof(baseline_output));memset(&current_output,0,sizeof(current_output));
        check(baseline_pipeline_observe(pcm,reference,&baseline_output,&baseline_work)==0);
        timer.tick=10000;timer.reads=0;timer.backwards=(int)case_id;
        rc=mic16_pipeline_observe_timed(pcm,reference,&current_output,&current_work,&clock,&profile);
        check(rc==0);same_result();
        check(profile.attempted_mask==63 && profile.completed_mask==63 && profile.pipeline_rc==0);
        check(profile.reads==12 && timer.reads==12);
        check(profile.clock_ok==(case_id?0u:1u));
        for(i=0;i<MIC16_PROFILE_STAGES;i++)check(profile.ticks[i]==(case_id?0u:10u));
        printf("PROFILE_CASE case=%u candidates=%u clock_ok=%u output_bit_differences=0\n",case_id,current_output.candidate_count,profile.clock_ok);
        if(!case_id){
            memset(&current_output,0,sizeof(current_output));
            check(mic16_pipeline_observe(pcm,reference,&current_output,&current_work)==0);same_result();
        }
    }
    printf("PIPELINE_PROFILE_PASS cases=2 checks=%u errors=%u fake_clock_not_ARM_timing=1\n",checks,errors);
    return errors?1:0;
}
