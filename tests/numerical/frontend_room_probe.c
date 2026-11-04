/* 普通会议室三支路离线入口；只替换频谱源，不修改正式固件。 */
#include "mic16_pipeline.h"
#include "mic16_stft_batch.h"
#include "mic16_frontend_bridge.h"
#include "mic16_mic_positions.h"
#include <stdint.h>
#include <stddef.h>
extern void *fopen(const char *,const char *);
extern size_t fread(void *,size_t,size_t,void *),fwrite(const void *,size_t,size_t,void *);
extern int fclose(void *),strcmp(const char *,const char *),printf(const char *,...);
extern int m16_room_pipeline(const float *,const float *,mic16_pipeline_observation *,mic16_pipeline_workspace *);
static float pcm[40960*16],quiet[8192*16];
static mic16_pipeline_workspace work;
static mic16_pipeline_observation observation;
static mic16_search_memory memory;
static mic16_cycle_output event;
static uint64_t words[M16_FRONT_SPECTRUM_WORDS];
static void *cache;
static unsigned which,frame_index,forwards;
static int write_(const void *p,size_t n,void *f){return fwrite(p,1,n,f)==n?0:-1;}
static unsigned checksum(const float *x,unsigned n){
 unsigned i,j,h=2166136261u;union{float f;uint32_t u;}v;
 for(i=0;i<n;i++){v.f=x[i];for(j=0;j<4;j++){h^=(v.u>>(j*8))&255u;h*=16777619u;}}
 return h;
}
int m16_room_score(const float *x,const double *d,const unsigned char *v,float *l,mic16_score_workspace *s){
 forwards++;return mic16_frozen_score(x,d,v,l,s);
}
int m16_room_spectrum(const float *x,mic16_spectral_frame *o,mic16_spectral_workspace *w){
 uint32_t tag[5];const float *p;int rc;
 if(!cache || frame_index>=24 || w!=&work.extended.physical.spectral)return -1;
 p=frame_index<4?quiet+frame_index*32768u:pcm+(frame_index-4)*32768u;
 if(x!=p || fread(tag,4,5,cache)!=5)return -1;
 if(tag[0]!=UINT32_C(0x4636314d) || tag[1]!=which || tag[2]!=frame_index || tag[3]!=31 || tag[4]!=checksum(x,32768))return -1;
 if(fread(words,8,M16_FRONT_SPECTRUM_WORDS,cache)!=M16_FRONT_SPECTRUM_WORDS)return -1;
 rc=m16_frontend_import_f32_spectrum(words,M16_FRONT_SPECTRUM_WORDS,31,o);
 if(!rc)frame_index++;return rc;
}
static int dump(void){
 unsigned i,q=0;void *f=fopen("observation.bin","wb");int bad;
 uint32_t h[12]={UINT32_C(0x5046314d),which,observation.candidate_count,0,frame_index,
  checksum(pcm,40960*16),checksum(quiet,8192*16),0,0,41600,156000,4320};
 if(!f)return -1;
 for(i=0;i<16;i++)if(observation.scores[i]>=.65)q++;h[3]=q;
 bad=write_(h,sizeof(h),f) || write_(observation.features,sizeof(observation.features),f) || write_(observation.logits,sizeof(observation.logits),f) ||
  write_(observation.directions,sizeof(observation.directions),f) || write_(observation.scores,sizeof(observation.scores),f) || write_(observation.valid,sizeof(observation.valid),f) ||
  write_(work.best_indices,sizeof(work.best_indices),f) || write_(work.maps,sizeof(work.maps),f) || write_(work.votes,sizeof(work.votes),f) ||
  write_(work.extended.physical.reference.floor_db,24*8,f) || write_(work.extended.physical.levels,20*24*8,f);
 if(fclose(f))bad=1;return bad?-1:0;
}
static int events(void){
 unsigned cycle,i;double empty[16]={0};void *f=fopen("events.bin","wb");int rc;
 if(!f)return -1;mic16_memory_init(&memory);
 /* 同一观测重复两次、随后三次空分数，仅检查状态兼容，不冒充连续场景。 */
 for(cycle=0;cycle<5;cycle++){
  uint32_t h[8];rc=mic16_consume_scored_cycle(&memory,observation.directions,observation.valid,
   cycle<2?observation.scores:empty,(cycle+1)*1.048576,"room_isolated_compatibility",1,&event);
  if(rc){fclose(f);return rc;}
  h[0]=event.cycle;h[1]=event.count;h[2]=event.confirmed_count;h[3]=event.expired_count;
  h[4]=event.new_source_valid?event.new_source.source_id:0;h[5]=event.pose_reset;h[6]=event.gap_reset;h[7]=event.new_source_valid;
  if(write_(h,sizeof(h),f)){fclose(f);return -1;}
  for(i=0;i<(unsigned)event.count;i++){
   const mic16_track *t=event.tracks+i;uint32_t a[4]={t->source_id,t->first_seen_cycle,t->last_seen_cycle,t->confirmed_this_cycle};
   double b[3]={t->azimuth,t->elevation,t->score};if(write_(a,sizeof(a),f)||write_(b,sizeof(b),f)){fclose(f);return -1;}
  }
  for(i=0;i<(unsigned)event.expired_count;i++)if(write_(event.expired_ids+i,4,f)){fclose(f);return -1;}
 }
 return fclose(f)?-1:0;
}
int main(int argc,char **argv){
 void *f;unsigned char extra;int rc,use_hls;
 if(argc==2 && !strcmp(argv[1],"geometry")){
  f=fopen("mic_positions.bin","wb");if(!f)return 3;
  rc=write_(m16_mic_xyz,sizeof(m16_mic_xyz),f);if(fclose(f))rc=-1;return rc?3:0;
 }
 if(argc!=5 || (strcmp(argv[1],"cpu") && strcmp(argv[1],"hls")) || argv[2][0]<'0' || argv[2][0]>'7' || argv[2][1])return 2;
 which=(unsigned)(argv[2][0]-'0');use_hls=!strcmp(argv[1],"hls");f=fopen(argv[3],"rb");if(!f)return 3;
 if(fread(quiet,4,8192*16,f)!=8192*16 || fread(pcm,4,40960*16,f)!=40960*16 || fread(&extra,1,1,f)!=0){fclose(f);return 3;}
 if(fclose(f))return 3;
 if(use_hls){cache=fopen(argv[4],"rb");if(!cache)return 3;rc=m16_room_pipeline(pcm,quiet,&observation,&work);
  if(!rc && (frame_index!=24 || fread(&extra,1,1,cache)!=0))rc=-20;
  if(fclose(cache))rc=-20;cache=0;
 }else rc=mic16_pipeline_observe_batched(pcm,quiet,&observation,&work);
 if(rc){printf("ROOM_STOP case=%u rc=%d forwards=%u frames=%u\n",which,rc,forwards,frame_index);return 6;}
 if(dump() || events())return 4;
 printf("ROOM_COMPLETE case=%u mode=%s forwards=%u cache_frames=%u events=5\n",which,argv[1],forwards,frame_index);
 return 0;
}
