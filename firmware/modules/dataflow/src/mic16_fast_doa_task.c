#include "mic16_fast_doa_task.h"
#include <float.h>
#include <string.h>
#include "mic16_fast_doa_near15.inc"
#define MAGIC 0x46444132u
static int valid(const m16_fast_doa_task *t){return t&&t->magic==MAGIC;}
static int finite64(double x){return x>=-DBL_MAX&&x<=DBL_MAX;}
static int apart(const void *a,uintptr_t an,const void *b,uintptr_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an&&bn&&x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&(x+an<=y||y+bn<=x);
}
static int near(unsigned a,unsigned b){return (m16_fd_near15[a][b/8u]>>(b%8u))&1u;}
static int fail(m16_fast_doa_task *t,int rc){t->error=(unsigned)(-rc);return rc;}
int m16_fast_doa_init(m16_fast_doa_task *t){
 if(!t||t->magic||(uintptr_t)t%_Alignof(m16_fast_doa_task))return M16_FD_INVALID;
 memset(t,0,sizeof(*t));t->magic=MAGIC;return 0;
}
int m16_fast_doa_start(m16_fast_doa_task *t,uint64_t epoch,uint64_t tick){
 if(!valid(t)||t->active||!epoch||epoch<=t->epoch)return M16_FD_SEQUENCE;
 /* 固定DDR存储只在START清状态；原始SRP/FFT不属于此对象。 */
 memset(t,0,sizeof(*t));t->magic=MAGIC;t->active=1;t->epoch=epoch;t->last_tick=tick;
 return 0;
}
int m16_fast_doa_can_accept(const m16_fast_doa_task *t){
 return valid(t)&&t->active&&!t->error&&t->phase==M16_FD_IDLE&&!t->output_ready;
}
int m16_fast_doa_begin(m16_fast_doa_task *t,const float *maps,const double *levels,
 uint64_t epoch,uint64_t frame,uint64_t first,uint64_t received,uint64_t done){
 if(!valid(t)||!maps||!levels||(uintptr_t)maps%_Alignof(float)||
  (uintptr_t)levels%_Alignof(double)||!apart(t,sizeof(*t),maps,24u*325u*4u)||
  !apart(t,sizeof(*t),levels,24u*8u))return M16_FD_INVALID;
 if(!m16_fast_doa_can_accept(t)){t->busy_count++;return M16_FD_BUSY;}
 if(epoch!=t->epoch||frame!=t->next_frame||frame>UINT64_MAX/2048u-1u||first!=frame*2048u)return M16_FD_SEQUENCE;
 if(done<received||done>t->last_tick)return M16_FD_CLOCK;
 for(unsigned b=0;b<24u;b++)if(!finite64(levels[b]))return M16_FD_VALUE;
 memcpy(t->input,maps,sizeof(t->input));memcpy(t->levels,levels,sizeof(t->levels));
 t->maximum_level=levels[0];for(unsigned b=1;b<24u;b++)if(levels[b]>t->maximum_level)t->maximum_level=levels[b];
 if(!t->history_count)t->group_received_tick=received;
 t->last_front_tick=done;t->band=t->cursor=0;t->band_grid=0;t->band_peak=-DBL_MAX;t->total=0;
 t->band_counts[t->history_count]=0;t->frame_valid[t->history_count]=0;
 memset(t->history[t->history_count],0,sizeof(t->history[0]));
 t->phase=M16_FD_MAX;return 0;
}
static void next_band(m16_fast_doa_task *t){
 t->cursor=0;
 if(++t->band==24u){t->phase=M16_FD_NORMALIZE;t->frame_max=-DBL_MAX;t->frame_grid=0;}
 else{t->phase=M16_FD_MAX;t->band_peak=-DBL_MAX;t->band_grid=0;}
}
static void publish(m16_fast_doa_task *t,uint64_t tick,unsigned map_valid){
 m16_fast_doa_result *r=&t->result;unsigned support=0;
 memset(r,0,sizeof(*r));r->epoch=t->epoch;r->sequence=t->sequence++;
 r->end_frame=t->next_frame;r->first_frame=r->end_frame-4u;
 r->first_sample=r->first_frame*2048u;r->end_sample=r->end_frame*2048u;
 r->first_received_tick=t->group_received_tick;r->last_front_tick=t->last_front_tick;r->completed_tick=tick;
 r->map_valid=map_valid;r->grid=t->frame_grid;
 for(unsigned h=0;h<4u;h++){
  r->selected_bands[h]=t->band_counts[h];
  if(map_valid&&t->frame_valid[h]&&near(t->frame_grid,t->frame_peak[h]))support++;
 }
 r->support_frames=support;r->support_fraction=(double)support/4.;
 if(map_valid){
  r->azimuth_deg=-60.+5.*(double)(t->frame_grid%25u);
  r->elevation_deg=-30.+5.*(double)(t->frame_grid/25u);
  r->coherence=t->coherence;r->contrast=t->contrast;
  /* 响应转换为原协议采用的F32；峰值和质量判定保持FP64归约。 */
  for(unsigned g=0;g<325u;g++)r->response[g]=(float)t->fused[g];
 }
 r->valid=map_valid&&r->coherence>=.15&&r->contrast>=.02&&r->support_fraction>=.5;
 r->reason=r->valid?M16_FD_VALID:(map_valid?M16_FD_LOW_QUALITY:M16_FD_NO_BAND);
 t->history_count=0;t->output_ready=1;t->results_produced++;t->phase=M16_FD_IDLE;
}
int m16_fast_doa_step(m16_fast_doa_task *t,uint64_t tick){
 unsigned end;
 if(!valid(t))return M16_FD_INVALID;
 if(t->error)return -(int)t->error;
 if(tick<t->last_tick)return fail(t,M16_FD_CLOCK);
 t->last_tick=tick;
 if(!t->active)return 2;
 if(t->phase==M16_FD_IDLE)return 0;
 t->steps++;end=t->cursor+64u;if(end>325u)end=325u;
 if(t->phase==M16_FD_MAX){
  for(unsigned g=t->cursor;g<end;g++){
   double v=t->input[t->band][g];
   if(!(v>=-2.000005&&v<=2.000005))return fail(t,M16_FD_VALUE);
   if(v>t->band_peak){t->band_peak=v;t->band_grid=g;}
  }
  t->cursor=end;
  if(end==325u){t->cursor=0;t->second=-DBL_MAX;t->phase=M16_FD_SECOND;}
 }else if(t->phase==M16_FD_SECOND){
  for(unsigned g=t->cursor;g<end;g++)if(!near(t->band_grid,g)&&t->input[t->band][g]>t->second)t->second=t->input[t->band][g];
  t->cursor=end;
  if(end==325u){
   double contrast=t->band_peak-t->second;
   if(t->levels[t->band]>=t->maximum_level-20.&&t->band_peak>=.15&&contrast>=.02){
    t->weight=t->band_peak*contrast;t->total+=t->weight;t->band_counts[t->history_count]++;
    t->phase=M16_FD_ADD;t->cursor=0;
   }else next_band(t);
  }
 }else if(t->phase==M16_FD_ADD){
  for(unsigned g=t->cursor;g<end;g++)t->history[t->history_count][g]+=(double)t->input[t->band][g]*t->weight;
  t->cursor=end;if(end==325u)next_band(t);
 }else if(t->phase==M16_FD_NORMALIZE){
  for(unsigned g=t->cursor;g<end;g++){
   double v=t->total>0.?t->history[t->history_count][g]/t->total:0.;
   t->history[t->history_count][g]=v;
   if(v>t->frame_max){t->frame_max=v;t->frame_grid=g;}
  }
  t->cursor=end;
  if(end==325u){
   t->frame_peak[t->history_count]=t->frame_grid;t->frame_valid[t->history_count]=t->total>0.;
   t->next_frame++;t->frames_consumed++;t->history_count++;t->cursor=0;
   if(t->history_count==4u){
    unsigned count=0;for(unsigned h=0;h<4u;h++)count+=t->frame_valid[h];
    if(!count){publish(t,tick,0);return 0;}
    t->total=(double)count;t->phase=M16_FD_FUSE;
   }else t->phase=M16_FD_IDLE;
  }
 }else if(t->phase==M16_FD_FUSE){
  for(unsigned g=t->cursor;g<end;g++){
   double sum=0.;for(unsigned h=0;h<4u;h++)if(t->frame_valid[h])sum+=t->history[h][g];
   t->fused[g]=sum/t->total;
  }
  t->cursor=end;if(end==325u){t->phase=M16_FD_PEAK;t->cursor=0;t->coherence=-DBL_MAX;t->frame_grid=0;}
 }else if(t->phase==M16_FD_PEAK){
  for(unsigned g=t->cursor;g<end;g++)if(t->fused[g]>t->coherence){t->coherence=t->fused[g];t->frame_grid=g;}
  t->cursor=end;if(end==325u){t->phase=M16_FD_CONTRAST;t->cursor=0;t->second=-DBL_MAX;}
 }else if(t->phase==M16_FD_CONTRAST){
  for(unsigned g=t->cursor;g<end;g++)if(!near(t->frame_grid,g)&&t->fused[g]>t->second)t->second=t->fused[g];
  t->cursor=end;
  if(end==325u){t->contrast=t->coherence-t->second;if(t->contrast<0.)t->contrast=0.;publish(t,tick,1);}
 }else return fail(t,M16_FD_INVALID);
 return t->phase==M16_FD_IDLE?0:1;
}
int m16_fast_doa_peek(const m16_fast_doa_task *t,const m16_fast_doa_result **out){
 if(!valid(t)||!out)return M16_FD_INVALID;
 if(!t->active||!t->output_ready||t->error)return 0;
 *out=&t->result;return 1;
}
int m16_fast_doa_ack(m16_fast_doa_task *t,uint64_t epoch,uint64_t sequence){
 if(!valid(t)||!t->output_ready||epoch!=t->result.epoch||sequence!=t->result.sequence)return M16_FD_SEQUENCE;
 t->output_ready=0;return 0;
}
int m16_fast_doa_stop(m16_fast_doa_task *t){
 if(!valid(t))return M16_FD_INVALID;
 t->active=0;t->phase=M16_FD_IDLE;t->output_ready=0;t->history_count=0;return 1;
}
