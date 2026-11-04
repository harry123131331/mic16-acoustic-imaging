#include "mic16_output_pool.h"
#include "mic16_heatmap_wire.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* 夹具只提供评分后的定位/连续重建输出，不替代真实检测或宣称采集实时。
 * 调度器、输出池、分步编码及原同步协议全部执行生产源码。 */
typedef struct {
 mic16_stream_config config;
 mic16_stream_scheduler scheduler;
 m16_output_pool pool;
 mic16_stream_ticket compute,build[3],capture;
 mic16_stream_window input_window;
 unsigned compute_held,capture_held;
 uint64_t now;
 float storage[M16_OUTPUT_POOL_FLOATS],maps[480*325];
 double a[M16_CONT_OUTPUT],b[M16_CONT_OUTPUT];
 mic16_cycle_output tracks;
 m16_cont_info info;
 m16_heatmap_workspace reference_map;
} fixture;
void *op_new(void){
 fixture *h=(fixture *)calloc(1,sizeof(*h));if(!h)return 0;
 h->now=100;h->config=(mic16_stream_config){3,3,1000000,1000000000,1000000000,1,1};
 if(mic16_stream_init(&h->scheduler,&h->config,27,h->now)||m16_output_pool_init(&h->pool,&h->scheduler,h->storage,M16_OUTPUT_POOL_FLOATS)){free(h);return 0;}return h;
}
static int slot(fixture *h,unsigned window){unsigned i;for(i=0;i<3;i++)if(h->pool.slots[i].bound&&h->pool.slots[i].window==window)return (int)i;return -1;}
int op_reserve(void *context,unsigned window){
 fixture *h=(fixture *)context;mic16_stream_ticket capture;int rc;
 if(!h->compute_held){
  rc=mic16_stream_input_reserve(&h->scheduler,(uint64_t)window*40960u,++h->now,&capture);if(rc)return rc;
  rc=mic16_stream_input_commit(&h->scheduler,&capture,++h->now);if(rc)return rc;
  rc=mic16_stream_input_acquire(&h->scheduler,++h->now,&h->compute,&h->input_window);if(rc)return rc;
  h->compute_held=1;
 }
 if(h->input_window.first_sample!=(uint64_t)window*40960u)return -1;
 rc=mic16_stream_output_reserve(&h->scheduler,&h->compute,++h->now,&capture);if(rc)return rc;
 h->build[capture.slot]=capture;return m16_output_pool_bind(&h->pool,&h->build[capture.slot],++h->now);
}
int op_release_input(void *context){fixture *h=(fixture *)context;int rc;if(!h->compute_held)return -1;
 rc=mic16_stream_input_release(&h->scheduler,&h->compute);if(rc!=M16_STREAM_LEASE&&rc!=M16_STREAM_INVALID)h->compute_held=0;return rc;}
int op_location(void *context,unsigned window,unsigned confirmed){
 fixture *h=(fixture *)context;unsigned i;int index=slot(h,window);if(index<0||confirmed>8)return -1;
 memset(&h->tracks,0,sizeof(h->tracks));h->tracks.count=h->tracks.confirmed_count=(int)confirmed;h->tracks.cycle=(int)window;
 for(i=0;i<confirmed;i++){h->tracks.tracks[i].source_id=i+1;h->tracks.tracks[i].azimuth=-20.+(double)i*5.;h->tracks.tracks[i].elevation=(double)i;
  h->tracks.tracks[i].score=.75;h->tracks.tracks[i].confirmed_this_cycle=1;}
 for(i=0;i<480u*325u;i++)h->maps[i]=(float)((int)((i*37u+window*17u)%257u)-128)/128.f;
 return m16_output_pool_location(&h->pool,&h->build[index],&h->tracks,h->maps,++h->now);
}
int op_reference_location(void *context,unsigned window,m16_source_datagram_sink sink,void *user){
 fixture *h=(fixture *)context;int rc=m16_directions_encode(h->scheduler.epoch,window,&h->tracks,sink,user);if(rc!=1)return rc;
 return m16_heatmap_encode(h->scheduler.epoch,window,h->maps,&h->reference_map,sink,user);
}
int op_location_releasable(void *context,unsigned window){fixture *h=(fixture *)context;int i=slot(h,window);
 return i>=0?m16_output_pool_location_releasable(&h->pool,&h->build[i]):0;}
void op_poison_maps(void *context){fixture *h=(fixture *)context;unsigned i;for(i=0;i<480u*325u;i++)h->maps[i]=NAN;}
int op_prepare_audio(void *context,unsigned window,unsigned ordinal,unsigned count,unsigned final){
 fixture *h=(fixture *)context;unsigned i,samples;uint32_t source=ordinal<count?ordinal+1u:0u;
 h->info=(m16_cont_info){0};h->info.epoch=h->scheduler.epoch;h->info.window=window;h->info.source_id=source;
 h->info.ordinal=ordinal;h->info.stream_count=count;h->info.current_confirmed=source&&source<=8u;h->info.final=final;
 h->info.output_start=((uint64_t)window*40960u*256u+624u)/625u;h->info.output_end=(((uint64_t)window+1u)*40960u*256u+624u)/625u;
 h->info.input_start=(uint64_t)window*40960u;h->info.input_end=((uint64_t)window+1u)*40960u;
 h->info.available_input=((uint64_t)window+(final?1u:2u))*40960u;samples=(unsigned)(h->info.output_end-h->info.output_start);
 for(i=0;i<samples;i++){h->a[i]=(double)((int)((i*7u+window*11u+source*3u)%997u)-498)/997.;h->b[i]=h->a[i]*.137+(double)(i%13u)*1e-7;}
 h->a[0]=-0.;return 0;
}
int op_reference_audio(void *context,m16_source_datagram_sink sink,void *user){fixture *h=(fixture *)context;
 return m16_source_encode(&h->info,h->a,h->b,(unsigned)(h->info.output_end-h->info.output_start),sink,user);}
int op_begin_audio(void *context){fixture *h=(fixture *)context;
 return m16_output_pool_audio_begin(&h->pool,&h->info,h->a,h->b,(unsigned)(h->info.output_end-h->info.output_start),++h->now);}
int op_audio_copied(void *context){fixture *h=(fixture *)context;const m16_cont_info *info=0;return m16_output_pool_audio_copied(&h->pool,&info);}
int op_release_audio(void *context,unsigned wrong){fixture *h=(fixture *)context;
 return m16_output_pool_audio_release(&h->pool,h->info.epoch,h->info.window,h->info.source_id+wrong);}
void op_poison_audio(void *context,unsigned last_only){fixture *h=(fixture *)context;unsigned i,n=(unsigned)(h->info.output_end-h->info.output_start);
 if(last_only){h->b[n-1u]=NAN;return;}for(i=0;i<n;i++){h->a[i]=NAN;h->b[i]=NAN;}}
int op_commit(void *context,unsigned window){fixture *h=(fixture *)context;return m16_output_pool_audio_commit(&h->pool,h->scheduler.epoch,window,++h->now);}
int op_step(void *context){fixture *h=(fixture *)context;return m16_output_pool_step(&h->pool,++h->now);}
int op_packet(void *context,m16_output_packet_view *view){fixture *h=(fixture *)context;return m16_output_pool_packet(&h->pool,++h->now,view);}
int op_ack(void *context,const m16_output_packet_ticket *ticket){fixture *h=(fixture *)context;return m16_output_pool_packet_ack(&h->pool,ticket,++h->now);}
int op_stop(void *context){fixture *h=(fixture *)context;return m16_output_pool_stop(&h->pool);}
int op_restart(void *context,unsigned epoch){fixture *h=(fixture *)context;int rc=mic16_stream_restart(&h->scheduler,epoch,1,1,++h->now);
 if(rc)return rc;
 return m16_output_pool_restart(&h->pool,epoch);}
int op_capture_hold(void *context){fixture *h=(fixture *)context;int rc=mic16_stream_input_reserve(&h->scheduler,h->scheduler.next_input_first,++h->now,&h->capture);
 if(!rc)h->capture_held=1;
 return rc;}
int op_capture_release(void *context){fixture *h=(fixture *)context;int rc;if(!h->capture_held)return -1;
 rc=mic16_stream_input_release(&h->scheduler,&h->capture);if(rc!=M16_STREAM_LEASE&&rc!=M16_STREAM_INVALID)h->capture_held=0;return rc;}
int op_advance(void *context,uint64_t delta){fixture *h=(fixture *)context;h->now+=delta;return m16_output_pool_step(&h->pool,h->now);}
int64_t op_value(void *context,unsigned key){fixture *h=(fixture *)context;unsigned i,n=0;
 switch(key){case 0:return h->scheduler.failure;case 1:return (int64_t)h->scheduler.stats.outputs_sent;
 case 2:return (int64_t)h->pool.windows_sent;case 3:for(i=0;i<3;i++)n+=h->scheduler.input[i].state==M16_STREAM_COMPUTE||h->scheduler.input[i].state==M16_STREAM_CAPTURE;return n;
 case 4:for(i=0;i<3;i++)n+=h->scheduler.output[i].state!=M16_STREAM_FREE;return n;
 case 5:return h->pool.pending;case 6:return h->pool.copy_phase;case 7:return h->pool.send_active;case 8:return h->pool.error;
 case 9:return (int64_t)h->pool.packets_sent;case 10:return (int64_t)h->scheduler.stats.output_backpressure;case 11:return m16_output_pool_releasable(&h->pool);
 case 12:return h->compute_held;case 13:return (int64_t)h->pool.packet_serial;default:return -1;}}
void op_counter_max(void *context){fixture *h=(fixture *)context;h->pool.packet_serial=UINT64_MAX;}
int op_alias_location(void *context,unsigned window){fixture *h=(fixture *)context;int i=slot(h,window);if(i<0)return -1;
 return m16_output_pool_location(&h->pool,&h->build[i],&h->tracks,h->storage,++h->now);}
int op_alias_init(void *context){fixture *h=(fixture *)context;return m16_output_pool_init((m16_output_pool *)h->storage,&h->scheduler,h->storage,M16_OUTPUT_POOL_FLOATS);}
unsigned op_view_size(void){return sizeof(m16_output_packet_view);}
unsigned op_ticket_size(void){return sizeof(m16_output_packet_ticket);}
void op_delete(void *context){fixture *h=(fixture *)context;if(!h)return;
 /* 本夹具没有物理DMA/PL，只归还模型所有者；不能移作实板强制释放。 */
 (void)m16_output_pool_stop(&h->pool);if(h->compute_held)(void)op_release_input(h);if(h->capture_held)(void)op_capture_release(h);free(h);
}
