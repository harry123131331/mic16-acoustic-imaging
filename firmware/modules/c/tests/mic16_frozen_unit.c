#include "mic16_frozen.h"
#include "mic16_frozen_gold.h"
extern int printf(const char *,...);
static float features[16*20*130],logits[16];
static double directions[32],scores[16];
static unsigned char valid[16];
static mic16_score_workspace workspace;
static mic16_search_memory memory;
static double absd(double x){return x<0 ? -x : x;}
static unsigned int feature_fnv(void){
    unsigned int h=2166136261u;int i,j;
    for(i=0;i<16*20*130;i++) { union {float f;unsigned int u;} a;a.f=features[i];for(j=0;j<4;j++){h^=(a.u>>(8*j))&255u;h*=16777619u;} }
    return h;
}
static void fixture(int which){
    int c,t,f;
    for(c=0;c<16;c++){
        int b=which==6 ? 15-c : c,q=which==6 ? 1 : which;
        directions[c*2]=-50.+7*b;directions[c*2+1]=-10.+20*(b%2);
        if(b==0){directions[c*2]=0;directions[c*2+1]=0;}
        if(b==1){directions[c*2]=15;directions[c*2+1]=0;}
        if(b==2){directions[c*2]=15.00000001;directions[c*2+1]=0;}
        if(which==7){directions[c*2]=c<15 ? -60.+30*(c%5) : -45.;directions[c*2+1]=c<15 ? -30.+30*(c/5) : 15.;}
        valid[c]=(unsigned char)(which==3 ? 0 : (which==2 ? b%2==0 : 1));
        for(t=0;t<20;t++)for(f=0;f<130;f++){
            float x;
            if(q==0)x=0;
            else if(q==4)x=((b+t+f)%2 ? 1.0f : -1.0f)*(f>=106 ? 2.0f : 1.0f);
            else x=(float)((b*131+t*17+f*7+q*19)%101-50)/50.0f;
            features[(c*20+t)*130+f]=x;
        }
    }
}
static int same_track(mic16_track a,mic16_track b){
    return a.source_id==b.source_id && a.first_seen_cycle==b.first_seen_cycle && a.last_seen_cycle==b.last_seen_cycle &&
           a.confirmed_this_cycle==b.confirmed_this_cycle && absd(a.azimuth-b.azimuth)<1e-12 &&
           absd(a.elevation-b.elevation)<1e-12 && absd(a.score-b.score)<1e-12;
}
static int same_memory(const mic16_search_memory *a,const mic16_search_memory *b){
    int i;
    if(a->count!=b->count || a->cycle!=b->cycle || a->next_id!=b->next_id || a->last_audio_end!=b->last_audio_end || a->has_previous_cycle!=b->has_previous_cycle)return 0;
    for(i=0;i<a->count;i++)if(!same_track(a->tracks[i],b->tracks[i]))return 0;
    for(i=0;i<MIC16_POSE_KEY_BYTES;i++){if(a->pose_key[i]!=b->pose_key[i])return 0;if(!a->pose_key[i])break;}
    return 1;
}
int mic16_frozen_unit_test(void){
    int which,c,k,rc,fail=0,mask_errors=0,eligibility_errors=0,memory_errors=0,error_checks=0;
    double max_error=0,max_score=0,max_stats=0;
    for(which=0;which<8;which++){
        double one_error=0;fixture(which);
        if(feature_fnv()!=gold_fnv[which]){printf("FIXTURE_INPUT_MISMATCH %d\n",which);return 1;}
        rc=mic16_frozen_score(features,directions,valid,logits,&workspace);if(rc){printf("SCORE_ERROR %d %d\n",which,rc);return 2;}
        for(c=0;c<16;c++){
            unsigned int mask=0;
            double e=absd((double)logits[c]-gold_logits[which][c]);
            double p=mic16_frozen_probability(logits[c]),g=mic16_frozen_probability(gold_logits[which][c]);
            if(e>one_error)one_error=e;if(e>max_error)max_error=e;if(absd(p-g)>max_score)max_score=absd(p-g);
            if((p>=.65)!=(g>=.65))eligibility_errors++;
            for(k=0;k<16;k++)mask|=(unsigned int)workspace.neighbors[c*16+k]<<k;
            if(mask!=gold_mask[which][c])mask_errors++;
            if(valid[c])for(k=0;k<7;k++){double se=absd((double)workspace.own[c*71+64+k]-gold_stats[which][c][k]);if(se>max_stats)max_stats=se;}
        }
        printf("NUMERIC case=%d max_error=%.17g\n",which,one_error);
    }
    mic16_memory_init(&memory);
    for(which=0;which<MEMORY_CASES;which++){
        const memory_fixture *f=memory_cases+which;mic16_cycle_output out;const mic16_cycle_output *e=&f->expected;
        if(f->reset)mic16_memory_init(&memory);
        rc=mic16_consume_scored_cycle(&memory,f->d,f->v,f->s,f->time,f->key,f->contiguous,&out);
        if(rc || out.count!=e->count || out.confirmed_count!=e->confirmed_count || out.expired_count!=e->expired_count ||
           out.new_source_valid!=e->new_source_valid || out.cycle!=e->cycle || out.pose_reset!=e->pose_reset || out.gap_reset!=e->gap_reset){memory_errors++;printf("MEMORY_FIELDS %d rc=%d count=%d expected=%d\n",which,rc,out.count,e->count);continue;}
        for(c=0;c<out.count;c++)if(!same_track(out.tracks[c],e->tracks[c])){memory_errors++;printf("MEMORY_TRACK %d %d\n",which,c);}
        if(out.new_source_valid && !same_track(out.new_source,e->new_source)){memory_errors++;printf("MEMORY_NEW %d\n",which);}
        for(c=0;c<out.expired_count;c++)if(out.expired_ids[c]!=e->expired_ids[c])memory_errors++;
    }
    {
        mic16_search_memory before=memory;mic16_cycle_output out;
        const memory_fixture *f=memory_cases+MEMORY_CASES-1;
        for(c=0;c<16;c++){scores[c]=f->s[c];valid[c]=f->v[c];directions[c*2]=f->d[c*2];directions[c*2+1]=f->d[c*2+1];}
        rc=mic16_consume_scored_cycle(&memory,directions,valid,scores,f->time,f->key,1,&out);
        if(rc!=-2 || !same_memory(&memory,&before))fail++;error_checks++;
        valid[0]=2;rc=mic16_consume_scored_cycle(&memory,directions,valid,scores,f->time+1,f->key,1,&out);
        if(rc!=-1 || !same_memory(&memory,&before))fail++;error_checks++;valid[0]=1;
        scores[0]=1.1;rc=mic16_consume_scored_cycle(&memory,directions,valid,scores,f->time+1,f->key,1,&out);
        if(rc!=-1 || !same_memory(&memory,&before))fail++;error_checks++;
        scores[0]=.9;rc=mic16_consume_scored_cycle(&memory,directions,valid,scores,f->time+1,"",1,&out);
        if(rc!=-1 || !same_memory(&memory,&before))fail++;error_checks++;
        fixture(1);valid[0]=2;if(mic16_frozen_score(features,directions,valid,logits,&workspace)!=-1)fail++;error_checks++;
        valid[0]=1;{union{unsigned int u;float f;} a;a.u=0x7fc00000u;features[0]=a.f;}
        if(mic16_frozen_score(features,directions,valid,logits,&workspace)!=-1)fail++;error_checks++;
        fixture(1);if(mic16_frozen_score(0,directions,valid,logits,&workspace)!=-1)fail++;error_checks++;
    }
    printf("C_REFERENCE max_logit_error=%.17g max_score_error=%.17g max_stats_error=%.17g mask_errors=%d eligibility_errors=%d memory_errors=%d memory_cases=%d invalid_checks=%d invalid_errors=%d workspace_bytes=%u memory_bytes=%u\n",
       max_error,max_score,max_stats,mask_errors,eligibility_errors,memory_errors,MEMORY_CASES,error_checks,fail,(unsigned int)sizeof(workspace),(unsigned int)sizeof(memory));
    return fail || max_error>5e-5 || max_stats>5e-6 || mask_errors || eligibility_errors || memory_errors;
}
