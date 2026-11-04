/* 离线单因素消费者：复用原整链数值夹具与原黄金，不修改生产入口。 */
#include "mic16_pipeline_unit.h"
#include "mic16_stft_batch.h"
#include "mic16_frontend_bridge.h"
#include <stddef.h>
#include <stdint.h>
extern void *fopen(const char *,const char *);
extern size_t fread(void *,size_t,size_t,void *),fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *),strcmp(const char *,const char *);
extern int printf(const char *,...);
static int test_observe(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);
static int test_consume(mic16_search_memory *,const double *,const unsigned char *,const double *,double,const char *,int,mic16_cycle_output *);
/* 仅在本测试翻译单元重定向调用；原prepare/check及所有黄金保持原文。 */
#define mic16_pipeline_observe test_observe
#define mic16_consume_scored_cycle test_consume
#include "mic16_pipeline_unit.c"
#undef mic16_pipeline_observe
#undef mic16_consume_scored_cycle
extern int m16_probe_pipeline_batched(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);
static void *cache,*events;
static const char *cache_directory;
static unsigned use_hls,provider_case,frame_index,cache_reads,total_cache_reads,score_forwards,events_written;
static uint64_t spectrum_words[M16_FRONT_SPECTRUM_WORDS];
static int write_(const void *p,size_t bytes,void *file){return fwrite(p,1,bytes,file)==bytes?0:-1;}
/* 两条分支调用相同冻结评分器，单独计数避免将缓存命中当成模型前向。 */
int m16_probe_score(const float *x,const double *d,const unsigned char *v,float *l,mic16_score_workspace *s){
 score_forwards++;return mic16_frozen_score(x,d,v,l,s);
}
int m16_probe_spectral_import(const float *x,mic16_spectral_frame *out,mic16_spectral_workspace *w){
 uint32_t tag[5];const float *expected;int rc;
 if(!cache || !x || !out || w!=&work.extended.physical.spectral || frame_index>=24)return -1;
 expected=frame_index<4?quiet+frame_index*2048u*16u:pcm+(frame_index-4)*2048u*16u;
 if(x!=expected){printf("CACHE_REJECT reason=pointer frame=%u\n",frame_index);return -1;}
 if(fread(tag,4,5,cache)!=5){printf("CACHE_REJECT reason=header_short\n");return -1;}
 if(tag[0]!=UINT32_C(0x4636314d) || tag[1]!=provider_case || tag[2]!=frame_index || tag[3]!=25 || tag[4]!=checksum(x,32768)){
  printf("CACHE_REJECT reason=identity frame=%u\n",frame_index);return -1;
 }
 if(fread(spectrum_words,8,M16_FRONT_SPECTRUM_WORDS,cache)!=M16_FRONT_SPECTRUM_WORDS){printf("CACHE_REJECT reason=payload_short\n");return -1;}
 rc=m16_frontend_import_f32_spectrum(spectrum_words,M16_FRONT_SPECTRUM_WORDS,25,out);
 if(rc){printf("CACHE_REJECT reason=spectrum\n");return rc;}
 frame_index++;cache_reads++;total_cache_reads++;return 0;
}
static int test_observe(const float *x,const float *r,mic16_pipeline_observation *o,mic16_pipeline_workspace *w){
 char path[1024];const char *name=provider_case?"case1.cache":"case0.cache";unsigned n=0,j=0;unsigned char trailing;int rc;
 if(!use_hls)return mic16_pipeline_observe_batched(x,r,o,w);
 /* 旧LLVM无snprintf导出；固定文件名配合有界路径复制，不依赖格式化扩展。 */
 while(cache_directory[n]){if(n>=sizeof(path)-14)return -1;path[n]=cache_directory[n];n++;}
 path[n++]='/';while(name[j])path[n++]=name[j++];path[n]=0;
 cache=fopen(path,"rb");if(!cache)return -1;frame_index=0;
 rc=m16_probe_pipeline_batched(x,r,o,w);
 if(!rc && (frame_index!=24 || fread(&trailing,1,1,cache)!=0))rc=-21;
 fclose(cache);cache=0;return rc;
}
static int test_consume(mic16_search_memory *m,const double *d,const unsigned char *v,const double *s,double end,const char *key,int contiguous,mic16_cycle_output *o){
 unsigned i;int rc=mic16_consume_scored_cycle(m,d,v,s,end,key,contiguous,o);uint32_t h[8];
 if(rc || !events)return rc;
 h[0]=(unsigned)o->cycle;h[1]=(unsigned)o->count;h[2]=(unsigned)o->confirmed_count;h[3]=(unsigned)o->expired_count;
 h[4]=o->new_source_valid?o->new_source.source_id:0;h[5]=(unsigned)o->pose_reset;h[6]=(unsigned)o->gap_reset;h[7]=(unsigned)o->new_source_valid;
 if(write_(h,sizeof(h),events))return -90;
 for(i=0;i<(unsigned)o->count;i++){
  const mic16_track *t=o->tracks+i;uint32_t a[4]={t->source_id,(unsigned)t->first_seen_cycle,(unsigned)t->last_seen_cycle,(unsigned)t->confirmed_this_cycle};
  double b[3]={t->azimuth,t->elevation,t->score};if(write_(a,sizeof(a),events) || write_(b,sizeof(b),events))return -90;
 }
 for(i=0;i<(unsigned)o->expired_count;i++)if(write_(o->expired_ids+i,4,events))return -90;
 events_written++;return 0;
}
static int dump(unsigned which){
 void *file;uint32_t h[12]={UINT32_C(0x5046314d),which,observation.candidate_count,g_mic16_pipeline_test.qualified,
  cache_reads,g_mic16_pipeline_test.pcm_checksum,g_mic16_pipeline_test.reference_checksum,g_mic16_pipeline_test.checks,g_mic16_pipeline_test.errors,41600,156000,4320};int bad;
 file=fopen(which?"case1.bin":"case0.bin","wb");if(!file)return -1;
 bad=write_(h,sizeof(h),file) || write_(observation.features,sizeof(observation.features),file) || write_(observation.logits,sizeof(observation.logits),file) ||
  write_(observation.directions,sizeof(observation.directions),file) || write_(observation.scores,sizeof(observation.scores),file) || write_(observation.valid,sizeof(observation.valid),file) ||
  write_(work.best_indices,sizeof(work.best_indices),file) || write_(work.maps,sizeof(work.maps),file) || write_(work.votes,sizeof(work.votes),file) ||
  write_(work.extended.physical.reference.floor_db,24*8,file) || write_(work.extended.physical.levels,20*24*8,file);
 if(fclose(file))bad=1;return bad?-1:0;
}
int main(int argc,char **argv){
 unsigned which;int rc;
 if(argc!=3 || (strcmp(argv[1],"cpu") && strcmp(argv[1],"hls")))return 2;
 use_hls=!strcmp(argv[1],"hls");cache_directory=argv[2];
 for(which=0;which<2;which++){
  provider_case=which;cache_reads=0;mic16_pipeline_test_prepare(which);
  rc=mic16_pipeline_test_compute();
  if(rc){printf("PIPELINE_EXECUTION_STOP case=%u rc=%d cache_reads=%u score_forwards=%u\n",which,rc,cache_reads,score_forwards);return 6;}
  events=fopen(which?"case1_events.bin":"case0_events.bin","wb");if(!events)return 3;
  (void)mic16_pipeline_test_check(which);if(fclose(events))return 4;events=0;
  if(dump(which))return 5;
 }
 printf("PIPELINE_NUMERIC_COMPLETE mode=%s checks=%u gold_errors=%u cache_frames=%u model_forwards=%u events=%u NO_BOARD\n",
  argv[1],g_mic16_pipeline_test.checks,g_mic16_pipeline_test.errors,total_cache_reads,score_forwards,events_written);
 /* 数值门失败是实验结果；执行/文件失败才返回非零，二者不得混淆。 */
 return 0;
}
