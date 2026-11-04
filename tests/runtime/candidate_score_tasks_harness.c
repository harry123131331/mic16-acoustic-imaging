/* 原候选/冻结网络逐位对照，不使用录音或硬件。 */
#include "mic16_candidate_pool_task.h"
#include "mic16_frozen_score_task.h"
#include <string.h>
#include <fenv.h>
#include <xmmintrin.h>
static m16_candidate_pool_task pool_task;
static mic16_spatial_workspace pool_work,old_pool_work;
static mic16_pool pool,old_pool;
static double votes[480*9],old_votes[480*9];
static unsigned short indices[480],old_indices[480];
static m16_frozen_score_task score_task;
static mic16_score_workspace score_work,old_score_work;
static float logits[16],old_logits[16];
static double probabilities[16],old_probabilities[16];
static const float *maps,*features;
static const double *levels,*floor_values,*directions;
static const unsigned char *valid_candidates;
void nt_environment(unsigned mode){
 unsigned x=_mm_getcsr();
 if(mode==1)fesetround(FE_DOWNWARD);else if(mode==2)_mm_setcsr(x|0x8000u);else if(mode==3)_mm_setcsr(x|0x40u);
 else{fesetround(FE_TONEAREST);_mm_setcsr(x&~0xe040u);}
}
int cp_setup(const float *m,const double *l,const double *f){
 unsigned i;int rc;nt_environment(0);
 memset(&pool_task,0,sizeof(pool_task));memset(&pool_work,0,sizeof(pool_work));memset(&old_pool_work,0,sizeof(old_pool_work));
 memset(&pool,0,sizeof(pool));memset(&old_pool,0,sizeof(old_pool));
 for(i=0;i<480u*9u;i++)votes[i]=old_votes[i]=-987.25;
 memset(indices,0,sizeof(indices));memset(old_indices,0,sizeof(old_indices));maps=m;levels=l;floor_values=f;
 rc=m16_candidate_pool_init(&pool_task,&pool_work);if(rc)return rc;
 return m16_candidate_pool_begin(&pool_task,m,M16_SP_MAP_FLOATS,l,480,f,votes,480u*9u,indices,&pool);
}
int cp_reference(void){
 int rc=mic16_spatial_votes(maps,levels,floor_values,old_votes,old_indices);
 return rc?rc:mic16_spatial_candidates(old_votes,&old_pool,&old_pool_work);
}
int cp_step(void){return m16_candidate_pool_step(&pool_task);}
int cp_run(unsigned limit,unsigned phase){
 unsigned i;int rc=1;for(i=0;i<limit && rc==1;i++){if(phase && pool_task.phase==phase)return 1;rc=cp_step();}return rc;
}
int cp_stop(void){return m16_candidate_pool_stop(&pool_task);}
int cp_releasable(void){return m16_candidate_pool_releasable(&pool_task);}
int cp_begin(void){return m16_candidate_pool_begin(&pool_task,maps,M16_SP_MAP_FLOATS,levels,480,floor_values,votes,480u*9u,indices,&pool);}
double *cp_votes(unsigned old){return old?old_votes:votes;}
unsigned short *cp_indices(unsigned old){return old?old_indices:indices;}
int cp_equal(void){
 return memcmp(&pool,&old_pool,sizeof(pool))==0 && memcmp(pool_work.clusters,old_pool_work.clusters,sizeof(pool_work.clusters))==0;
}
unsigned long long cp_metric(unsigned k){
 switch(k){case 0:return pool_task.phase;case 1:return pool.count;case 2:return pool_task.seed_count;case 3:return pool_task.steps;
 case 4:return pool_task.max_items;case 5:return sizeof(pool_task);default:return 0;}
}
int cp_alias(unsigned k){
 return m16_candidate_pool_begin(&pool_task,maps,M16_SP_MAP_FLOATS,levels,480,floor_values,
  k==0?(double *)maps:votes,480u*9u,indices,k==1?(mic16_pool *)&pool_task:&pool);
}
int nt_setup(const float *x,const double *d,const unsigned char *v){
 unsigned i;int rc;nt_environment(0);memset(&score_task,0,sizeof(score_task));memset(&score_work,0,sizeof(score_work));memset(&old_score_work,0,sizeof(old_score_work));
 for(i=0;i<16;i++){logits[i]=old_logits[i]=-987.25f;probabilities[i]=old_probabilities[i]=-987.25;}
 features=x;directions=d;valid_candidates=v;rc=m16_frozen_score_task_init(&score_task,&score_work);if(rc)return rc;
 return m16_frozen_score_task_begin(&score_task,x,16u*20u*130u,d,v,logits,probabilities);
}
int nt_reference(void){
 unsigned i;int rc=mic16_frozen_score(features,directions,valid_candidates,old_logits,&old_score_work);
 if(!rc){for(i=0;i<16;i++)old_probabilities[i]=mic16_frozen_probability(old_logits[i]);}
 return rc;
}
int nt_step(void){return m16_frozen_score_task_step(&score_task);}
int nt_run(unsigned limit,unsigned phase){
 unsigned i;int rc=1;for(i=0;i<limit && rc==1;i++){if(phase && score_task.phase==phase)return 1;rc=nt_step();}return rc;
}
int nt_stop(void){return m16_frozen_score_task_stop(&score_task);}
int nt_releasable(void){return m16_frozen_score_task_releasable(&score_task);}
int nt_begin(void){return m16_frozen_score_task_begin(&score_task,features,16u*20u*130u,directions,valid_candidates,logits,probabilities);}
float *nt_logits(unsigned old){return old?old_logits:logits;}
double *nt_probabilities(unsigned old){return old?old_probabilities:probabilities;}
int nt_equal(void){return memcmp(&score_work,&old_score_work,sizeof(score_work))==0;}
unsigned long long nt_metric(unsigned k){switch(k){case 0:return score_task.phase;case 1:return score_task.steps;case 2:return score_task.max_items;case 3:return sizeof(score_task);default:return 0;}}
int nt_alias(unsigned k){return m16_frozen_score_task_begin(&score_task,features,16u*20u*130u,directions,valid_candidates,
 k==0?(float *)features:logits,k==1?(double *)&score_work:probabilities);}
