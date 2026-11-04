/* 固定数值夹具，不是录音或仿真声学场景；不分配堆，不访问DMA、网络或PL，不改变模型行为。 */
#include "mic16_pipeline_unit.h"
#include "mic16_pipeline_gold.h"
extern int printf(const char *, ...);
static float pcm[40960*16] __attribute__((aligned(64)));
static float quiet[8192*16] __attribute__((aligned(64)));
static mic16_pipeline_workspace work __attribute__((aligned(64)));
static mic16_pipeline_observation observation __attribute__((aligned(64)));
static mic16_search_memory memory;
static mic16_cycle_output event;
volatile mic16_pipeline_test_report g_mic16_pipeline_test;
static unsigned int next_word(unsigned int *s) {
    *s = 1664525u*(*s)+1013904223u;
    return *s;
}
static unsigned int checksum(const float *x, unsigned int n) {
    unsigned int i,j,h=2166136261u;
    for(i=0;i<n;i++) {
        union { float f; unsigned int u; } v; v.f=x[i];
        for(j=0;j<4;j++) { h^=(v.u>>(8*j))&255u; h*=16777619u; }
    }
    return h;
}
static double delta(double a,double b) { double x=a-b; return x<0?-x:x; }
static void exact(int ok) {
    g_mic16_pipeline_test.checks++;
    if(!ok) g_mic16_pipeline_test.errors++;
}
static void near(double a,double b,double tolerance,volatile double *maximum) {
    double d=delta(a,b);
    if(d>*maximum) *maximum=d;
    exact(a==a && b==b && d<=tolerance);
}
void mic16_pipeline_test_prepare(unsigned int which) {
    unsigned int i,state=20261015u;
    g_mic16_pipeline_test.current_case=which;
    for(i=0;i<8192*16;i++) {
        int v=(int)((next_word(&state)>>16)&65535u)-32768;
        quiet[i]=(float)v*0x1p-25f;
    }
    state=314159265u;
    for(i=0;i<40960*16;i++) {
        unsigned int s=i/16,c=i%16;
        int v=(int)((next_word(&state)>>16)&65535u)-32768;
        int saw=(int)((s*17u+c*29u)%1024u)-512;
        pcm[i]=which?((float)saw*0x1p-13f+(float)v*0x1p-20f):0.f;
    }
    g_mic16_pipeline_test.pcm_checksum=checksum(pcm,40960*16);
    g_mic16_pipeline_test.reference_checksum=checksum(quiet,8192*16);
}
int mic16_pipeline_test_compute(void) {
    return mic16_pipeline_observe(pcm,quiet,&observation,&work);
}
int mic16_pipeline_test_check(unsigned int which) {
    const mic16_d3f_gold *g=&mic16_d3f_expected[which];
    unsigned int i,t,k,cycle,probe=0,before=g_mic16_pipeline_test.errors;
    double empty[16]={0};
    exact(which<2);
    exact(g_mic16_pipeline_test.pcm_checksum==g->pcm_checksum);
    exact(g_mic16_pipeline_test.reference_checksum==g->reference_checksum);
    exact(observation.candidate_count==g->count);
    for(i=0;i<480;i++) exact(work.best_indices[i]==g->best[i]);
    for(i=0;i<32;i++) near(observation.directions[i],g->directions[i],1e-4,&g_mic16_pipeline_test.direction_max_error);
    g_mic16_pipeline_test.qualified=0;
    for(i=0;i<16;i++) {
        exact(observation.valid[i]==g->valid[i]);
        near(observation.logits[i],g->logits[i],1e-4,&g_mic16_pipeline_test.logit_max_error);
        near(observation.scores[i],g->scores[i],3e-5,&g_mic16_pipeline_test.score_max_error);
        exact((observation.scores[i]>=.65)==(g->scores[i]>=.65));
        if(observation.scores[i]>=.65) g_mic16_pipeline_test.qualified++;
        for(t=0;t<20;t++) for(k=0;k<10;k++)
            near(observation.features[(i*20+t)*130+mic16_d3f_probes[k]],g->probes[probe++],5e-5,&g_mic16_pipeline_test.feature_max_error);
    }
    mic16_memory_init(&memory);
    for(cycle=0;cycle<5;cycle++) {
        const mic16_d3f_gold_event *ge=&g->events[cycle];
        int rc=mic16_consume_scored_cycle(&memory,observation.directions,observation.valid,
            cycle<2?observation.scores:empty,(cycle+1)*1.048576,"fixed_numerical_reference",1,&event);
        exact(rc==0); if(rc) continue;
        exact(event.cycle==ge->cycle); exact(event.count==ge->count);
        exact(event.confirmed_count==ge->confirmed);
        exact((event.new_source_valid?event.new_source.source_id:0u)==ge->new_id);
        exact(event.expired_count==ge->expired_count);
        for(i=0;i<(unsigned int)event.expired_count;i++) exact(event.expired_ids[i]==ge->expired[i]);
        for(i=0;i<(unsigned int)event.count && i<(unsigned int)ge->count;i++) {
            const mic16_track *a=&event.tracks[i]; const mic16_d3f_gold_track *b=&ge->tracks[i];
            exact(a->source_id==b->id); exact(a->first_seen_cycle==b->first);
            exact(a->last_seen_cycle==b->last); exact(a->confirmed_this_cycle==b->current);
            near(a->azimuth,b->a,1e-4,&g_mic16_pipeline_test.direction_max_error);
            near(a->elevation,b->e,1e-4,&g_mic16_pipeline_test.direction_max_error);
            near(a->score,b->score,3e-5,&g_mic16_pipeline_test.score_max_error);
        }
    }
    g_mic16_pipeline_test.completed_cases++;
    printf("D3F_CASE case=%u candidates=%u qualified=%u checks=%u errors=%u\n",which,
        observation.candidate_count,g_mic16_pipeline_test.qualified,
        g_mic16_pipeline_test.checks,g_mic16_pipeline_test.errors);
    return (int)(g_mic16_pipeline_test.errors-before);
}
unsigned int mic16_pipeline_test_static_bytes(void) {
    return sizeof(pcm)+sizeof(quiet)+sizeof(work)+sizeof(observation)+sizeof(memory)+sizeof(event);
}
