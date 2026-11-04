/* 输入是统计前端已完成的频谱/功率数值夹具；不把它称为原始PCM端到端。 */
#include "mic16_statistics_projection_task.h"
#include <string.h>
extern void *sp_runtime_bridge(void);
extern int m16_statistics_neon(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
extern int sp_fp64_reference(const double *,unsigned,float *,mic16_extended_workspace *,mic16_feature_reuse_report *);
static m16_statistics_projection_task task;
static m16_statistics_projection_storage storage;
static mic16_extended_workspace work,reference;
static struct{float before[16],values[16*360],after[16];} output;
static float expected[16*360];
static double directions[32];static unsigned count,embedded;
int m16_original_statistics18_pair_cache(const double *d,unsigned n,float *out,mic16_extended_workspace *w,mic16_feature_reuse_report *r){return sp_fp64_reference(d,n,out,w,r);}
int sp_setup(const double *observed,const double *power,const double *channel,const double *quiet,const double *d,unsigned c,unsigned inside){
 unsigned i;int rc;m16_projection_runtime *r=(m16_projection_runtime *)sp_runtime_bridge();if(!r || c<1 || c>16)return -999;
 memset(&task,0,sizeof(task));memset(&storage,0,sizeof(storage));memset(&work,0,sizeof(work));memset(&reference,0,sizeof(reference));
 memcpy(work.observed,observed,sizeof(work.observed));memcpy(reference.observed,observed,sizeof(reference.observed));
 memcpy(work.beam_power,power,c*67u*64u*8u);memcpy(reference.beam_power,power,c*67u*64u*8u);
 memcpy(work.channel_power,channel,sizeof(work.channel_power));memcpy(reference.channel_power,channel,sizeof(reference.channel_power));
 memcpy(work.quiet_power,quiet,sizeof(work.quiet_power));memcpy(reference.quiet_power,quiet,sizeof(reference.quiet_power));
 count=c;embedded=inside;memcpy(directions,d,c*16u);
 for(i=0;i<16;i++)output.before[i]=output.after[i]=-1234.75f;
 for(i=0;i<16u*360u;i++)output.values[i]=work.statistics18[i]=expected[i]=-987.25f;
 rc=m16_statistics_projection_init(&task,r,&work,&storage);if(rc)return rc;
 return m16_statistics_projection_begin(&task,d,c,inside?work.statistics18:output.values,c*360u,12,1000);
}
int sp_reference(void){
 if(count==1)return sp_fp64_reference(directions,count,expected,&reference,0);
 return m16_statistics_neon(directions,count,expected,&reference,0);
}
int sp_step(void){return m16_statistics_projection_step(&task);}
int sp_stop(void){return m16_statistics_projection_stop(&task);}
int sp_releasable(void){return m16_statistics_projection_releasable(&task);}
int sp_begin(uint64_t epoch,uint64_t tag){return m16_statistics_projection_begin(&task,directions,count,embedded?work.statistics18:output.values,count*360u,epoch,tag);}
int sp_copy(unsigned old,float *out){memcpy(out,old?expected:(embedded?work.statistics18:output.values),count*360u*4u);return 0;}
int sp_work_copy(unsigned old,double *signature,float *summary,float *spectral){
 mic16_extended_workspace *w=old?&reference:&work;
 memcpy(signature,w->signature,sizeof(w->signature));memcpy(summary,w->summary18,sizeof(w->summary18));memcpy(spectral,w->spectral10,sizeof(w->spectral10));return 0;
}
int sp_inputs_copy(double *observed,double *power,double *channel,double *quiet){
 memcpy(observed,work.observed,sizeof(work.observed));memcpy(power,work.beam_power,count*67u*64u*8u);
 memcpy(channel,work.channel_power,sizeof(work.channel_power));memcpy(quiet,work.quiet_power,sizeof(work.quiet_power));return 0;
}
int sp_guards(void){
 unsigned i;for(i=0;i<16;i++)if(output.before[i]!=-1234.75f || output.after[i]!=-1234.75f)return -1;
 for(i=count*360u;i<16u*360u;i++)if((embedded?work.statistics18:output.values)[i]!=-987.25f)return -2;
 return 0;
}
uint64_t sp_metric(unsigned k){
 switch(k){case 0:return task.phase;case 1:return task.owned;case 2:return task.frame_start;case 3:return task.first_bin;
 case 4:return task.transactions;case 5:return task.rotations;case 6:return task.pair_normalizations;case 7:return task.prefetch_pairs;
 case 8:return task.device_prefetch_pairs;case 9:return task.last_items;case 10:return task.max_items;case 11:return task.steps;
 case 12:return task.reserved_tag_high;case 13:return task.out;case 14:return task.receipt;case 15:return task.max_used;
 case 16:return sizeof(storage);case 17:return task.next_cursor;case 18:return task.buffer;case 19:return (uint64_t)(int64_t)task.error;
 default:return UINT64_MAX;}
}
void sp_bad_input(unsigned region,unsigned i){
 union{uint64_t u;double d;}n;n.u=UINT64_C(0x7ff8000000000000);
 if(region==0 && i<67u*257u*16u*2u)work.observed[i]=n.d;
 if(region==1 && i<count*67u*64u)work.beam_power[i]=n.d;
 if(region==2 && i<67u*64u)work.channel_power[i]=-1.;
 if(region==3 && i<64u)work.quiet_power[i]=n.d;
}
int sp_bad_begin(unsigned k){
 const double *d=directions;float *out=output.values;unsigned c=count,n=count*360u;uint64_t epoch=12,tag=2000;
 switch(k){case 0:d=(const double *)&task;break;case 1:out=(float *)work.observed;break;case 2:out=(float *)&storage;break;
 case 3:n--;break;case 4:c=17;break;case 5:epoch=0;break;case 6:tag=UINT64_MAX;break;case 7:tag=1000;break;
 default:return -999;}
 return m16_statistics_projection_begin(&task,d,c,out,n,epoch,tag);
}
